#include "skcoop/actors/player_command.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace skcoop::actors {
namespace {
bool accepted_mask(ButtonMask value)noexcept{return (value&~command_button_mask)==0;}
bool overlaps_match(const PlayerCommand& old,const PlayerCommand& next)noexcept {
    for(std::size_t i=0;i<old.transition_count;++i)
        for(std::size_t j=0;j<next.transition_count;++j)
            if(old.transitions[i].serial==next.transitions[j].serial&&old.transitions[i]!=next.transitions[j])return false;
    return true;
}
bool complete_suffix(const PlayerCommand& old,const PlayerCommand& next)noexcept {
    return next.transition_latest==old.transition_latest||next.transitions[0].serial<=old.transition_latest+1;
}
bool consistent_held(const PlayerCommand& old,const PlayerCommand& next)noexcept {
    if(!complete_suffix(old,next))return true; // Explicit baselines may discard an unknown prefix.
    auto held=old.input.held;
    for(std::size_t i=0;i<next.transition_count;++i){
        const auto& event=next.transitions[i];if(event.serial<=old.transition_latest)continue;
        const auto bit=button_mask(event.button);
        if(((held&bit)!=0)==event.pressed)return false;
        if(event.pressed)held|=bit;else held&=~bit;
    }
    return held==next.input.held;
}
PlayerCommandState state(const PlayerCommand& command)noexcept {
    return {command.input_generation,command.frame_sequence,command.source_ms,command.input};
}
}
bool valid(CommandButton button)noexcept {
    const auto bit=button_mask(button);return bit&&(bit&~command_button_mask)==0&&(bit&(bit-1))==0;
}
bool valid(const PlayerInput& input,ButtonMask accepted)noexcept {
    if(!accepted_mask(accepted)||(input.held&~accepted))return false;
    const auto& move=input.movement;
    if(!std::isfinite(move.intensity)||move.intensity<0||move.intensity>1)return false;
    for(const auto value:move.direction)if(!std::isfinite(value)||std::abs(value)>1)return false;
    if(move.intensity==0)return move.direction==std::array<float,2>{};
    return std::abs(std::hypot(move.direction[0],move.direction[1])-1.f)<=.001f;
}
bool valid(const PlayerCommand& command,ButtonMask accepted)noexcept {
    if(!command.input_generation||!command.frame_sequence||!valid(command.input,accepted)||
       command.transition_count!=std::min<std::uint64_t>(command.transition_latest,command_history_capacity))return false;
    ButtonMask known{},held{};
    if(command.transition_latest==command.transition_count)known=accepted;
    std::uint64_t previous_ms{};
    for(std::size_t i=0;i<command.transitions.size();++i){
        const auto& event=command.transitions[i];
        if(i>=command.transition_count){if(event!=ButtonTransition{})return false;continue;}
        if(!valid(event.button)||(button_mask(event.button)&~accepted)||
           event.serial!=command.transition_latest-command.transition_count+1+i||
           event.source_ms>command.source_ms||(i&&event.source_ms<previous_ms))return false;
        const auto bit=button_mask(event.button);
        if((known&bit)&&(((held&bit)!=0)==event.pressed))return false;
        known|=bit;if(event.pressed)held|=bit;else held&=~bit;
        previous_ms=event.source_ms;
    }
    return (command.input.held&known)==held;
}
bool valid(const CommandScope& scope)noexcept {
    const auto nonzero=[](const auto& value){return std::any_of(value.begin(),value.end(),[](auto byte){return byte!=0;});};
    return nonzero(scope.session)&&nonzero(scope.connection)&&scope.player&&scope.input_generation;
}
PlayerCommandCapture::PlayerCommandCapture(std::uint64_t generation,ButtonMask accepted)noexcept:accepted_(accepted) {
    command_.input_generation=generation;
    if(!generation||!accepted_mask(accepted))fault_=CommandFault::invalid;
}
bool PlayerCommandCapture::fail(CommandFault fault)noexcept{fault_=fault;return false;}
bool PlayerCommandCapture::capture(const PlayerInput& input,std::uint64_t ms)noexcept {
    if(fault_!=CommandFault::none)return false;
    if(!valid(input,accepted_))return fail(CommandFault::invalid);
    if(ms<command_.source_ms)return fail(CommandFault::clock_regressed);
    if(command_.frame_sequence==std::numeric_limits<std::uint64_t>::max())return fail(CommandFault::sequence_exhausted);
    auto next=command_;const auto changed=input.held^next.input.held;
    for(ButtonMask bit=1;bit<=button_mask(CommandButton::item);bit<<=1){
        if(!(changed&bit))continue;
        if(next.transition_latest==std::numeric_limits<std::uint64_t>::max())return fail(CommandFault::sequence_exhausted);
        if(next.transition_count==command_history_capacity){
            if(next.transitions[0].serial>offered_)return fail(CommandFault::overflow);
            std::move(next.transitions.begin()+1,next.transitions.end(),next.transitions.begin());--next.transition_count;
        }
        next.transitions[next.transition_count++]={++next.transition_latest,ms,static_cast<CommandButton>(bit),(input.held&bit)!=0};
    }
    next.input=input;next.source_ms=ms;++next.frame_sequence;command_=next;return true;
}
std::optional<PlayerCommand> PlayerCommandCapture::latest()const noexcept {
    if(fault_!=CommandFault::none||!command_.frame_sequence)return {};return command_;
}
bool PlayerCommandCapture::offered(std::uint64_t serial)noexcept {
    if(fault_!=CommandFault::none)return false;
    if(serial<offered_||serial>command_.transition_latest)return fail(CommandFault::invalid);
    offered_=serial;return true;
}
PlayerCommandReceiver::PlayerCommandReceiver(ButtonMask accepted)noexcept:accepted_(accepted) {
    if(!accepted_mask(accepted)){fault_=CommandFault::invalid;neutral_pending_=true;}
}
void PlayerCommandReceiver::suspend()noexcept {
    if(active_)neutral_pending_=true;
    active_=delivery_pending_=baseline_pending_=false;count_=0;pending_.fill({});
}
CommandReceive PlayerCommandReceiver::fail(CommandFault fault)noexcept {
    if(fault_==CommandFault::none){
        fault_=fault;suspend();
        // The adapter may already own a native binding before the first
        // accepted command. Every first fault requests its neutralization;
        // the owner still must verify the binding before performing writes.
        neutral_pending_=true;
    }
    return CommandReceive::failed;
}
CommandReceive PlayerCommandReceiver::observe(const CommandScope& scope,const PlayerCommand& command,
                                              std::uint64_t arrival,bool following)noexcept {
    if(fault_!=CommandFault::none)return CommandReceive::failed;
    if(!valid(scope)||!valid(command,accepted_)||scope.input_generation!=command.input_generation)return fail(CommandFault::invalid);
    bool expired=false;
    if(bound_){
        if(scope!=scope_)return fail(CommandFault::scope_changed);
        if(command.frame_sequence<known_.frame_sequence)return CommandReceive::stale;
        if(command.frame_sequence==known_.frame_sequence){
            if(command!=known_)return fail(CommandFault::invalid);
            // A local pause/resume can happen between incoming frames. It may
            // baseline the same command but cannot refresh its original lease.
            if(!following&&active_){suspend();return CommandReceive::accepted;}
            if(following&&!active_){active_=delivery_pending_=baseline_pending_=true;return CommandReceive::accepted;}
            return CommandReceive::stale;
        }
        if(arrival<arrival_||command.source_ms<known_.source_ms)return fail(CommandFault::clock_regressed);
        if(command.transition_latest<known_.transition_latest||!overlaps_match(known_,command)||!consistent_held(known_,command))return fail(CommandFault::invalid);
        expired=arrival-arrival_>command_expiry_ms;
    }
    const bool baseline=!bound_||!following||!active_||expired;
    if(bound_&&!complete_suffix(known_,command)&&!baseline)return fail(CommandFault::gap);
    if(baseline){
        if(active_&&(!following||expired))neutral_pending_=true;
        count_=0;pending_.fill({});baseline_pending_=following;
    }else {
        auto next=pending_;auto count=count_;
        for(std::size_t i=0;i<command.transition_count;++i){
            const auto& event=command.transitions[i];if(event.serial<=known_.transition_latest)continue;
            if(command.source_ms-event.source_ms>command_expiry_ms)continue;
            if(count==next.size())return fail(CommandFault::overflow);
            next[count++]={event,arrival};
        }
        pending_=next;count_=count;
    }
    scope_=scope;known_=command;arrival_=arrival;bound_=true;active_=following;delivery_pending_=following;
    return CommandReceive::accepted;
}
std::optional<CommandDelivery> PlayerCommandReceiver::take(std::uint64_t now)noexcept {
    if(fault_==CommandFault::none&&(now<checked_at_||(bound_&&now<arrival_)))fail(CommandFault::clock_regressed);
    checked_at_=std::max(checked_at_,now);
    if(active_&&now-arrival_>command_expiry_ms)suspend();
    if(neutral_pending_){
        neutral_pending_=false;CommandDelivery result;result.state=state(known_);result.state.input={};result.neutralize=true;return result;
    }
    if(fault_!=CommandFault::none||!active_||!delivery_pending_)return {};
    CommandDelivery result;result.state=state(known_);result.baseline=baseline_pending_;
    for(std::size_t i=0;i<count_;++i)
        if(now-pending_[i].arrival_ms<=command_expiry_ms)result.transitions[result.transition_count++]=pending_[i].transition;
    count_=0;pending_.fill({});delivery_pending_=baseline_pending_=false;return result;
}
}
