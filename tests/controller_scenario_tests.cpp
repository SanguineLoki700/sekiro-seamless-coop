#include "skcoop/actors/controller_scenario.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace skcoop::actors;
namespace {
using Phase=ControllerScenarioPhase;
using Fault=ControllerScenarioFault;
using Mode=ControllerScenarioMode;
static_assert(static_cast<int>(Phase::inconclusive)==12&&static_cast<int>(Fault::action_result_unbound)==24);
void require(bool condition,int line){if(!condition)throw std::runtime_error("Controller scenario policy invariant at "+std::to_string(line));}
#define CHECK(value) require((value),__LINE__)
// Deliberately a simulated observation source. It tests the oracle and its
// mutations, not a native address, game controller or animation implementation.
struct FakeRun {
    enum class Motion {normal,gravity,orthogonal,reverse,sloped};
    ControllerScenario scenario;
    ControllerScenarioObservation observation;
    std::optional<PlayerCommand> pending;
    PlayerInput applied;
    std::uint64_t now{1000},attack_until{},guard_since{},guard_exit_until{},releases{},guard_entry_ms{};
    std::array<bool,13> visited{};
    float movement_roundtrip_error{};
    float intensity_error{};
    Motion motion{Motion::normal};
    bool provider{true},movement{true},attack{true},guard{true},guard_break{},guard_stuck{},writeback_only{},recovery_missing{};
    explicit FakeRun(Mode mode=Mode::strict,bool output_bound=true):scenario(19,1000,mode){
        observation.guest.valid=observation.guest.ready=observation.ownership_verified=true;
        observation.lifetime=7;observation.pose_setter_calls=1;
        observation.held_state_observed=true;
        observation.action_output_bound=output_bound;
        observation.guest.action_idle=true;
        observation.guest.position={10,0,10};observation.primary_position={10,0,12};
        observation.sample_sequence=1;observation.observed_ms=now;
        const auto first=scenario.tick(now,observation);
        if(mode==Mode::strict&&!output_bound)CHECK(!first.command&&first.neutralize);
        else CHECK(first.command&&!first.neutralize);
        pending=first.command;visited[static_cast<std::size_t>(scenario.report().phase)]=true;
    }
    void observe(std::uint64_t dt=50){
        now+=dt;
        if(provider&&pending){
            const auto& command=*pending;observation.consumer_observed=!writeback_only;
            observation.guest.consumed_input_generation=command.input_generation;
            observation.guest.consumed_frame_sequence=command.frame_sequence;
            observation.resolved_input=command.input;
            if(command.input.movement.intensity>0){
                observation.resolved_input.movement.direction[0]+=movement_roundtrip_error;
                observation.resolved_input.movement.intensity+=movement_roundtrip_error;
                observation.resolved_input.movement.intensity+=intensity_error;
            }
            const bool press=(command.input.held&button_mask(CommandButton::attack))&&!(applied.held&button_mask(CommandButton::attack));
            if(press&&attack)attack_until=now+1700;
            if((applied.held&button_mask(CommandButton::guard))&&!(command.input.held&button_mask(CommandButton::guard)))guard_exit_until=now+700;
            applied=command.input;
            if(movement){
                const auto amount=applied.movement.intensity*static_cast<float>(dt)*.002f;
                if(motion==Motion::gravity)observation.guest.position[1]-=amount;
                else if(motion==Motion::orthogonal)observation.guest.position[0]+=amount;
                else {
                    const float sign=motion==Motion::reverse?-1.f:1.f;
                    observation.guest.position[0]+=sign*applied.movement.direction[0]*amount;
                    observation.guest.position[2]+=sign*applied.movement.direction[1]*amount;
                    if(motion==Motion::sloped)observation.guest.position[1]-=amount*.25f;
                }
            }
            const bool held=(applied.held&button_mask(CommandButton::guard))!=0;
            if(held&&!guard_since)guard_since=now;
            observation.guest.guarding=guard&&held&&now-guard_since>=guard_entry_ms&&!(guard_break&&now-guard_since>600);
            if(guard_stuck&&guard_since)observation.guest.guarding=true;
        }else observation.consumer_observed=false;
        observation.guest.attacking=attack_until>now;
        observation.guest.action_idle=!observation.guest.attacking&&!observation.guest.guarding&&guard_exit_until<=now&&
            !(applied.held&button_mask(CommandButton::guard))&&!recovery_missing;
        ++observation.sample_sequence;observation.observed_ms=now;
    }
    ControllerScenarioStep apply(){
        const auto result=scenario.tick(now,observation);pending=result.command;
        visited[static_cast<std::size_t>(scenario.report().phase)]=true;
        if(result.neutralize){++releases;applied={};}
        return result;
    }
    void tick(std::uint64_t dt=50){observe(dt);apply();}
    void finish(){for(unsigned i=0;i<300&&!terminal(scenario.report().phase);++i)tick();CHECK(terminal(scenario.report().phase));}
    void until(Phase phase){for(unsigned i=0;i<300&&scenario.report().phase!=phase&&!terminal(scenario.report().phase);++i)tick();CHECK(scenario.report().phase==phase);}
    void failed(Fault fault){
        if(scenario.report().fault!=fault)throw std::runtime_error("Expected "+std::string(controller_scenario_fault_name(fault))+", got "+std::string(controller_scenario_fault_name(scenario.report().fault)));
        CHECK(scenario.report().phase==Phase::failed&&scenario.report().fault==fault&&!scenario.report().assertions_satisfied());
        const auto result=scenario.tick(now+1,{});CHECK(!result.command&&result.neutralize&&scenario.report().fault==fault);
    }
};
void complete_trace(){
    FakeRun run;run.finish();const auto& result=run.scenario.report();
    CHECK(result.assertions_satisfied()&&!result.action_result_unbound&&result.commands>80&&result.consumed_commands>80&&result.observations>80);
    CHECK(result.displacement>=.15f&&result.displacement<=ControllerScenario::max_displacement&&result.attack_observed&&result.guard_released&&result.guard_held_ms>=1500);
    CHECK(run.releases==1&&run.applied==PlayerInput{});
    CHECK(controller_scenario_phase_name(result.phase)=="complete"&&controller_scenario_fault_name(result.fault)=="none");
    // Exactly the same supplied policy evidence can have different scheduling;
    // durations use the explicit clock, not fixed tick counts.
    FakeRun jitter;for(unsigned i=0;i<400&&!terminal(jitter.scenario.report().phase);++i)jitter.tick(17+(i%4)*11);
    CHECK(jitter.scenario.report().assertions_satisfied());
    FakeRun root_motion;root_motion.until(Phase::attack_press);root_motion.observe();
    root_motion.observation.guest.position[2]+=1.86f;root_motion.apply();root_motion.finish();
    CHECK(root_motion.scenario.report().assertions_satisfied());
    CHECK(root_motion.scenario.report().displacement<1.f); // Walk evidence is separate from subsequent native attack root motion.
    FakeRun entry;entry.guard_entry_ms=500;entry.finish();
    CHECK(entry.scenario.report().assertions_satisfied()&&entry.scenario.report().guard_held_ms>=1500);
}
void diagnostic_output_binding(){
    CHECK(!ControllerScenarioObservation{}.action_output_bound);
    {FakeRun strict(Mode::strict,false);strict.failed(Fault::action_result_unbound);CHECK(strict.scenario.report().action_result_unbound);}
    {FakeRun strict;strict.until(Phase::guard_hold);strict.observe();strict.observation.action_output_bound=false;strict.apply();
     strict.failed(Fault::action_result_unbound);CHECK(!strict.scenario.report().attack_observed&&strict.scenario.report().guard_held_ms==0);}
    {FakeRun diagnostic(Mode::diagnostic_unbound,false);diagnostic.attack=false;diagnostic.guard=false;diagnostic.finish();
     const auto& result=diagnostic.scenario.report();
     CHECK(result.phase==Phase::inconclusive&&result.fault==Fault::action_result_unbound&&!result.assertions_satisfied()&&result.action_result_unbound);
     CHECK(result.commands>80&&result.consumed_commands>80&&result.displacement>=.15f);
     CHECK(!result.attack_observed&&!result.guard_released&&result.guard_held_ms==0&&diagnostic.releases==1);
     for(const auto phase:{Phase::attack_press,Phase::attack_release,Phase::guard_hold,Phase::guard_release,Phase::final_neutral})
         CHECK(diagnostic.visited[static_cast<std::size_t>(phase)]);
     const auto stopped=diagnostic.scenario.tick(diagnostic.now+50,diagnostic.observation);
     CHECK(stopped.neutralize&&!stopped.command&&diagnostic.scenario.report().phase==Phase::inconclusive);
     CHECK(controller_scenario_phase_name(result.phase)=="inconclusive"&&controller_scenario_fault_name(result.fault)=="action_result_unbound");}
    {FakeRun diagnostic(Mode::diagnostic_unbound,false);diagnostic.attack_until=diagnostic.now+10000;diagnostic.guard_stuck=true;diagnostic.finish();
     CHECK(diagnostic.scenario.report().phase==Phase::inconclusive&&!diagnostic.scenario.report().attack_observed&&!diagnostic.scenario.report().guard_released);}
    {FakeRun diagnostic(Mode::diagnostic_unbound,false);diagnostic.until(Phase::attack_press);diagnostic.observation.action_output_bound=true;diagnostic.finish();
     CHECK(diagnostic.scenario.report().phase==Phase::inconclusive&&!diagnostic.scenario.report().assertions_satisfied());
     CHECK(diagnostic.scenario.report().action_result_unbound&&!diagnostic.scenario.report().attack_observed&&diagnostic.scenario.report().guard_held_ms==0);}
    {FakeRun diagnostic(Mode::diagnostic_unbound,true);diagnostic.finish();
     CHECK(diagnostic.scenario.report().phase==Phase::inconclusive&&!diagnostic.scenario.report().assertions_satisfied());}
    {FakeRun diagnostic(Mode::diagnostic_unbound,false);diagnostic.provider=false;diagnostic.finish();diagnostic.failed(Fault::consumer_timeout);}
    {FakeRun diagnostic(Mode::diagnostic_unbound,false);diagnostic.movement=false;diagnostic.finish();diagnostic.failed(Fault::movement_missing);}
    {FakeRun diagnostic(Mode::diagnostic_unbound,false);diagnostic.observation.held_state_observed=false;diagnostic.finish();diagnostic.failed(Fault::invalid_consumption);}
    {FakeRun diagnostic(Mode::diagnostic_unbound,false);diagnostic.observe();diagnostic.observation.other_driver_commands=1;diagnostic.apply();diagnostic.failed(Fault::unsafe_driver);}
    {FakeRun diagnostic(Mode::diagnostic_unbound,false);diagnostic.observe();++diagnostic.observation.lifetime;diagnostic.apply();diagnostic.failed(Fault::scope_changed);}
    {FakeRun diagnostic(Mode::diagnostic_unbound,false);diagnostic.observe();diagnostic.observation.primary_position[0]+=.2f;diagnostic.apply();diagnostic.failed(Fault::primary_changed);}
    {FakeRun diagnostic(Mode::diagnostic_unbound,false);diagnostic.tick(251);diagnostic.failed(Fault::tick_gap);}
}
void independent_movement_roundtrip(){
    {FakeRun within;within.movement_roundtrip_error=ControllerScenario::movement_input_tolerance*.5f;within.finish();CHECK(within.scenario.report().assertions_satisfied());}
    {FakeRun outside;outside.movement_roundtrip_error=ControllerScenario::movement_input_tolerance*2;outside.finish();outside.failed(Fault::invalid_consumption);}
    {FakeRun buttons;buttons.until(Phase::attack_press);buttons.observe();buttons.observation.resolved_input.held=0;buttons.apply();buttons.failed(Fault::invalid_consumption);}
    {FakeRun invalid;invalid.until(Phase::moving);invalid.observe();invalid.observation.resolved_input.movement.intensity=std::numeric_limits<float>::quiet_NaN();invalid.apply();invalid.failed(Fault::invalid_consumption);}
    {FakeRun native_precision;native_precision.intensity_error=.3497791588306427f-.35f;native_precision.finish();CHECK(native_precision.scenario.report().assertions_satisfied());}
    {FakeRun wrong_intensity;wrong_intensity.intensity_error=-.001f;wrong_intensity.finish();wrong_intensity.failed(Fault::invalid_consumption);}
    for(const auto motion:{FakeRun::Motion::gravity,FakeRun::Motion::orthogonal,FakeRun::Motion::reverse}){
        FakeRun wrong_progress;wrong_progress.motion=motion;wrong_progress.finish();wrong_progress.failed(Fault::movement_missing);
        CHECK(wrong_progress.scenario.report().displacement==0);
    }
    {FakeRun slope;slope.motion=FakeRun::Motion::sloped;slope.finish();CHECK(slope.scenario.report().assertions_satisfied());}
    {FakeRun vertical_bound;vertical_bound.until(Phase::moving);vertical_bound.observe();vertical_bound.observation.guest.position[1]+=4.1f;
     vertical_bound.apply();vertical_bound.failed(Fault::movement_exceeded);}
}
void required_outcomes(){
    {FakeRun run;run.provider=false;run.finish();run.failed(Fault::consumer_timeout);}
    {FakeRun run;run.writeback_only=true;run.finish();run.failed(Fault::consumer_timeout);}
    {FakeRun run;run.movement=false;run.finish();run.failed(Fault::movement_missing);}
    {FakeRun run;run.attack=false;run.finish();run.failed(Fault::attack_missing);}
    {FakeRun run;run.guard=false;run.finish();run.failed(Fault::guard_missing);}
    {FakeRun run;run.guard_break=true;run.finish();run.failed(Fault::guard_not_held);}
    {FakeRun run;run.guard_stuck=true;run.finish();run.failed(Fault::guard_not_released);}
    {FakeRun run;run.observation.held_state_observed=false;run.finish();run.failed(Fault::invalid_consumption);}
    {FakeRun run;run.until(Phase::guard_hold);
     for(unsigned i=0;i<90&&!terminal(run.scenario.report().phase)&&run.scenario.report().phase==Phase::guard_hold;++i){
         run.tick(50);if(terminal(run.scenario.report().phase))break;
         // At 90 ms the old native sample remains fresh enough to keep the
         // test alive, but that observation gap must not earn guard duration.
         run.now+=40;run.apply();run.tick(50);
     }
     if(!terminal(run.scenario.report().phase))run.finish();
     run.failed(Fault::guard_not_held);CHECK(run.scenario.report().guard_held_ms<1500);}
    {FakeRun run;run.until(Phase::attack_release);run.attack_until=run.now+10000;run.finish();run.failed(Fault::attack_not_released);}
    {FakeRun run;run.until(Phase::attack_release);run.recovery_missing=true;run.finish();run.failed(Fault::attack_not_released);}
    {FakeRun run;run.until(Phase::guard_release);run.recovery_missing=true;run.finish();run.failed(Fault::guard_not_released);}
    {FakeRun run;run.guard_entry_ms=1100;run.finish();run.failed(Fault::guard_missing);}
    {FakeRun run;run.until(Phase::movement_release);run.observe();run.observation.guest.position[2]+=.3f;run.apply();
     // Establish a neutral sample, then reveal continued uncommanded motion.
     if(!terminal(run.scenario.report().phase)){run.observe();run.observation.guest.position[2]+=.3f;run.apply();run.finish();}
     run.failed(Fault::movement_not_released);}
    {FakeRun run;run.until(Phase::movement_release);run.tick();
     // The first valid neutral consumer establishes the anchor. Physical
     // samples remain authoritative during a brief consumer-observation gap.
     const auto anchor=run.observation.guest.position;
     run.observe();run.observation.consumer_observed=false;run.observation.guest.position[2]+=.3f;run.apply();
     run.observe();run.observation.consumer_observed=false;run.observation.guest.position=anchor;run.apply();
     run.tick();CHECK(run.observation.consumer_observed&&!terminal(run.scenario.report().phase));
     run.finish();run.failed(Fault::movement_not_released);}
}
void invariant_mutations(){
    for(unsigned mutation=0;mutation<5;++mutation){FakeRun run;run.observe();
        switch(mutation){
        case 0:++run.observation.pose_setter_calls;break;
        case 1:++run.observation.manual_hkb_enqueues;break;
        case 2:++run.observation.direct_graph_writes;break;
        case 3:++run.observation.other_driver_commands;break;
        default:++run.observation.primary_write_calls;break;
        }
        const auto result=run.apply();CHECK(result.neutralize&&!result.command);run.failed(Fault::unsafe_driver);
    }
    {FakeRun run;run.observe();run.observation.primary_position[0]+=.2f;run.apply();run.failed(Fault::primary_changed);}
    {FakeRun run;run.observe();run.observation.primary_input.held=button_mask(CommandButton::attack);run.apply();run.failed(Fault::primary_changed);}
    {FakeRun run;run.observe();run.observation.guest.position[0]+=4.01f;run.apply();run.failed(Fault::movement_exceeded);}
    {FakeRun run;run.observe();++run.observation.lifetime;run.apply();run.failed(Fault::scope_changed);}
    {FakeRun run;run.observe();run.observation.guest.ready=false;run.apply();run.failed(Fault::readiness_lost);}
    {FakeRun run;run.observe();run.observation.ownership_verified=false;run.apply();run.failed(Fault::readiness_lost);}
    {FakeRun run;run.observe();run.observation.guest.position[0]=std::numeric_limits<float>::quiet_NaN();run.apply();run.failed(Fault::invalid_observation);}
    {FakeRun run;run.observe();run.observation.guest.attacking=true;run.apply();run.failed(Fault::not_neutral);}
}
void clock_and_consumption_mutations(){
    {ControllerScenario invalid(0,0);CHECK(invalid.tick(0,{}).neutralize&&invalid.report().fault==Fault::invalid_generation);}
    {ControllerScenario waiting(1,0);CHECK(!waiting.tick(29999,{}).command);waiting.tick(30000,{});CHECK(waiting.report().fault==Fault::preparation_timeout);}
    {FakeRun run;run.scenario.tick(999,run.observation);run.failed(Fault::clock_regressed);}
    {FakeRun run;run.tick(251);run.failed(Fault::tick_gap);}
    {FakeRun run;run.observe();run.observation.observed_ms=run.now+1;run.apply();run.failed(Fault::invalid_observation);}
    {FakeRun run;run.observe(150);run.observation.observed_ms=run.now-101;run.apply();run.failed(Fault::stale_observation);}
    {FakeRun run;run.observe();++run.observation.guest.consumed_input_generation;run.apply();run.failed(Fault::invalid_consumption);}
    {FakeRun run;run.observe();++run.observation.guest.consumed_frame_sequence;run.apply();run.failed(Fault::invalid_consumption);}
    {FakeRun run;run.observe();run.observation.resolved_input.held=button_mask(CommandButton::guard);run.apply();run.failed(Fault::invalid_consumption);}
    {FakeRun run;run.tick();run.observe();run.observation.guest.consumed_frame_sequence=1;run.apply();
     run.observe();run.observation.guest.consumed_frame_sequence=0;run.apply();run.failed(Fault::invalid_consumption);}
    {FakeRun run;run.tick();run.observe();run.observation.sample_sequence=1;run.apply();run.failed(Fault::invalid_observation);}
    {FakeRun run;const auto original=run.observation;
     for(unsigned i=0;i<3&&!terminal(run.scenario.report().phase);++i){run.now+=50;run.observation=original;run.apply();}
     run.failed(Fault::stale_observation);}
    {FakeRun run;run.scenario.request_stop();run.tick();CHECK(run.scenario.report().phase==Phase::stopped&&run.releases==1&&!run.scenario.report().assertions_satisfied());}
}
}
int main(){try{complete_trace();diagnostic_output_binding();independent_movement_roundtrip();required_outcomes();invariant_mutations();clock_and_consumption_mutations();
    std::cout<<"PASS offline controller scenario/oracle, bounded independent commands and deliberate failure mutations; no native game binding tested\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
