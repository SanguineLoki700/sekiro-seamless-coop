#include "skcoop/actors/native_command_mailbox.hpp"
#include "skcoop/actors/native_input_match.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace skcoop::actors {
NativeCommandMailbox::NativeCommandMailbox(std::uint64_t generation,Clock clock,NativeMovementOnset onset)
    :latch_(generation),clock_(clock),movement_onset_(onset),generation_(generation){
    std::lock_guard lock(mutex_);std::uint64_t now{};
    if(!clock_){fail_locked(NativeMailboxFault::invalid_clock);return;}
    if(!generation){fail_locked(NativeMailboxFault::latch_failure);return;}
    now_locked(now);
}
bool NativeCommandMailbox::advance_epoch_locked(){
    if(epoch_counter_==std::numeric_limits<std::uint64_t>::max()){
        record_failure_locked(NativeMailboxFault::epoch_exhausted);failed_=true;return false;
    }
    ++epoch_counter_;return true;
}
void NativeCommandMailbox::invalidate_locked(){
    advance_epoch_locked(); // Record exhaustion before clearing diagnostic values.
    association_=ready_=false;
    // Invalidating a source association does not run the native Pad. Only an
    // actual neutral begin_update may rearm its previous-stick onset allowance.
    onset_frame_=turn_intent_verified_=false;
    resolver_gate_=resolver_speed_=resolver_angle_=attack_seen_=guard_seen_=false;
    resolved_movement_={};resolved_held_=0;
    resolver_at_=attack_at_=guard_at_=completed_at_=0;
}
void NativeCommandMailbox::record_failure_locked(NativeMailboxFault kind,std::optional<PlayerInput> observed,
                                                 CommandButton button,float duration){
    if(failure_.kind!=NativeMailboxFault::none)return;
    failure_={kind,latch_.fault(),frame_.epoch,frame_.input,
        observed.value_or(PlayerInput{resolved_movement_,resolved_held_}),button,duration};
}
void NativeCommandMailbox::fail_locked(NativeMailboxFault kind,std::optional<PlayerInput> observed,
                                      CommandButton button,float duration){
    record_failure_locked(kind,observed,button,duration);failed_=true;latch_.suspend();invalidate_locked();
}
bool NativeCommandMailbox::now_locked(std::uint64_t& now){
    if(!clock_){fail_locked(NativeMailboxFault::invalid_clock);return false;}
    now=clock_();
    if(clock_seen_&&now<clock_at_){fail_locked(NativeMailboxFault::clock_regressed);return false;}
    clock_seen_=true;clock_at_=now;return true;
}
bool NativeCommandMailbox::matches_locked(std::uint64_t epoch)const {
    return !failed_&&association_&&ready_&&!in_flight_&&frame_.accepted&&epoch&&epoch==frame_.epoch;
}
bool NativeCommandMailbox::fresh_locked(std::uint64_t now)const {
    return now>=issued_at_&&now-issued_at_<=observation_expiry_ms;
}
NativeInputOffer NativeCommandMailbox::offer(const CommandDelivery& delivery){
    std::lock_guard lock(mutex_);std::uint64_t now{};
    if(failed_||!now_locked(now))return NativeInputOffer::failed;
    const auto result=latch_.offer(delivery,now);
    if(result==NativeInputOffer::failed){fail_locked(NativeMailboxFault::latch_failure);return result;}
    if(result==NativeInputOffer::needs_baseline||
       (result==NativeInputOffer::accepted&&(delivery.baseline||delivery.neutralize)))invalidate_locked();
    return result;
}
NativeInputFrame NativeCommandMailbox::begin_update(){
    std::lock_guard lock(mutex_);std::uint64_t now{};
    if(failed_||!now_locked(now))return {};
    if(in_flight_){fail_locked(NativeMailboxFault::overlapping_update);return {};}
    if(!advance_epoch_locked()){latch_.suspend();association_=ready_=false;return {};}
    frame_={};frame_.epoch=epoch_counter_;frame_.input=latch_.begin_update(now);
    if(latch_.fault()!=NativeInputFault::none||!valid(frame_.input)){fail_locked(NativeMailboxFault::latch_failure);return {};}
    // A baseline-pending latch still supplies a valid neutral Pad update. Its
    // original must be finished/abandoned normally, but it has no source claim.
    frame_.accepted=true;frame_.source=latch_.source_identity();
    const bool moving=frame_.input.movement.intensity>0;
    const bool starting=moving&&!previous_moving_;
    onset_frame_=(movement_onset_==NativeMovementOnset::defer_first_zero||
        movement_onset_==NativeMovementOnset::defer_verified_steering)&&starting;
    if(movement_onset_==NativeMovementOnset::defer_verified_steering){
        if(!moving){steering_window_open_=false;steering_updates_=0;}
        else if(starting){steering_window_open_=true;steering_updates_=1;steering_onset_at_=now;}
        else if(steering_updates_<4)++steering_updates_;
    }
    previous_moving_=moving;
    if(frame_.source&&(frame_.source->input_generation!=generation_||!frame_.source->frame_sequence)){
        fail_locked(NativeMailboxFault::invalid_source);return {};
    }
    issued_at_=now;completed_at_=0;flight_epoch_=frame_.epoch;in_flight_=association_=true;ready_=false;
    resolver_gate_=resolver_speed_=resolver_angle_=attack_seen_=guard_seen_=false;
    turn_intent_verified_=false;
    resolved_movement_={};resolved_held_=0;
    resolver_at_=attack_at_=guard_at_=0;return frame_;
}
bool NativeCommandMailbox::finish_update(std::uint64_t epoch,std::optional<NativeTurnIntent> turn_intent){
    std::lock_guard lock(mutex_);
    if(!epoch||!in_flight_||epoch!=flight_epoch_)return false;
    in_flight_=false;flight_epoch_=0;
    if(failed_||!association_||epoch!=frame_.epoch)return false;
    std::uint64_t now{};if(!now_locked(now))return false;
    if(!fresh_locked(now)){association_=ready_=false;return false;}
    if(turn_intent){
        observe_turn_intent_locked(turn_intent->movement,turn_intent->native_turn_observed);
        if(failed_)return false;
    }
    completed_at_=now;ready_=true;return true;
}
void NativeCommandMailbox::abandon_update(std::uint64_t epoch){
    std::lock_guard lock(mutex_);
    if(!epoch||!in_flight_||epoch!=flight_epoch_)return;
    in_flight_=false;flight_epoch_=0;fail_locked(NativeMailboxFault::abandoned_update);
}
std::uint64_t NativeCommandMailbox::current_epoch(){
    std::lock_guard lock(mutex_);
    if(failed_||!association_||!ready_||in_flight_||!frame_.accepted)return 0;
    std::uint64_t now{};if(!now_locked(now)||!fresh_locked(now)||now-completed_at_>observation_expiry_ms)return 0;
    return frame_.epoch;
}
bool NativeCommandMailbox::observe_resolver(std::uint64_t epoch,MovementIntent resolved,
                                           bool gate,bool speedWrite,bool angleWrite){
    std::lock_guard lock(mutex_);if(!matches_locked(epoch))return false;
    std::uint64_t now{};if(!now_locked(now)||!fresh_locked(now))return false;
    if(!gate||!speedWrite||!angleWrite){
        resolver_gate_=resolver_speed_=resolver_angle_=false;resolver_at_=0;resolved_movement_={};return false;
    }
    const PlayerInput observed{resolved,resolved_held_};
    if(!valid(PlayerInput{resolved,0})){fail_locked(NativeMailboxFault::invalid_movement,observed);return false;}
    if(!native_movement_matches(frame_.input.movement,resolved)){
        // The supported Pad's previous-stick filter can produce exactly zero
        // on its first moving update. Keep the actual zero uncredited; neither
        // submitted input nor a prior epoch may stand in for consumption.
        const bool steering_policy=movement_onset_==NativeMovementOnset::defer_verified_steering;
        const bool first_zero=onset_frame_&&resolved==MovementIntent{}&&(!steering_policy||steering_window_open_);
        const bool verified_onset=steering_policy&&steering_window_open_&&steering_updates_>=1&&steering_updates_<=3&&
            now>=steering_onset_at_&&now-steering_onset_at_<=observation_expiry_ms&&turn_intent_verified_;
        if(first_zero||verified_onset){
            resolver_gate_=resolver_speed_=resolver_angle_=false;resolver_at_=0;resolved_movement_={};return false;
        }
        fail_locked(NativeMailboxFault::movement_mismatch,observed);return false;
    }
    if(movement_onset_==NativeMovementOnset::defer_verified_steering)steering_window_open_=false;
    resolver_gate_=resolver_speed_=resolver_angle_=true;resolved_movement_=resolved;resolver_at_=now;return true;
}
bool NativeCommandMailbox::observe_turn_intent(std::uint64_t epoch,MovementIntent native_intent,bool native_turn_observed){
    std::lock_guard lock(mutex_);if(!matches_locked(epoch))return false;
    std::uint64_t now{};if(!now_locked(now)||!fresh_locked(now))return false;
    return observe_turn_intent_locked(native_intent,native_turn_observed);
}
bool NativeCommandMailbox::observe_turn_intent_locked(MovementIntent native_intent,bool native_turn_observed){
    turn_intent_verified_=false;
    if(!native_turn_observed)return false;
    const PlayerInput observed{native_intent,resolved_held_};
    if(!valid(PlayerInput{native_intent,0})||native_intent.intensity==0){fail_locked(NativeMailboxFault::invalid_movement,observed);return false;}
    if(!native_movement_matches(frame_.input.movement,native_intent)){fail_locked(NativeMailboxFault::movement_mismatch,observed);return false;}
    turn_intent_verified_=true;return true;
}
bool NativeCommandMailbox::observe_held(std::uint64_t epoch,CommandButton button,float duration){
    std::lock_guard lock(mutex_);if(!matches_locked(epoch))return false;
    std::uint64_t now{};if(!now_locked(now)||!fresh_locked(now))return false;
    PlayerInput observed{resolved_movement_,resolved_held_};
    if((button!=CommandButton::attack&&button!=CommandButton::guard)||!std::isfinite(duration)||duration<0){
        fail_locked(NativeMailboxFault::invalid_held,observed,button,duration);return false;
    }
    const auto bit=button_mask(button);if(duration>0)observed.held|=bit;else observed.held&=~bit;
    if((duration>0)!=((frame_.input.held&bit)!=0)){
        fail_locked(NativeMailboxFault::held_mismatch,observed,button,duration);return false;
    }
    if(button==CommandButton::attack){attack_seen_=true;attack_at_=now;}
    else {guard_seen_=true;guard_at_=now;}
    resolved_held_=observed.held;
    return true;
}
std::optional<NativeConsumedInput> NativeCommandMailbox::consumed(){
    std::lock_guard lock(mutex_);
    if(failed_||!association_||!ready_||in_flight_||!frame_.accepted||!frame_.source||
       !resolver_gate_||!resolver_speed_||!resolver_angle_||!attack_seen_||!guard_seen_)return {};
    constexpr auto observed_buttons=button_mask(CommandButton::attack)|button_mask(CommandButton::guard);
    if(frame_.input.held&~observed_buttons)return {}; // No evidence for other buttons.
    std::uint64_t now{};if(!now_locked(now)||!fresh_locked(now)||
       now-completed_at_>observation_expiry_ms||now-resolver_at_>observation_expiry_ms||
       now-attack_at_>observation_expiry_ms||now-guard_at_>observation_expiry_ms)return {};
    const auto observed=std::max({completed_at_,resolver_at_,attack_at_,guard_at_});
    return NativeConsumedInput{*frame_.source,frame_.epoch,PlayerInput{resolved_movement_,resolved_held_},observed,true};
}
void NativeCommandMailbox::suspend(){
    std::lock_guard lock(mutex_);std::uint64_t now{};if(!now_locked(now))return;
    latch_.suspend();invalidate_locked();
}
bool NativeCommandMailbox::reset_generation(std::uint64_t generation){
    std::lock_guard lock(mutex_);
    if(!generation||generation<=generation_||in_flight_||epoch_counter_==std::numeric_limits<std::uint64_t>::max())return false;
    std::uint64_t now{};if(!now_locked(now))return false;
    generation_=generation;latch_=NativeInputLatch(generation);failed_=false;invalidate_locked();frame_={};return !failed_;
}
bool NativeCommandMailbox::failed()const {std::lock_guard lock(mutex_);return failed_;}
NativeMailboxFailure NativeCommandMailbox::failure()const {std::lock_guard lock(mutex_);return failure_;}
}
