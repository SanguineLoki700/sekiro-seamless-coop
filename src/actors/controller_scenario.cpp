#include "skcoop/actors/controller_scenario.hpp"
#include "skcoop/actors/native_input_match.hpp"
#include <algorithm>
#include <cmath>

namespace skcoop::actors {
namespace {
using Phase=ControllerScenarioPhase;
using Fault=ControllerScenarioFault;
bool finite_position(const std::array<float,3>& value)noexcept{
    for(const auto v:value)if(!std::isfinite(v)||std::abs(v)>=1e7f)return false;
    return true;
}
float distance(const std::array<float,3>& a,const std::array<float,3>& b)noexcept{
    return std::hypot(a[0]-b[0],a[1]-b[1],a[2]-b[2]);
}
bool resolved_input_matches(const PlayerInput& expected,const PlayerInput& actual)noexcept{
    if(expected.held!=actual.held)return false;
    return native_movement_matches(expected.movement,actual.movement);
}
PlayerInput input_for(Phase phase)noexcept{
    PlayerInput input;
    if(phase==Phase::moving)input.movement={{0,1},.35f};
    else if(phase==Phase::attack_press)input.held=button_mask(CommandButton::attack);
    else if(phase==Phase::guard_hold)input.held=button_mask(CommandButton::guard);
    return input;
}
std::uint64_t duration(Phase phase)noexcept{
    switch(phase){
    case Phase::neutral:return 500;
    case Phase::moving:return 800;
    case Phase::movement_release:return 500;
    case Phase::attack_press:return 200;
    case Phase::attack_release:return 2500;
    case Phase::guard_hold:return 3000;
    case Phase::guard_release:return 1200;
    case Phase::final_neutral:return 300;
    default:return 0;
    }
}
}
bool terminal(Phase phase)noexcept{return phase==Phase::complete||phase==Phase::stopped||phase==Phase::failed||phase==Phase::inconclusive;}
std::string_view controller_scenario_phase_name(Phase phase)noexcept{
    switch(phase){
    case Phase::waiting_ready:return "waiting_ready";
    case Phase::neutral:return "neutral";
    case Phase::moving:return "moving";
    case Phase::movement_release:return "movement_release";
    case Phase::attack_press:return "attack_press";
    case Phase::attack_release:return "attack_release";
    case Phase::guard_hold:return "guard_hold";
    case Phase::guard_release:return "guard_release";
    case Phase::final_neutral:return "final_neutral";
    case Phase::complete:return "complete";
    case Phase::stopped:return "stopped";
    case Phase::failed:return "failed";
    case Phase::inconclusive:return "inconclusive";
    }
    return "unknown";
}
std::string_view controller_scenario_fault_name(Fault fault)noexcept{
    switch(fault){
#define SKCOOP_SCENARIO_FAULT(name) case Fault::name:return #name
    SKCOOP_SCENARIO_FAULT(none);SKCOOP_SCENARIO_FAULT(invalid_generation);
    SKCOOP_SCENARIO_FAULT(clock_regressed);SKCOOP_SCENARIO_FAULT(tick_gap);
    SKCOOP_SCENARIO_FAULT(preparation_timeout);SKCOOP_SCENARIO_FAULT(total_timeout);
    SKCOOP_SCENARIO_FAULT(invalid_observation);SKCOOP_SCENARIO_FAULT(stale_observation);
    SKCOOP_SCENARIO_FAULT(readiness_lost);SKCOOP_SCENARIO_FAULT(scope_changed);
    SKCOOP_SCENARIO_FAULT(unsafe_driver);SKCOOP_SCENARIO_FAULT(primary_changed);
    SKCOOP_SCENARIO_FAULT(command_failure);SKCOOP_SCENARIO_FAULT(invalid_consumption);
    SKCOOP_SCENARIO_FAULT(consumer_timeout);SKCOOP_SCENARIO_FAULT(not_neutral);
    SKCOOP_SCENARIO_FAULT(movement_missing);SKCOOP_SCENARIO_FAULT(movement_exceeded);
    SKCOOP_SCENARIO_FAULT(movement_not_released);SKCOOP_SCENARIO_FAULT(attack_missing);
    SKCOOP_SCENARIO_FAULT(attack_not_released);SKCOOP_SCENARIO_FAULT(guard_missing);
    SKCOOP_SCENARIO_FAULT(guard_not_held);SKCOOP_SCENARIO_FAULT(guard_not_released);
    SKCOOP_SCENARIO_FAULT(action_result_unbound);
#undef SKCOOP_SCENARIO_FAULT
    }
    return "unknown";
}
ControllerScenario::ControllerScenario(std::uint64_t generation,std::uint64_t now,ControllerScenarioMode mode)noexcept
    :capture_(generation,button_mask(CommandButton::attack)|button_mask(CommandButton::guard)),
     mode_(mode),generation_(generation),start_(now),checked_(now){
    if(!generation)fail(Fault::invalid_generation);
}
bool ControllerScenario::output_assertions_bound(const ControllerScenarioObservation& o)const noexcept{
    return o.action_output_bound&&!report_.action_result_unbound;
}
bool ControllerScenario::observe_output_binding(const ControllerScenarioObservation& o)noexcept{
    if(o.action_output_bound)return true;
    report_.action_result_unbound=true;
    report_.attack_observed=report_.guard_released=false;report_.guard_held_ms=0;
    guard_seen_=guard_consecutive_=false;
    if(mode_!=ControllerScenarioMode::diagnostic_unbound){fail(Fault::action_result_unbound);return false;}
    return true;
}
ControllerScenarioStep ControllerScenario::fail(Fault fault)noexcept{
    if(report_.fault==Fault::none){report_.fault=fault;report_.phase=Phase::failed;}
    return {};
}
void ControllerScenario::enter(Phase phase,std::uint64_t now)noexcept{
    report_.phase=phase;phase_at_=now;phase_first_=0;phase_consumed_=false;
}
ControllerScenarioStep ControllerScenario::emit(std::uint64_t now)noexcept{
    const auto input=input_for(report_.phase);
    if(!capture_.capture(input,now-start_))return fail(Fault::command_failure);
    const auto command=capture_.latest();if(!command)return fail(Fault::command_failure);
    const auto index=static_cast<std::size_t>(report_.phase);
    if(index>=issued_.size())return fail(Fault::command_failure);
    auto& range=issued_[index];if(!range.first)range.first=command->frame_sequence;
    range.last=command->frame_sequence;range.input=input;
    if(!phase_first_)phase_first_=command->frame_sequence;
    last_issued_=command->frame_sequence;++report_.commands;
    // This acknowledges the local hand-off only. The separate observation
    // below is mandatory before this oracle credits native consumption.
    if(!capture_.offered(command->transition_latest))return fail(Fault::command_failure);
    return {command,false};
}
bool ControllerScenario::check_invariants(std::uint64_t now,const ControllerScenarioObservation& o)noexcept{
    if(!o.guest.valid||!o.guest.ready||!o.ownership_verified){fail(Fault::readiness_lost);return false;}
    if(!o.sample_sequence||!o.lifetime||!finite_position(o.guest.position)||!finite_position(o.primary_position)||
       !valid(o.primary_input)||now<o.observed_ms||o.sample_sequence<sample_||
       (o.sample_sequence==sample_&&o.observed_ms!=sample_at_)||
       (o.sample_sequence>sample_&&o.observed_ms<sample_at_)){
        fail(Fault::invalid_observation);return false;
    }
    if(now-o.observed_ms>sample_expiry_ms){fail(Fault::stale_observation);return false;}
    if(o.lifetime!=lifetime_){fail(Fault::scope_changed);return false;}
    if(o.pose_setter_calls!=1||o.manual_hkb_enqueues||o.direct_graph_writes||o.other_driver_commands||o.primary_write_calls){
        fail(Fault::unsafe_driver);return false;
    }
    if(o.primary_input!=PlayerInput{}||distance(o.primary_position,primary_origin_)>.1f){fail(Fault::primary_changed);return false;}
    const auto displacement=distance(o.guest.position,guest_origin_);
    if(displacement>max_displacement){fail(Fault::movement_exceeded);return false;}
    if(report_.phase==Phase::neutral&&displacement>.1f){fail(Fault::movement_not_released);return false;}
    if(!observe_output_binding(o))return false;
    if(output_assertions_bound(o)&&((report_.phase<Phase::attack_press&&o.guest.attacking)||
       (report_.phase<Phase::guard_hold&&o.guest.guarding)||
       (report_.phase>=Phase::guard_hold&&o.guest.attacking))){
        fail(Fault::not_neutral);return false;
    }
    return true;
}
bool ControllerScenario::observe_consumer(std::uint64_t now,const ControllerScenarioObservation& o,bool fresh)noexcept{
    if(!fresh){guard_consecutive_=false;return true;}
    ++report_.observations;
    // Once native neutral consumption establishes the stop anchor, every
    // fresh physical sample belongs to the envelope. A missing consumer
    // observation cannot hide an excursion that later returns to the anchor.
    if(report_.phase==Phase::movement_release&&stop_origin_set_)
        stop_drift_=std::max(stop_drift_,distance(o.guest.position,movement_stop_origin_));
    if(!o.consumer_observed){guard_consecutive_=false;return true;}
    const auto sequence=o.guest.consumed_frame_sequence;
    if(o.guest.consumed_input_generation!=generation_||!sequence||sequence>last_issued_||sequence<last_consumed_||!valid(o.resolved_input)){
        fail(Fault::invalid_consumption);return false;
    }
    const auto range=std::find_if(issued_.begin(),issued_.end(),[&](const auto& issued){return issued.first&&sequence>=issued.first&&sequence<=issued.last;});
    if(range==issued_.end()||!resolved_input_matches(range->input,o.resolved_input)){fail(Fault::invalid_consumption);return false;}
    if(sequence>last_consumed_){last_consumed_=sequence;progress_at_=now;++report_.consumed_commands;}
    if(!phase_first_||sequence<phase_first_){guard_consecutive_=false;return true;}
    phase_consumed_=true;
    const auto phase=report_.phase;
    if((phase==Phase::guard_hold||phase==Phase::guard_release)&&!o.held_state_observed){fail(Fault::invalid_consumption);return false;}
    if(phase==Phase::moving||phase==Phase::movement_release){
        const auto direction=input_for(Phase::moving).movement.direction;
        const auto progress=(o.guest.position[0]-movement_origin_[0])*direction[0]+
                            (o.guest.position[2]-movement_origin_[2])*direction[1];
        // Walking needs forward progress on the commanded world plane. Gravity,
        // lateral displacement and reverse motion cannot satisfy this outcome.
        report_.displacement=std::max(report_.displacement,progress);
    }
    if(phase==Phase::movement_release){
        if(!stop_origin_set_){movement_stop_origin_=o.guest.position;stop_origin_set_=true;}
    }
    // Input consumption and physical motion above remain mandatory. Once any
    // classification sample was unbound, this diagnostic cannot retrospectively
    // credit attack/guard outcomes, even if a later sample becomes bound.
    if(!output_assertions_bound(o)){guard_consecutive_=false;return true;}
    if(phase==Phase::neutral&&(o.guest.attacking||o.guest.guarding)){fail(Fault::not_neutral);return false;}
    if((phase==Phase::attack_press||phase==Phase::attack_release)&&o.guest.attacking)report_.attack_observed=true;
    if(phase==Phase::guard_hold){
        if(o.guest.guarding){
            if(guard_consecutive_)report_.guard_held_ms+=o.observed_ms-guard_last_;
            guard_seen_=guard_consecutive_=true;guard_last_=o.observed_ms;
        }
        else if(guard_seen_){fail(Fault::guard_not_held);return false;}
    }
    if(phase==Phase::guard_release){
        if(!o.guest.guarding)report_.guard_released=true;
        else if(report_.guard_released){fail(Fault::guard_not_released);return false;}
    }
    if(phase==Phase::final_neutral&&(o.guest.attacking||o.guest.guarding)){fail(Fault::not_neutral);return false;}
    return true;
}
ControllerScenarioStep ControllerScenario::tick(std::uint64_t now,const ControllerScenarioObservation& o)noexcept{
    if(terminal(report_.phase))return {};
    if(now<checked_)return fail(Fault::clock_regressed);
    const auto previous=checked_;checked_=now;
    if(stop_requested_){report_.phase=Phase::stopped;return {};}
    if(report_.phase==Phase::waiting_ready){
        if(now-start_>=preparation_timeout_ms)return fail(Fault::preparation_timeout);
        if(!o.guest.valid||!o.guest.ready||!o.ownership_verified||o.primary_input!=PlayerInput{}||
           (output_assertions_bound(o)&&(o.guest.attacking||o.guest.guarding)))return {};
        lifetime_=o.lifetime;primary_origin_=o.primary_position;guest_origin_=o.guest.position;
        if(!check_invariants(now,o))return {};
        sample_=o.sample_sequence;sample_at_=o.observed_ms;active_at_=progress_at_=now;
        enter(Phase::neutral,now);return emit(now);
    }
    if(now-previous>max_tick_gap_ms)return fail(Fault::tick_gap);
    if(now-active_at_>=max_run_ms)return fail(Fault::total_timeout);
    if(!check_invariants(now,o))return {};
    const bool fresh=o.sample_sequence>sample_;
    if(!observe_consumer(now,o,fresh))return {};
    if(fresh){sample_=o.sample_sequence;sample_at_=o.observed_ms;}
    if(now-progress_at_>consumer_timeout_ms||(!phase_consumed_&&now-phase_at_>consumer_timeout_ms))return fail(Fault::consumer_timeout);
    const auto elapsed=now-phase_at_;
    if(output_assertions_bound(o)&&report_.phase==Phase::guard_hold&&!guard_seen_&&elapsed>action_start_timeout_ms)return fail(Fault::guard_missing);
    if(elapsed<duration(report_.phase))return emit(now);
    if(!fresh)return emit(now); // A repeated/mutable sample cannot finish a stage.
    if(!phase_consumed_)return fail(Fault::consumer_timeout);
    switch(report_.phase){
    case Phase::neutral:movement_origin_=o.guest.position;enter(Phase::moving,now);break;
    case Phase::moving:enter(Phase::movement_release,now);break;
    case Phase::movement_release:
        if(report_.displacement<.15f)return fail(Fault::movement_missing);
        if(!stop_origin_set_||stop_drift_>.15f)return fail(Fault::movement_not_released);
        enter(Phase::attack_press,now);break;
    case Phase::attack_press:enter(Phase::attack_release,now);break;
    case Phase::attack_release:
        if(output_assertions_bound(o)){
            if(!report_.attack_observed)return fail(Fault::attack_missing);
            if(o.guest.attacking||!o.guest.action_idle)return fail(Fault::attack_not_released);
        }
        enter(Phase::guard_hold,now);break;
    case Phase::guard_hold:
        if(output_assertions_bound(o)){
            if(!guard_seen_)return fail(Fault::guard_missing);
            if(report_.guard_held_ms<1500)return fail(Fault::guard_not_held);
        }
        enter(Phase::guard_release,now);break;
    case Phase::guard_release:
        if(output_assertions_bound(o)&&(!report_.guard_released||o.guest.guarding||!o.guest.action_idle))return fail(Fault::guard_not_released);
        enter(Phase::final_neutral,now);break;
    case Phase::final_neutral:
        if(output_assertions_bound(o)&&!o.guest.action_idle)return fail(Fault::not_neutral);
        if(mode_==ControllerScenarioMode::diagnostic_unbound){report_.phase=Phase::inconclusive;report_.fault=Fault::action_result_unbound;}
        else report_.phase=Phase::complete;
        return {};
    default:return fail(Fault::command_failure);
    }
    return emit(now);
}
}
