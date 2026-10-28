#include "skcoop/actors/native_input_latch.hpp"
#include <algorithm>

namespace skcoop::actors {
namespace {
bool valid_delivery(const CommandDelivery& delivery)noexcept {
    const auto& state=delivery.state;
    if(!state.input_generation||!state.frame_sequence||!valid(state.input)||
       delivery.transition_count>delivery.transitions.size()||
       (delivery.neutralize&&delivery.baseline)||
       ((delivery.neutralize||delivery.baseline)&&delivery.transition_count)||
       (delivery.neutralize&&state.input!=PlayerInput{}))return false;
    std::uint64_t serial{},time{};
    for(std::size_t i=0;i<delivery.transitions.size();++i){
        const auto& edge=delivery.transitions[i];
        if(i>=delivery.transition_count){if(edge!=ButtonTransition{})return false;continue;}
        if(!edge.serial||!valid(edge.button)||edge.source_ms>state.source_ms||
           (i&&(edge.serial<=serial||edge.serial-serial!=1||edge.source_ms<time)))return false;
        serial=edge.serial;time=edge.source_ms;
    }
    return true;
}
bool same_delivery(const CommandDelivery& a,const CommandDelivery& b)noexcept {
    return a.state==b.state&&a.transition_count==b.transition_count&&a.transitions==b.transitions&&
        a.baseline==b.baseline&&a.neutralize==b.neutralize;
}
}
NativeInputLatch::NativeInputLatch(std::uint64_t generation)noexcept:generation_(generation) {
    if(!generation)fault_=NativeInputFault::invalid;
}
void NativeInputLatch::suspend()noexcept {
    active_=fresh_baseline_=false;baseline_required_=true;
    count_=0;pending_.fill({});held_=blocked_=0;output_={};output_source_.reset();
}
NativeInputOffer NativeInputLatch::fail(NativeInputFault fault)noexcept {
    if(fault_==NativeInputFault::none)fault_=fault;
    suspend();return NativeInputOffer::failed;
}
NativeInputOffer NativeInputLatch::offer(const CommandDelivery& delivery,std::uint64_t arrival)noexcept {
    if(fault_!=NativeInputFault::none)return NativeInputOffer::failed;
    if(!valid_delivery(delivery))return fail(NativeInputFault::invalid);
    const auto& state=delivery.state;
    if(state.input_generation!=generation_)return fail(NativeInputFault::generation_changed);
    if(known_){
        if(state.frame_sequence<known_->frame_sequence)return NativeInputOffer::stale;
        if(last_delivery_&&same_delivery(*last_delivery_,delivery)&&
           !(delivery.baseline&&baseline_required_))return NativeInputOffer::stale;
        if(state.frame_sequence==known_->frame_sequence){
            if(state.source_ms!=known_->source_ms||(!delivery.neutralize&&known_input_&&state.input!=known_->input))
                return fail(NativeInputFault::invalid);
            if(!delivery.baseline&&!delivery.neutralize){
                if(baseline_required_)return NativeInputOffer::needs_baseline;
                return fail(NativeInputFault::invalid);
            }
        }
        if(arrival<arrival_||state.source_ms<known_->source_ms)return fail(NativeInputFault::clock_regressed);
    }
    if(delivery.neutralize){
        if(!known_||state.frame_sequence>known_->frame_sequence){known_=state;known_input_=false;arrival_=arrival;}
        last_delivery_=delivery;suspend();return NativeInputOffer::accepted;
    }
    const bool same_frame=known_&&state.frame_sequence==known_->frame_sequence;
    // A repeated authoritative frame never receives a new freshness lease.
    if(same_frame&&arrival-arrival_>command_expiry_ms){suspend();return NativeInputOffer::needs_baseline;}
    if(active_&&((!same_frame&&arrival-arrival_>command_expiry_ms)||
       (have_update_&&arrival>=updated_at_&&arrival-updated_at_>command_expiry_ms)))suspend();
    if(delivery.baseline){
        count_=0;pending_.fill({});held_=0;blocked_=state.input.held;output_={};output_source_.reset();
        baseline_required_=false;active_=fresh_baseline_=true;
    }else {
        if(baseline_required_)return NativeInputOffer::needs_baseline;
        auto pending=pending_;auto count=count_;auto recent=recent_;auto recent_count=recent_count_;auto serial=last_serial_;
        for(std::size_t i=0;i<delivery.transition_count;++i){
            const auto& edge=delivery.transitions[i];
            if(edge.serial<=serial){
                const auto old=std::find_if(recent.begin(),recent.begin()+static_cast<std::ptrdiff_t>(recent_count),
                    [&](const auto& previous){return previous.serial==edge.serial;});
                if(old!=recent.begin()+static_cast<std::ptrdiff_t>(recent_count)&&*old!=edge)return fail(NativeInputFault::invalid);
                continue; // An already accepted edge can never be replayed.
            }
            if(state.source_ms-edge.source_ms>command_expiry_ms){suspend();return NativeInputOffer::needs_baseline;}
            if(count==pending.size())return fail(NativeInputFault::overflow);
            pending[count++]={edge,arrival};serial=edge.serial;
            if(recent_count==recent.size()){std::move(recent.begin()+1,recent.end(),recent.begin());--recent_count;}
            recent[recent_count++]=edge;
        }
        pending_=pending;count_=count;recent_=recent;recent_count_=recent_count;last_serial_=serial;
        output_source_.reset();
    }
    known_=state;known_input_=true;if(!same_frame)arrival_=arrival;last_delivery_=delivery;
    return NativeInputOffer::accepted;
}
PlayerInput NativeInputLatch::begin_update(std::uint64_t now)noexcept {
    if(fault_!=NativeInputFault::none)return {};
    if((have_update_&&now<updated_at_)||(active_&&now<arrival_)){fail(NativeInputFault::clock_regressed);return {};}
    if(active_&&(now-arrival_>command_expiry_ms||
       (have_update_&&!fresh_baseline_&&now-updated_at_>command_expiry_ms)||
       (count_&&now-pending_[0].arrival_ms>command_expiry_ms)))suspend();
    if(have_update_&&now==updated_at_)return output_;
    have_update_=true;updated_at_=now;fresh_baseline_=false;
    if(!active_||baseline_required_||!known_)return {};
    output_.movement=known_->input.movement;output_source_.reset();
    if(count_){
        const auto edge=pending_[0].event;
        std::move(pending_.begin()+1,pending_.begin()+static_cast<std::ptrdiff_t>(count_),pending_.begin());
        pending_[--count_]={};const auto bit=button_mask(edge.button);
        if(edge.pressed){if(!(blocked_&bit))held_|=bit;}
        else {held_&=~bit;blocked_&=~bit;}
        // Do not overwrite this pulse with the batch's final held state in the
        // same update. A coalesced press/release must occupy separate updates.
    }else {
        held_&=known_->input.held; // Safe release when no pulse remains.
        blocked_|=known_->input.held&~held_; // Never infer an unseen press.
    }
    output_.held=held_&~blocked_;
    if(!count_&&output_==known_->input)output_source_=NativeInputSource{generation_,known_->frame_sequence};
    return output_;
}
}
