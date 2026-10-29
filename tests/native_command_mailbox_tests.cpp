#include "skcoop/actors/native_command_mailbox.hpp"
#include <atomic>
#include <barrier>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
using namespace skcoop::actors;
namespace {
void check(bool yes,int line){if(!yes)throw std::runtime_error("Native command mailbox invariant at line "+std::to_string(line));}
#define require(value) check((value),__LINE__)
std::atomic<std::uint64_t> ticks{};
std::uint64_t clock_read()noexcept{return ticks.load(std::memory_order_relaxed);}
std::uint64_t clock_progress()noexcept{return ticks.fetch_add(1,std::memory_order_relaxed);}
CommandDelivery command(std::uint64_t generation=7,std::uint64_t frame=1){CommandDelivery value;value.state.input_generation=generation;value.state.frame_sequence=frame;value.state.source_ms=frame-1;return value;}
CommandDelivery baseline(std::uint64_t generation=7,std::uint64_t frame=1){auto value=command(generation,frame);value.baseline=true;return value;}
NativeInputFrame begin(NativeCommandMailbox& box){const auto frame=box.begin_update();require(frame.epoch&&frame.accepted);return frame;}
bool evidence(NativeCommandMailbox& box,const NativeInputFrame& frame){
    return box.observe_resolver(frame.epoch,frame.input.movement,true,true,true)&&
        box.observe_held(frame.epoch,CommandButton::attack,(frame.input.held&button_mask(CommandButton::attack))?.1f:0.f)&&
        box.observe_held(frame.epoch,CommandButton::guard,(frame.input.held&button_mask(CommandButton::guard))?.1f:0.f);
}
void completion_and_required_evidence(){
    ticks=0;NativeCommandMailbox box(7,&clock_read);require(box.offer(baseline())==NativeInputOffer::accepted);
    const auto frame=begin(box);require(frame.source&&box.current_epoch()==0&&!box.consumed());
    require(!box.observe_resolver(frame.epoch,{},true,true,true));
    require(!box.observe_held(frame.epoch,CommandButton::attack,0)&&!box.failed());
    require(box.finish_update(frame.epoch)&&box.current_epoch()==frame.epoch&&!box.consumed());
    require(!box.observe_resolver(frame.epoch,{},true,false,false));
    require(!box.observe_resolver(frame.epoch,{},false,true,false));
    require(!box.observe_resolver(frame.epoch,{},false,false,true));
    require(box.observe_held(frame.epoch,CommandButton::attack,0));
    require(!box.consumed());require(box.observe_resolver(frame.epoch,{},true,true,true));
    require(box.observe_held(frame.epoch,CommandButton::guard,0));const auto consumed=box.consumed();
    require(consumed&&consumed->source==*frame.source&&consumed->epoch==frame.epoch&&consumed->input==frame.input&&consumed->held_state_observed);
    require(!box.finish_update(frame.epoch));require(box.consumed().has_value());
    require(!box.observe_resolver(frame.epoch,{},false,true,true)&&!box.consumed());
    require(box.observe_resolver(frame.epoch,{},true,true,true)&&box.consumed().has_value());
    require(!box.observe_held(frame.epoch+1,CommandButton::guard,0)&&!box.failed());
    // Full provider writeback and even all resolver evidence cannot credit an
    // original Pad that has not actually completed.
    ticks=1;require(box.offer(command(7,2))==NativeInputOffer::accepted);const auto next=begin(box);
    require(!evidence(box,next)&&!box.consumed()&&box.current_epoch()==0);
    require(box.finish_update(next.epoch)&&!box.consumed());
    require(evidence(box,next)&&box.consumed()->source.frame_sequence==2);
    require(!box.observe_resolver(frame.epoch,{},true,true,true)&&!box.failed());

    ticks=0;NativeCommandMailbox no_source(7,&clock_read);auto held=baseline();held.state.input.held=button_mask(CommandButton::guard);
    require(no_source.offer(held)==NativeInputOffer::accepted);const auto suppressed=begin(no_source);
    require(!suppressed.source&&suppressed.input.held==0);
    require(no_source.finish_update(suppressed.epoch)&&evidence(no_source,suppressed)&&!no_source.consumed());
}
void freshness_and_late_callbacks(){
    ticks=0;NativeCommandMailbox box(7,&clock_read);require(box.offer(baseline())==NativeInputOffer::accepted);
    const auto frame=begin(box);require(box.finish_update(frame.epoch)&&evidence(box,frame));
    ticks=100;require(box.current_epoch()==frame.epoch&&box.consumed().has_value());
    ticks=101;require(box.current_epoch()==0&&!box.consumed());
    require(!box.observe_held(frame.epoch,CommandButton::guard,0)&&!box.failed());
    require(box.reset_generation(8));ticks=102;require(box.offer(baseline(8))==NativeInputOffer::accepted);
    const auto newer=begin(box);require(newer.epoch>frame.epoch);
    // A delayed older callback must not even sample a clock belonging to the
    // new lifetime, nor corrupt its pending association.
    ticks=1;require(!box.observe_resolver(frame.epoch,{},true,true,true)&&!box.failed());
    ticks=103;require(box.finish_update(newer.epoch)&&evidence(box,newer));
    require(box.consumed()->source.input_generation==8);

    ticks=0;NativeCommandMailbox slow(7,&clock_read);require(slow.offer(baseline())==NativeInputOffer::accepted);
    const auto old=begin(slow);require(!evidence(slow,old));ticks=101;
    require(!slow.finish_update(old.epoch)&&!slow.consumed()&&slow.current_epoch()==0&&!slow.failed());
    ticks=102;require(slow.offer(command(7,2))==NativeInputOffer::accepted);const auto fresh=begin(slow);
    require(fresh.epoch>old.epoch&&slow.finish_update(fresh.epoch)&&evidence(slow,fresh)&&slow.consumed().has_value());
}
void suspend_reset_overlap_and_abandon(){
    ticks=0;NativeCommandMailbox before_baseline(7,&clock_read);
    const auto quarantine=begin(before_baseline);require(!quarantine.source&&quarantine.input==PlayerInput{});
    require(before_baseline.finish_update(quarantine.epoch)&&evidence(before_baseline,quarantine)&&!before_baseline.consumed());
    ticks=0;NativeCommandMailbox box(7,&clock_read);require(box.offer(baseline())==NativeInputOffer::accepted);
    const auto old=begin(box);require(!box.reset_generation(8));box.suspend();
    require(!box.failed()&&box.current_epoch()==0&&!box.observe_held(old.epoch,CommandButton::guard,0));
    require(!box.reset_generation(8));require(!box.finish_update(old.epoch));
    require(box.reset_generation(8)&&!box.reset_generation(8)&&!box.reset_generation(7));
    ticks=1;require(box.offer(baseline(8))==NativeInputOffer::accepted);const auto fresh=begin(box);
    require(fresh.epoch>old.epoch&&box.finish_update(fresh.epoch)&&evidence(box,fresh));
    require(box.consumed()->source.input_generation==8);
    box.suspend();require(!box.consumed()&&box.current_epoch()==0);
    const auto neutral=box.begin_update();require(neutral.epoch>fresh.epoch&&neutral.accepted&&neutral.input==PlayerInput{}&&!neutral.source);
    require(box.finish_update(neutral.epoch)&&evidence(box,neutral)&&!box.consumed());

    ticks=0;NativeCommandMailbox overlapping(7,&clock_read);require(overlapping.offer(baseline())==NativeInputOffer::accepted);
    const auto a=begin(overlapping);const auto bad=overlapping.begin_update();
    require(!bad.epoch&&!bad.accepted&&overlapping.failed()&&!overlapping.consumed());
    require(!overlapping.reset_generation(8));require(!overlapping.finish_update(a.epoch));
    require(overlapping.reset_generation(8));ticks=1;require(overlapping.offer(baseline(8))==NativeInputOffer::accepted);
    const auto b=begin(overlapping);require(b.epoch>a.epoch);overlapping.abandon_update(b.epoch);
    require(overlapping.failed()&&!overlapping.finish_update(b.epoch)&&!overlapping.consumed());
}
void actual_observed_values(){
    ticks=0;NativeCommandMailbox box(7,&clock_read);auto value=baseline();value.state.input.movement={{.6f,.8f},.35f};
    require(box.offer(value)==NativeInputOffer::accepted);const auto frame=begin(box);require(box.finish_update(frame.epoch));
    const MovementIntent observed{{.60004f,.79997f},.35005f};require(valid(PlayerInput{observed,0}));
    require(box.observe_resolver(frame.epoch,observed,true,true,true));
    require(box.observe_held(frame.epoch,CommandButton::attack,0)&&box.observe_held(frame.epoch,CommandButton::guard,0));
    const auto consumed=box.consumed();require(consumed&&consumed->input.movement==observed&&consumed->input.movement!=frame.input.movement);
    auto wrong=observed;wrong.intensity=.3502f;
    require(!box.observe_resolver(frame.epoch,wrong,true,true,true)&&box.failed()&&!box.consumed());
    // No combination of separate partial calls can create one complete native
    // resolver observation, even if all flags appeared somewhere in the epoch.
    ticks=0;NativeCommandMailbox partial(7,&clock_read);require(partial.offer(baseline())==NativeInputOffer::accepted);
    const auto neutral=begin(partial);require(partial.finish_update(neutral.epoch));
    require(partial.observe_held(neutral.epoch,CommandButton::attack,0)&&partial.observe_held(neutral.epoch,CommandButton::guard,0));
    require(!partial.observe_resolver(neutral.epoch,{},true,false,false));
    require(!partial.observe_resolver(neutral.epoch,{},false,true,false));
    require(!partial.observe_resolver(neutral.epoch,{},false,false,true)&&!partial.consumed()&&!partial.failed());
}
void invalid_observation_and_ingress(){
    for(unsigned defect=0;defect<6;++defect){
        ticks=0;NativeCommandMailbox box(7,&clock_read);require(box.offer(baseline())==NativeInputOffer::accepted);const auto frame=begin(box);require(box.finish_update(frame.epoch));
        bool result=false;
        if(defect==0)result=box.observe_resolver(frame.epoch,{{1,0},.5f},true,true,true);
        if(defect==1)result=box.observe_resolver(frame.epoch,{{},std::numeric_limits<float>::quiet_NaN()},true,true,true);
        if(defect==2)result=box.observe_held(frame.epoch,CommandButton::guard,-1);
        if(defect==3)result=box.observe_held(frame.epoch,CommandButton::guard,1);
        if(defect==4)result=box.observe_held(frame.epoch,CommandButton::jump,0);
        if(defect==5)result=box.observe_held(frame.epoch,CommandButton::attack,std::numeric_limits<float>::infinity());
        require(!result&&box.failed()&&!box.consumed()&&!box.begin_update().accepted);
        require(!box.finish_update(frame.epoch));
    }
    ticks=10;NativeCommandMailbox clock(7,&clock_read);require(clock.offer(baseline())==NativeInputOffer::accepted);const auto frame=begin(clock);require(clock.finish_update(frame.epoch));
    ticks=9;require(!clock.observe_held(frame.epoch,CommandButton::attack,0)&&clock.failed());
    ticks=0;NativeCommandMailbox zero(0,&clock_read),missing(7,nullptr);require(zero.failed()&&missing.failed());
    NativeCommandMailbox wrong(7,&clock_read);require(wrong.offer(baseline(8))==NativeInputOffer::failed&&wrong.failed());
    NativeCommandMailbox full(7,&clock_read);require(full.offer(baseline())==NativeInputOffer::accepted);
    auto filled=command(7,2);filled.state.source_ms=64;filled.transition_count=command_queue_capacity;
    for(std::size_t i=0;i<filled.transition_count;++i)filled.transitions[i]={i+1,i+1,CommandButton::attack,i%2==0};
    ticks=1;require(full.offer(filled)==NativeInputOffer::accepted);
    auto extra=command(7,3);extra.state.source_ms=65;extra.state.input.held=button_mask(CommandButton::attack);
    extra.transition_count=1;extra.transitions[0]={65,65,CommandButton::attack,true};
    ticks=2;require(full.offer(extra)==NativeInputOffer::failed&&full.failed()&&!full.consumed());
    require(full.failure().kind==NativeMailboxFault::latch_failure&&full.failure().latch_fault==NativeInputFault::overflow);
}
void pulse_identity_and_duplicates(){
    ticks=0;NativeCommandMailbox box(7,&clock_read);require(box.offer(baseline())==NativeInputOffer::accepted);
    auto first=begin(box);require(box.finish_update(first.epoch)&&evidence(box,first));
    auto tap=command(7,2);tap.state.source_ms=2;tap.transition_count=2;
    tap.transitions[0]={1,1,CommandButton::attack,true};tap.transitions[1]={2,2,CommandButton::attack,false};
    ticks=1;require(box.offer(tap)==NativeInputOffer::accepted&&box.offer(tap)==NativeInputOffer::stale);
    auto press=begin(box);require(press.input.held==button_mask(CommandButton::attack)&&!press.source);
    require(box.finish_update(press.epoch)&&evidence(box,press)&&!box.consumed());
    ticks=2;auto release=begin(box);require(release.input.held==0&&release.source);
    require(box.finish_update(release.epoch)&&evidence(box,release)&&box.consumed()->source.frame_sequence==2);
    ticks=3;auto held=begin(box);require(held.input.held==0&&held.epoch>release.epoch&&box.finish_update(held.epoch)&&evidence(box,held));
    ticks=504;auto expired=box.begin_update();require(expired.accepted&&expired.input==PlayerInput{}&&!expired.source&&box.finish_update(expired.epoch)&&!box.consumed());
}
void concurrent_ingress_updates_and_observers(){
    ticks=0;NativeCommandMailbox box(7,&clock_progress);require(box.offer(baseline())==NativeInputOffer::accepted);
    std::barrier phases(3);std::atomic<bool> good{true};NativeInputFrame shared;
    constexpr unsigned rounds=64;
    std::thread native([&]{for(unsigned i=0;i<rounds;++i){
        shared=box.begin_update();if(!shared.accepted||!shared.source||shared.source->frame_sequence!=i+1)good=false;
        phases.arrive_and_wait();phases.arrive_and_wait();
        if(!box.finish_update(shared.epoch))good=false;
        phases.arrive_and_wait();phases.arrive_and_wait();
    }});
    std::thread ingress([&]{for(unsigned i=0;i<rounds;++i){
        phases.arrive_and_wait();auto next=command(7,i+2);next.state.input.movement={{(i%2)?-1.f:1.f,0},.35f};
        if(box.current_epoch()!=0||box.offer(next)!=NativeInputOffer::accepted||box.reset_generation(8))good=false;
        phases.arrive_and_wait();phases.arrive_and_wait();phases.arrive_and_wait();
    }});
    std::thread observer([&]{for(unsigned i=0;i<rounds;++i){
        phases.arrive_and_wait();if(evidence(box,shared)||box.consumed()||box.failed())good=false;
        phases.arrive_and_wait();phases.arrive_and_wait();
        if(!evidence(box,shared))good=false;
        const auto result=box.consumed();
        if(!result||result->epoch!=shared.epoch||result->source.frame_sequence!=i+1||result->input!=shared.input)good=false;
        phases.arrive_and_wait();
    }});
    native.join();ingress.join();observer.join();require(good&&!box.failed());
    const auto final=box.consumed();require(final&&final->source.frame_sequence==rounds);
}
void failure_diagnostics(){
    ticks=10;NativeCommandMailbox box(7,&clock_read);auto requested=baseline();requested.state.input.movement={{1,0},.35f};
    require(box.offer(requested)==NativeInputOffer::accepted);const auto frame=begin(box);require(box.finish_update(frame.epoch));
    const MovementIntent actual{{0,-1},.5f};
    require(!box.observe_resolver(frame.epoch,actual,true,true,true));const auto first=box.failure();
    require(first.kind==NativeMailboxFault::movement_mismatch&&first.latch_fault==NativeInputFault::none&&first.epoch==frame.epoch);
    require(first.expected==frame.input&&first.observed.movement==actual&&first.observed.held==0);
    // Later calls, a clock failure during suspend, and even an explicitly new
    // lifetime cannot overwrite the mailbox object's first diagnostic report.
    require(!box.observe_held(frame.epoch,CommandButton::guard,1));ticks=9;box.suspend();
    ticks=20;require(box.reset_generation(8));const auto check_first=[&]{const auto value=box.failure();
        return value.kind==first.kind&&value.latch_fault==first.latch_fault&&value.epoch==first.epoch&&
            value.expected==first.expected&&value.observed==first.observed&&value.button==first.button&&value.held_duration==first.held_duration;};
    require(check_first());require(box.offer(baseline(8))==NativeInputOffer::accepted);const auto later=begin(box);box.abandon_update(later.epoch);
    require(box.failed()&&check_first());
    std::atomic<bool> coherent{true};std::thread reader([&]{for(unsigned i=0;i<100;++i)if(!check_first())coherent=false;});
    std::thread suspender([&]{for(unsigned i=0;i<100;++i)box.suspend();});reader.join();suspender.join();require(coherent&&check_first());

    ticks=0;NativeCommandMailbox invalid_move(7,&clock_read);require(invalid_move.offer(baseline())==NativeInputOffer::accepted);
    const auto invalid_frame=begin(invalid_move);require(invalid_move.finish_update(invalid_frame.epoch));
    const auto nan=std::numeric_limits<float>::quiet_NaN();const MovementIntent bad{{},nan};
    require(!invalid_move.observe_resolver(invalid_frame.epoch,bad,true,true,true));
    const auto invalid=invalid_move.failure();require(invalid.kind==NativeMailboxFault::invalid_movement&&invalid.epoch==invalid_frame.epoch);
    require(std::bit_cast<std::uint32_t>(invalid.observed.movement.intensity)==std::bit_cast<std::uint32_t>(nan)&&invalid.expected==invalid_frame.input);

    NativeCommandMailbox held(7,&clock_read);require(held.offer(baseline())==NativeInputOffer::accepted);const auto held_frame=begin(held);require(held.finish_update(held_frame.epoch));
    require(!held.observe_held(held_frame.epoch,CommandButton::guard,.25f));const auto mismatch=held.failure();
    require(mismatch.kind==NativeMailboxFault::held_mismatch&&mismatch.epoch==held_frame.epoch&&mismatch.expected.held==0&&
        mismatch.observed.held==button_mask(CommandButton::guard)&&mismatch.button==CommandButton::guard&&mismatch.held_duration==.25f);
    NativeCommandMailbox invalid_held(7,&clock_read);require(invalid_held.offer(baseline())==NativeInputOffer::accepted);
    const auto invalid_hold_frame=begin(invalid_held);require(invalid_held.finish_update(invalid_hold_frame.epoch));
    require(!invalid_held.observe_held(invalid_hold_frame.epoch,CommandButton::attack,-1));
    const auto invalid_hold=invalid_held.failure();require(invalid_hold.kind==NativeMailboxFault::invalid_held&&invalid_hold.button==CommandButton::attack&&invalid_hold.held_duration==-1);

    NativeCommandMailbox no_clock(7,nullptr),no_generation(0,&clock_read);
    require(no_clock.failure().kind==NativeMailboxFault::invalid_clock&&no_clock.failure().epoch==0);
    require(no_generation.failure().kind==NativeMailboxFault::latch_failure&&no_generation.failure().latch_fault==NativeInputFault::invalid);
    NativeCommandMailbox wrong_generation(7,&clock_read);require(wrong_generation.offer(baseline(8))==NativeInputOffer::failed);
    require(wrong_generation.failure().kind==NativeMailboxFault::latch_failure&&wrong_generation.failure().latch_fault==NativeInputFault::generation_changed);

    NativeCommandMailbox overlap(7,&clock_read);require(overlap.offer(baseline())==NativeInputOffer::accepted);const auto pending=begin(overlap);
    require(!overlap.begin_update().accepted&&overlap.failure().kind==NativeMailboxFault::overlapping_update&&overlap.failure().epoch==pending.epoch);
    NativeCommandMailbox abandoned(7,&clock_read);require(abandoned.offer(baseline())==NativeInputOffer::accepted);const auto original=begin(abandoned);
    abandoned.abandon_update(original.epoch);require(abandoned.failure().kind==NativeMailboxFault::abandoned_update&&abandoned.failure().epoch==original.epoch);
    ticks=10;NativeCommandMailbox clock(7,&clock_read);require(clock.offer(baseline())==NativeInputOffer::accepted);const auto clock_frame=begin(clock);
    require(clock.finish_update(clock_frame.epoch));ticks=9;require(!clock.observe_held(clock_frame.epoch,CommandButton::attack,0));
    require(clock.failure().kind==NativeMailboxFault::clock_regressed&&clock.failure().epoch==clock_frame.epoch);
}
void native_previous_stick_filter(){
    const MovementIntent movement{{0,1},.35f};
    const auto moving=[&](std::uint64_t frame){auto value=command(7,frame);value.state.input.movement=movement;return value;};
    ticks=0;NativeCommandMailbox strict(7,&clock_read);
    require(strict.offer(baseline())==NativeInputOffer::accepted);
    auto frame=begin(strict);require(strict.finish_update(frame.epoch)&&evidence(strict,frame));
    ticks=1;require(strict.offer(moving(2))==NativeInputOffer::accepted);frame=begin(strict);require(strict.finish_update(frame.epoch));
    require(!strict.observe_resolver(frame.epoch,{},true,true,true)&&strict.failed());

    ticks=0;NativeCommandMailbox filtered(7,&clock_read,NativeMovementOnset::defer_first_zero);
    require(filtered.offer(baseline())==NativeInputOffer::accepted);
    frame=begin(filtered);require(filtered.finish_update(frame.epoch)&&evidence(filtered,frame));
    ticks=1;require(filtered.offer(moving(2))==NativeInputOffer::accepted);frame=begin(filtered);require(filtered.finish_update(frame.epoch));
    require(filtered.observe_held(frame.epoch,CommandButton::attack,0)&&filtered.observe_held(frame.epoch,CommandButton::guard,0));
    // Repeated callbacks within the withheld epoch still cannot earn credit.
    for(unsigned i=0;i<3;++i)require(!filtered.observe_resolver(frame.epoch,{},true,true,true)&&!filtered.failed()&&!filtered.consumed());
    const auto withheld=frame.epoch;
    ticks=2;require(filtered.offer(moving(3))==NativeInputOffer::accepted);frame=begin(filtered);require(filtered.finish_update(frame.epoch));
    require(!filtered.observe_resolver(withheld,movement,true,true,true)&&!filtered.consumed());
    require(evidence(filtered,frame)&&filtered.consumed()->source.frame_sequence==3&&filtered.consumed()->input.movement==movement);
    // This policy never tolerates a later loss of movement or a wrong nonzero
    // direction, and fresh command sequence numbers cannot renew the onset.
    ticks=3;require(filtered.offer(moving(4))==NativeInputOffer::accepted);frame=begin(filtered);require(filtered.finish_update(frame.epoch));
    require(!filtered.observe_resolver(frame.epoch,{},true,true,true)&&filtered.failed()&&!filtered.consumed());

    ticks=0;NativeCommandMailbox blocked(7,&clock_read,NativeMovementOnset::defer_first_zero);
    require(blocked.offer(baseline())==NativeInputOffer::accepted);frame=begin(blocked);require(blocked.finish_update(frame.epoch)&&evidence(blocked,frame));
    ticks=1;require(blocked.offer(moving(2))==NativeInputOffer::accepted);frame=begin(blocked);require(blocked.finish_update(frame.epoch));
    require(!blocked.observe_resolver(frame.epoch,{},true,true,true)&&!blocked.failed());
    // Even without a newer packet, the next engine update must resolve it.
    ticks=2;frame=begin(blocked);require(blocked.finish_update(frame.epoch));
    require(!blocked.observe_resolver(frame.epoch,{},true,true,true)&&blocked.failed());

    ticks=0;NativeCommandMailbox wrong(7,&clock_read,NativeMovementOnset::defer_first_zero);
    require(wrong.offer(baseline())==NativeInputOffer::accepted);frame=begin(wrong);require(wrong.finish_update(frame.epoch)&&evidence(wrong,frame));
    ticks=1;require(wrong.offer(moving(2))==NativeInputOffer::accepted);frame=begin(wrong);require(wrong.finish_update(frame.epoch));
    require(!wrong.observe_resolver(frame.epoch,{{1,0},.35f},true,true,true)&&wrong.failed());

    // Resetting command association without an actual neutral native update
    // cannot renew a withheld onset. This includes a fresh baseline, suspend,
    // and a newer input generation on the same native controller.
    for(unsigned reset=0;reset<3;++reset){
        ticks=0;NativeCommandMailbox rebaseline(7,&clock_read,NativeMovementOnset::defer_first_zero);
        require(rebaseline.offer(baseline())==NativeInputOffer::accepted);frame=begin(rebaseline);
        require(rebaseline.finish_update(frame.epoch)&&evidence(rebaseline,frame));
        ticks=1;require(rebaseline.offer(moving(2))==NativeInputOffer::accepted);frame=begin(rebaseline);
        require(rebaseline.finish_update(frame.epoch)&&!rebaseline.observe_resolver(frame.epoch,{},true,true,true)&&!rebaseline.failed());
        if(reset==1)rebaseline.suspend();
        if(reset==2)require(rebaseline.reset_generation(8));
        ticks=2;auto replacement=baseline(reset==2?8:7,reset==2?1:3);replacement.state.input.movement=movement;
        require(rebaseline.offer(replacement)==NativeInputOffer::accepted);frame=begin(rebaseline);require(rebaseline.finish_update(frame.epoch));
        require(!rebaseline.observe_resolver(frame.epoch,{},true,true,true)&&rebaseline.failed());
    }
    ticks=0;NativeCommandMailbox resumed(7,&clock_read,NativeMovementOnset::defer_first_zero);
    require(resumed.offer(baseline())==NativeInputOffer::accepted);frame=begin(resumed);require(resumed.finish_update(frame.epoch)&&evidence(resumed,frame));
    ticks=1;require(resumed.offer(moving(2))==NativeInputOffer::accepted);frame=begin(resumed);require(resumed.finish_update(frame.epoch)&&evidence(resumed,frame));
    resumed.suspend();ticks=2;frame=begin(resumed);require(frame.input==PlayerInput{}&&!frame.source&&resumed.finish_update(frame.epoch)&&evidence(resumed,frame));
    ticks=3;auto restart=baseline(7,3);restart.state.input.movement=movement;
    require(resumed.offer(restart)==NativeInputOffer::accepted);frame=begin(resumed);require(resumed.finish_update(frame.epoch));
    require(!resumed.observe_resolver(frame.epoch,{},true,true,true)&&!resumed.failed()&&!resumed.consumed());
    ticks=4;frame=begin(resumed);require(resumed.finish_update(frame.epoch)&&evidence(resumed,frame)&&resumed.consumed().has_value());
}
void verified_native_steering_onset(){
    const MovementIntent movement{{0,1},.35f},opposite{{0,-1},.35f};
    const auto moving=[&](std::uint64_t generation=7,std::uint64_t sequence=2){auto value=command(generation,sequence);value.state.input.movement=movement;return value;};
    const auto prepare=[&](NativeCommandMailbox& box){
        require(box.offer(baseline())==NativeInputOffer::accepted);auto frame=begin(box);
        require(box.finish_update(frame.epoch)&&evidence(box,frame));
        ticks=1;require(box.offer(moving())==NativeInputOffer::accepted);frame=begin(box);require(box.finish_update(frame.epoch));return frame;
    };
    const auto next=[&](NativeCommandMailbox& box,std::uint64_t time,bool atomic_turn=false){
        ticks=time;auto frame=begin(box);const auto proof=atomic_turn?std::optional{NativeTurnIntent{movement,true}}:std::nullopt;
        require(box.finish_update(frame.epoch,proof));return frame;
    };
    const auto held=[&](NativeCommandMailbox& box,const NativeInputFrame& frame){
        require(box.observe_held(frame.epoch,CommandButton::attack,0)&&box.observe_held(frame.epoch,CommandButton::guard,0));
    };

    ticks=0;NativeCommandMailbox delayed(7,&clock_read,NativeMovementOnset::defer_verified_steering);auto frame=prepare(delayed);
    // The first filtered zero retains the old exception without steering proof.
    require(!delayed.observe_resolver(frame.epoch,{},true,true,true)&&!delayed.failed()&&!delayed.consumed());
    for(std::uint64_t update=2;update<=3;++update){
        frame=next(delayed,update,true);held(delayed,frame);
        require(!delayed.consumed());
        require(!delayed.observe_resolver(frame.epoch,opposite,true,true,true)&&!delayed.failed()&&!delayed.consumed());
    }
    // A fourth update must physically match. Steering or held samples alone do
    // not credit it, and partial native resolver stages remain insufficient.
    frame=next(delayed,4,true);require(!delayed.consumed());
    require(!delayed.observe_resolver(frame.epoch,movement,true,false,true)&&!delayed.consumed());
    require(delayed.observe_resolver(frame.epoch,movement,true,true,true)&&!delayed.consumed());
    require(delayed.observe_held(frame.epoch,CommandButton::attack,0)&&!delayed.consumed());
    require(delayed.observe_held(frame.epoch,CommandButton::guard,0));
    const auto consumed=delayed.consumed();require(consumed&&consumed->source.frame_sequence==2&&consumed->input.movement==movement);

    // The bound counts actual native updates, not packets or resolver calls.
    for(bool zero:{false,true}){
        ticks=0;NativeCommandMailbox persistent(7,&clock_read,NativeMovementOnset::defer_verified_steering);frame=prepare(persistent);
        require(!persistent.observe_resolver(frame.epoch,{},true,true,true)&&!persistent.failed());
        for(std::uint64_t update=2;update<=3;++update){
            frame=next(persistent,update);require(persistent.observe_turn_intent(frame.epoch,movement,true));
            for(unsigned duplicate=0;duplicate<3;++duplicate)
                require(!persistent.observe_resolver(frame.epoch,zero?MovementIntent{}:opposite,true,true,true)&&!persistent.failed()&&!persistent.consumed());
        }
        frame=next(persistent,4);require(persistent.observe_turn_intent(frame.epoch,movement,true));
        require(!persistent.observe_resolver(frame.epoch,zero?MovementIntent{}:opposite,true,true,true)&&persistent.failed());
        require(persistent.failure().kind==NativeMailboxFault::movement_mismatch&&!persistent.consumed());
    }
    // Missing, false and previous-epoch steering cannot authorize a divergent
    // physical observation. A wrong/invalid nonzero intent fails immediately.
    for(unsigned defect=0;defect<7;++defect){
        ticks=0;NativeCommandMailbox proof(7,&clock_read,NativeMovementOnset::defer_verified_steering);const auto prior=prepare(proof);
        require(proof.observe_turn_intent(prior.epoch,movement,true));
        require(!proof.observe_resolver(prior.epoch,{},true,true,true));frame=next(proof,2);
        if(defect==1)require(!proof.observe_turn_intent(frame.epoch,movement,false));
        if(defect==2)require(!proof.observe_turn_intent(prior.epoch,movement,true));
        if(defect==3){require(!proof.observe_turn_intent(frame.epoch,opposite,true)&&proof.failed());require(proof.failure().kind==NativeMailboxFault::movement_mismatch);continue;}
        if(defect==4){require(!proof.observe_turn_intent(frame.epoch,{{},std::numeric_limits<float>::quiet_NaN()},true)&&proof.failed());require(proof.failure().kind==NativeMailboxFault::invalid_movement);continue;}
        if(defect==5){require(!proof.observe_turn_intent(frame.epoch,{},true)&&proof.failed());require(proof.failure().kind==NativeMailboxFault::invalid_movement);continue;}
        if(defect==6){require(proof.observe_turn_intent(frame.epoch,movement,true));require(!proof.observe_turn_intent(frame.epoch,movement,false));}
        require(!proof.observe_resolver(frame.epoch,opposite,true,true,true)&&proof.failed()&&!proof.consumed());
    }
    // A nonzero wrong direction on the first update also needs actual steering
    // proof; the inherited no-proof exception is exclusively the first zero.
    ticks=0;NativeCommandMailbox first_wrong(7,&clock_read,NativeMovementOnset::defer_verified_steering);frame=prepare(first_wrong);
    require(!first_wrong.observe_resolver(frame.epoch,opposite,true,true,true)&&first_wrong.failed());
    ticks=0;NativeCommandMailbox unfinished(7,&clock_read,NativeMovementOnset::defer_verified_steering);
    require(unfinished.offer(baseline())==NativeInputOffer::accepted);frame=begin(unfinished);require(unfinished.finish_update(frame.epoch));
    ticks=1;require(unfinished.offer(moving())==NativeInputOffer::accepted);frame=begin(unfinished);
    require(!unfinished.observe_turn_intent(frame.epoch,movement,true)&&!unfinished.failed());
    require(unfinished.finish_update(frame.epoch));require(!unfinished.observe_resolver(frame.epoch,opposite,true,true,true)&&unfinished.failed());

    ticks=0;NativeCommandMailbox stale_ignored(7,&clock_read,NativeMovementOnset::defer_verified_steering);const auto old_epoch=prepare(stale_ignored);
    require(!stale_ignored.observe_resolver(old_epoch.epoch,{},true,true,true));frame=next(stale_ignored,2);
    require(stale_ignored.observe_turn_intent(frame.epoch,movement,true));
    require(!stale_ignored.observe_turn_intent(old_epoch.epoch,opposite,true)&&!stale_ignored.failed());
    require(!stale_ignored.observe_resolver(frame.epoch,opposite,true,true,true)&&!stale_ignored.failed()&&!stale_ignored.consumed());

    ticks=0;NativeCommandMailbox timeout(7,&clock_read,NativeMovementOnset::defer_verified_steering);frame=prepare(timeout);
    require(!timeout.observe_resolver(frame.epoch,{},true,true,true));
    frame=next(timeout,101);require(timeout.observe_turn_intent(frame.epoch,movement,true));
    require(!timeout.observe_resolver(frame.epoch,opposite,true,true,true)&&!timeout.failed()); // Exactly100ms.
    frame=next(timeout,102);require(timeout.observe_turn_intent(frame.epoch,movement,true));
    require(!timeout.observe_resolver(frame.epoch,opposite,true,true,true)&&timeout.failed());
    ticks=0;NativeCommandMailbox stale(7,&clock_read,NativeMovementOnset::defer_verified_steering);frame=prepare(stale);
    ticks=102;require(!stale.observe_turn_intent(frame.epoch,movement,true)&&!stale.failed());
    frame=next(stale,102);require(stale.observe_turn_intent(frame.epoch,movement,true));
    require(!stale.observe_resolver(frame.epoch,opposite,true,true,true)&&stale.failed());

    for(unsigned association=0;association<4;++association){
        ticks=0;NativeCommandMailbox unchanged(7,&clock_read,NativeMovementOnset::defer_verified_steering);frame=prepare(unchanged);
        require(!unchanged.observe_resolver(frame.epoch,{},true,true,true));
        for(std::uint64_t update=2;update<=3;++update){frame=next(unchanged,update);require(unchanged.observe_turn_intent(frame.epoch,movement,true));require(!unchanged.observe_resolver(frame.epoch,opposite,true,true,true)&&!unchanged.failed());}
        if(association==1)unchanged.suspend();
        if(association==2)require(unchanged.reset_generation(8));
        ticks=4;
        if(association==3)require(unchanged.offer(baseline(7,3))==NativeInputOffer::accepted); // No neutral begin.
        auto replacement=baseline(association==2?8:7,association==2?1:association==3?4:3);replacement.state.input.movement=movement;
        require(unchanged.offer(replacement)==NativeInputOffer::accepted);frame=next(unchanged,4);
        require(unchanged.observe_turn_intent(frame.epoch,movement,true));
        require(!unchanged.observe_resolver(frame.epoch,opposite,true,true,true)&&unchanged.failed());
    }
    // An association reset cannot renew the original100ms deadline either.
    ticks=0;NativeCommandMailbox old_window(7,&clock_read,NativeMovementOnset::defer_verified_steering);frame=prepare(old_window);
    require(!old_window.observe_resolver(frame.epoch,{},true,true,true));old_window.suspend();ticks=102;
    auto restart=baseline(7,3);restart.state.input.movement=movement;require(old_window.offer(restart)==NativeInputOffer::accepted);frame=next(old_window,102);
    require(old_window.observe_turn_intent(frame.epoch,movement,true));require(!old_window.observe_resolver(frame.epoch,opposite,true,true,true)&&old_window.failed());

    ticks=0;NativeCommandMailbox rearmed(7,&clock_read,NativeMovementOnset::defer_verified_steering);frame=prepare(rearmed);
    require(evidence(rearmed,frame)&&rearmed.consumed());rearmed.suspend();
    frame=next(rearmed,2);require(frame.input==PlayerInput{}&&!frame.source&&evidence(rearmed,frame));
    ticks=3;restart=baseline(7,3);restart.state.input.movement=movement;require(rearmed.offer(restart)==NativeInputOffer::accepted);frame=next(rearmed,3);
    require(!rearmed.observe_resolver(frame.epoch,{},true,true,true)&&!rearmed.failed());
    frame=next(rearmed,4);require(rearmed.observe_turn_intent(frame.epoch,movement,true));require(!rearmed.observe_resolver(frame.epoch,opposite,true,true,true)&&!rearmed.failed());
    frame=next(rearmed,5);require(evidence(rearmed,frame)&&rearmed.consumed());
    // Once the physical onset has matched, later divergence is not a transient.
    frame=next(rearmed,6);require(rearmed.observe_turn_intent(frame.epoch,movement,true));require(!rearmed.observe_resolver(frame.epoch,opposite,true,true,true)&&rearmed.failed());
    for(const auto policy:{NativeMovementOnset::strict,NativeMovementOnset::defer_first_zero}){
        ticks=0;NativeCommandMailbox existing(7,&clock_read,policy);frame=prepare(existing);
        require(existing.observe_turn_intent(frame.epoch,movement,true));require(!existing.observe_resolver(frame.epoch,opposite,true,true,true)&&existing.failed());
    }
}
void atomic_completion_and_turn_proof(){
    const MovementIntent movement{{0,1},.35f},opposite{{0,-1},.35f};
    const auto prepare=[&](NativeCommandMailbox& box){
        require(box.offer(baseline())==NativeInputOffer::accepted);auto frame=begin(box);require(box.finish_update(frame.epoch));
        ticks=1;auto moving=command(7,2);moving.state.input.movement=movement;
        require(box.offer(moving)==NativeInputOffer::accepted);return begin(box);
    };
    for(unsigned defect=0;defect<4;++defect){
        ticks=0;NativeCommandMailbox box(7,&clock_read,NativeMovementOnset::defer_verified_steering);const auto frame=prepare(box);
        NativeTurnIntent turn{movement,true};
        if(defect==0)turn.movement=opposite;
        if(defect==1)turn.movement.intensity=std::numeric_limits<float>::quiet_NaN();
        if(defect==2)turn.movement={};
        if(defect==3)turn.native_turn_observed=false;
        const auto completed=box.finish_update(frame.epoch,turn);
        if(defect==3){
            require(completed&&!box.failed()&&!box.consumed());
            require(!box.observe_resolver(frame.epoch,opposite,true,true,true)&&box.failed());
        }else {
            require(!completed&&box.failed()&&box.current_epoch()==0&&!box.consumed());
            require(!box.observe_resolver(frame.epoch,movement,true,true,true));
            require(box.failure().kind==(defect==0?NativeMailboxFault::movement_mismatch:NativeMailboxFault::invalid_movement));
        }
    }
    // Observer threads may race completion, but can never see a completed
    // epoch before its supplied steering proof has been validated/published.
    // Nonzero opposite physical input detects a missing proof even on update1.
    for(unsigned trial=0;trial<24;++trial){
        ticks=0;NativeCommandMailbox box(7,&clock_read,NativeMovementOnset::defer_verified_steering);const auto frame=prepare(box);
        std::barrier stages(2);std::atomic<bool> good{true};
        std::thread completion([&]{
            stages.arrive_and_wait();
            if(!box.finish_update(frame.epoch,NativeTurnIntent{movement,true}))good=false;
            stages.arrive_and_wait();
        });
        std::thread observer([&]{
            stages.arrive_and_wait();bool ready=false;
            for(unsigned attempts=0;attempts<100000;++attempts){
                if(box.current_epoch()==frame.epoch){ready=true;break;}
                if(box.failed())break;
                std::this_thread::yield();
            }
            if(!ready||box.observe_resolver(frame.epoch,opposite,true,true,true)||box.failed()||box.consumed())good=false;
            stages.arrive_and_wait();
        });
        completion.join();observer.join();require(good&&!box.failed()&&!box.consumed());
    }
}
}
int main(){try{completion_and_required_evidence();freshness_and_late_callbacks();suspend_reset_overlap_and_abandon();actual_observed_values();invalid_observation_and_ingress();pulse_identity_and_duplicates();concurrent_ingress_updates_and_observers();failure_diagnostics();native_previous_stick_filter();verified_native_steering_onset();atomic_completion_and_turn_proof();
    std::cout<<"PASS portable native command mailbox, threaded ingress/update/observer epochs, actual-evidence requirements, stale/reset/pause guards and no native binding claim\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
