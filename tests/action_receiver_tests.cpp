#include "action_receiver.h"
#include "action_native_contract.h"
#include "remote_actor.h"
#include <iostream>
#include <stdexcept>
using namespace skcoop::lan;
using namespace skcoop::actors;
namespace {
void check(bool result,int line){if(!result)throw std::runtime_error("Action receiver invariant at line "+std::to_string(line));}
#define require(x) check((x),__LINE__)
ActionScope scope(){ActionScope s;s.room[0]=1;s.connection[0]=2;s.participant=3;s.epoch=7;return s;}
struct Stream {
    ActionCapture source{7};ActionFrameBuffer frames;std::uint64_t sequence{};
    void offer(std::uint32_t time,std::uint64_t arrival){const auto history=source.snapshot(time);require(history.has_value());
        require(frames.push({scope(),*history,++sequence,arrival})==ActionFramePush::accepted);require(source.offered(history->latest));}
    void event(ActionKind kind,std::uint32_t time){require(source.capture(kind,time,30));}
    void acknowledge(ActionReceiver& receiver){require(frames.acknowledge(receiver.acknowledgement()));}
};
void lifecycle(){
    Stream s;ActionReceiver r;s.offer(0,1000);r.observe(s.frames,false,1000);s.acknowledge(r);
    s.event(ActionKind::ground_jump_ready,10);s.offer(20,1020);r.observe(s.frames,false,1020);s.acknowledge(r);
    // Spawn/settle history is discarded, never replayed on entering following.
    r.observe(s.frames,true,1021);require(r.active()&&!r.take(1121));require(r.discarded()==1);
    s.event(ActionKind::ground_step,30);s.event(ActionKind::sprint_start_from_step,40);s.offer(50,1050);
    r.observe(s.frames,true,1050);s.acknowledge(r);require(!r.take(1129));
    require(r.take(1130)->kind==ActionKind::ground_step);require(r.take(1140)->kind==ActionKind::sprint_start_from_step);
    s.offer(60,1060);r.observe(s.frames,true,1141);s.acknowledge(r);require(!r.take(1160));
    // A pause releases held state; resuming restores metadata without a pulse.
    r.observe(s.frames,false,1161);require(!r.held_sprint()&&!r.active());s.acknowledge(r);
    s.event(ActionKind::ground_jump_ready,70);s.offer(80,1180);r.observe(s.frames,false,1180);s.acknowledge(r);
    s.event(ActionKind::sprint_start_from_step,90);s.offer(100,1200);r.observe(s.frames,true,1200);s.acknowledge(r);
    require(r.restored()&&r.held_sprint()&&!r.take(1300));
    require(!r.take(1701)&&!r.active()&&!r.held_sprint());
    // After expiry a fresh baseline must not replay either retained action.
    s.offer(700,1800);r.observe(s.frames,true,1800);s.acknowledge(r);require(r.active()&&r.held_sprint()&&!r.take(1900));
    s.event(ActionKind::stand_run_stop,710);s.offer(720,1820);r.observe(s.frames,true,1820);s.acknowledge(r);
    require(r.take(1910)->kind==ActionKind::stand_run_stop);require(!r.held_sprint());require(r.fault()==ActionFault::none);
}
void idle_and_overflow(){
    Stream s;ActionReceiver receiver;
    // The inactive worker intentionally acknowledges menus/pre-spawn batches;
    // the eventual baseline sees current metadata and no stored old requests.
    for(unsigned i=1;i<=600;++i){s.event(ActionKind::ground_step,i*20);s.offer(i*20,1000+i*20);
        require(s.frames.acknowledge(s.sequence));}
    receiver.observe(s.frames,true,13000);require(receiver.discarded()==600&&!receiver.take(13100));
    s.event(ActionKind::ground_jump_ready,12020);s.offer(12030,13030);receiver.observe(s.frames,true,13030);s.acknowledge(receiver);
    require(receiver.take(13120)->kind==ActionKind::ground_jump_ready);require(!receiver.take(13200));
    Stream unconsumed;for(unsigned i=1;i<=16;++i){unconsumed.event(ActionKind::ground_step,i);unconsumed.offer(i,1000+i);}
    require(unconsumed.source.capture(ActionKind::ground_step,17));const auto history=unconsumed.source.snapshot(17);
    require(unconsumed.frames.push({scope(),*history,17,1017})==ActionFramePush::failed);
    ActionReceiver bad;bad.observe(unconsumed.frames,true,1017);require(bad.fault()==ActionFault::overflow&&!bad.active());
}
void expired_rebaseline(){
    Stream s;ActionReceiver receiver;
    s.offer(0,1000);receiver.observe(s.frames,true,1000);s.acknowledge(receiver);
    require(!receiver.rebaselined());
    s.event(ActionKind::guard_enter,1);s.offer(1,1001);receiver.observe(s.frames,true,1001);s.acknowledge(receiver);
    require(receiver.take(1101)->kind==ActionKind::guard_enter&&receiver.held_guard());
    // Retain a queued attack, then let source events advance while the helper
    // offers no frames. The world never observes an inactive/stale callback.
    s.event(ActionKind::attack_combo1,2);s.offer(2,1002);receiver.observe(s.frames,true,1101);s.acknowledge(receiver);
    require(receiver.pending()==1);
    for(std::uint32_t i=3;i<=10;++i){s.event(ActionKind::attack_combo2,i);require(s.source.offered(i));}
    s.offer(1102,2102);require(s.frames.baseline_generation()==1);
    receiver.observe(s.frames,true,2102);s.acknowledge(receiver);
    require(receiver.rebaselined()&&receiver.restored()&&receiver.active()&&receiver.held_guard());
    require(receiver.pending()==0&&!receiver.take(2202)&&receiver.fault()==ActionFault::none);
    // The held flag is metadata only; neither old guard nor queued attacks
    // become a new native edge. The flag itself lasts exactly one observation.
    receiver.observe(s.frames,true,2202);require(!receiver.rebaselined()&&!receiver.restored());
    require(receiver.held_guard()&&!receiver.take(2202));
    s.event(ActionKind::guard_exit,1112);s.offer(1112,2112);receiver.observe(s.frames,true,2202);s.acknowledge(receiver);
    require(!receiver.rebaselined()&&!receiver.held_guard());
    require(receiver.take(2212)->kind==ActionKind::guard_exit&&!receiver.take(2212));
    // Duplicate/reordered frames cannot refresh freshness or cause a baseline.
    const auto duplicate=*s.frames.latest();require(s.frames.push(duplicate)==ActionFramePush::stale);
    auto reordered=duplicate;--reordered.packet_sequence;reordered.arrival_ms=4000;
    require(s.frames.push(reordered)==ActionFramePush::stale&&s.frames.baseline_generation()==1);
    receiver.observe(s.frames,true,2212);require(!receiver.rebaselined()&&!receiver.take(2212));

    // An explicit inactive callback and an unseen expiry have the same no-replay
    // result, including when the source guard exits and re-enters while paused.
    receiver.observe(s.frames,false,2213);require(!receiver.held_guard()&&!receiver.rebaselined());
    s.event(ActionKind::guard_enter2,1113);require(s.source.offered(12));
    for(std::uint32_t i=13;i<=20;++i){s.event(ActionKind::attack_combo3,1101+i);require(s.source.offered(i));}
    s.offer(2212,3212);receiver.observe(s.frames,false,3212);s.acknowledge(receiver);
    require(receiver.rebaselined()&&!receiver.active()&&!receiver.held_guard()&&!receiver.take(3212));
    receiver.observe(s.frames,true,3213);require(!receiver.rebaselined()&&receiver.restored()&&receiver.held_guard());
    require(!receiver.take(3313)&&receiver.fault()==ActionFault::none);
}
void short_pause_and_generation_contract(){
    Stream short_pause;ActionReceiver receiver;short_pause.offer(0,1000);receiver.observe(short_pause.frames,true,1000);short_pause.acknowledge(receiver);
    short_pause.event(ActionKind::ground_step,1);short_pause.offer(1,1500);
    require(short_pause.frames.baseline_generation()==0);receiver.observe(short_pause.frames,true,1500);short_pause.acknowledge(receiver);
    require(!receiver.rebaselined()&&!receiver.restored()&&receiver.take(1600)->kind==ActionKind::ground_step);
    // Exactly the expiry boundary still requires a complete source suffix.
    Stream gap;gap.offer(0,1000);
    for(std::uint32_t i=1;i<=8;++i){gap.event(ActionKind::attack_combo1,i);require(gap.source.offered(i));}
    require(gap.frames.push({scope(),*gap.source.snapshot(8),2,1500})==ActionFramePush::failed&&gap.frames.fault()==ActionFault::gap);
    // A copied older internal generation must not rearm an already rebound
    // receiver, even though its authenticated scope is otherwise identical.
    Stream generations;generations.offer(0,1000);const auto old=generations.frames;
    ActionReceiver checked;checked.observe(generations.frames,true,1000);
    generations.offer(501,1501);checked.observe(generations.frames,true,1501);require(checked.rebaselined());
    checked.observe(old,true,1502);require(checked.fault()==ActionFault::invalid&&!checked.active()&&!checked.rebaselined());
    // Receiving the first world callback after multiple wire-side baselines is
    // safe too: all retained history is discarded, even with guard held.
    Stream first;first.event(ActionKind::guard_enter3,1);first.offer(1,1000);
    first.offer(600,1600);first.offer(1200,2200);ActionReceiver late;late.observe(first.frames,true,2200);
    require(first.frames.baseline_generation()==2&&late.rebaselined()&&late.held_guard()&&!late.take(2300));
}
void contracts(){
    require(valid_event_queue(8,0,0)&&valid_event_queue(8,8,7));
    require(!valid_event_queue(0,0,0)&&!valid_event_queue(4097,0,0)&&!valid_event_queue(8,-1,0)&&
        !valid_event_queue(8,9,0)&&!valid_event_queue(8,0,-1)&&!valid_event_queue(8,0,8));
    require(action_binding("W_GroundJumpReady")->guest_id==989);
    require(action_binding(ActionKind::ground_jump_ready)->source_id==988);
    require(action_binding(ActionKind::attack_combo1)->source_id==3544&&action_binding(ActionKind::guard_exit)->guest_id==631);
    require(action_bindings.size()==action_kind_count);
    require(!action_binding("arbitrary-peer-event")&&!action_binding(ActionKind::none));
    for(const auto& b:action_bindings){require(action_binding(b.kind)==&b&&action_binding(b.source_name)==&b);}
    Stream s;ActionReceiver receiver;s.offer(0,1000);receiver.observe(s.frames,true,1000);
    auto changed=scope();changed.connection[0]++;
    require(s.frames.push({changed,*s.source.snapshot(1),2,1001})==ActionFramePush::failed);
    receiver.observe(s.frames,true,1001);require(receiver.fault()==ActionFault::scope_changed&&!receiver.take(1101));
}
}
int main(){try{lifecycle();idle_and_overflow();expired_rebaseline();short_pause_and_generation_contract();contracts();
    std::cout<<"PASS native action hand-off policy, expiry generations, idle discard, pause/stale baselines, held state and whitelist\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
