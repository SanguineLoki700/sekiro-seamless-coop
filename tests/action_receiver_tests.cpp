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
int main(){try{lifecycle();idle_and_overflow();contracts();
    std::cout<<"PASS native action hand-off policy, idle discard, pause/stale baselines, held state and whitelist\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
