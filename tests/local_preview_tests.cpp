#include "skcoop/lan/local_preview.hpp"
#include "skcoop/lan/scenario.hpp"
#include "../src/runtime/action_receiver.h"
#include <Windows.h>
#include <iostream>
#include <stdexcept>
using namespace skcoop::lan;
namespace {
void check(bool result,int line){if(!result)throw std::runtime_error("Local preview invariant at "+std::to_string(line));}
#define require(x) check((x),__LINE__)
ActionScope scope(){ActionScope s;s.room[0]=1;s.connection[0]=2;s.participant=101;s.epoch=7;return s;}
Motion source(){Motion m;m.epoch=7;m.descriptor=0xa000;m.position={5,0,5};m.rotation={0,1,0};m.speed=.5f;m.angle=30;m.actions.epoch=7;return m;}
View frame(const Motion& motion,std::uint64_t sequence,std::uint64_t time,ActionScope s=scope()){
    View view;view.status.state=State::connected;
    require(view.remote.push(motion,time,sequence)==PushResult::accepted);
    require(view.actions.push({s,motion.actions,sequence,time})==ActionFramePush::accepted);return view;
}
LocalPreviewMirror running(){LocalPreviewMirror mirror(0);const auto origin=source();require(!mirror.step(frame(origin,1,1000),1000).motion);
    auto separated=origin;separated.position[2]+=2;require(mirror.step(frame(separated,2,1050),1050).motion.has_value());return mirror;}
void policy(){
    LocalPreviewMirror mirror(0);View empty;require(!mirror.step(empty,0).motion&&mirror.phase()==LocalPreviewPhase::waiting_frame);
    auto m=source();auto result=mirror.step(frame(m,1,1000),1000);require(!result.motion&&result.acknowledge==1&&mirror.phase()==LocalPreviewPhase::waiting_separation);
    auto close=m;close.position[2]+=.5f;result=mirror.step(frame(close,2,1050),1050);require(!result.motion&&result.acknowledge==2);
    auto separated=m;separated.position[2]+=2;result=mirror.step(frame(separated,3,1100),1100);
    require(result.motion&&result.motion->position==m.position&&mirror.phase()==LocalPreviewPhase::active);
    ActionCapture capture(7);require(capture.capture(ActionKind::attack_combo1,1));require(capture.capture(ActionKind::guard_enter,2));
    auto next=separated;next.position[0]++;next.actions=*capture.snapshot(3);require(next.actions.guarding);
    const auto input=frame(next,4,1150);result=mirror.step(input,1150);
    require((result.motion&&result.motion->position==std::array<float,3>{6,0,5}));
    require(result.motion->actions==next.actions&&result.motion->rotation==next.rotation&&result.motion->speed==next.speed&&result.motion->angle==next.angle);
    require(input.remote.sample(1200)->motion.actions==ActionSnapshot{}&&result.motion->actions!=ActionSnapshot{});
    result=mirror.step(input,1200);require(result.motion&&result.motion->actions==next.actions); // Repeated snapshots preserve serials.
    next.position[0]+=7;result=mirror.step(frame(next,5,1250),1250);
    require(!result.motion&&result.acknowledge==5&&mirror.phase()==LocalPreviewPhase::path_limit&&!finished(mirror.phase()));
    auto returned=separated;returned.position[0]++;
    require(!mirror.step(frame(returned,6,1300),1300).motion); // Receiver freshness lease must expire first.
    result=mirror.step(frame(returned,7,2350),2350);
    require((result.motion&&result.motion->position==std::array<float,3>{6,0,5}&&mirror.phase()==LocalPreviewPhase::active));
    // Same measured origin/offset and original active deadline survive recovery.
    require(!mirror.step(frame(returned,8,91100),91100).motion&&mirror.phase()==LocalPreviewPhase::time_limit);
}
void guards(){
    auto m=source();
    {auto mirror=running();const auto stale=mirror.step(frame(m,3,1100),1601);
     require(!stale.motion&&stale.acknowledge==3&&mirror.phase()==LocalPreviewPhase::stale_source&&!finished(mirror.phase()));
     auto resume=m;resume.position[2]+=2;resume.position[0]++;
     const auto fresh=mirror.step(frame(resume,4,1650),1650);
     require((fresh.motion&&fresh.motion->position==std::array<float,3>{6,0,5}&&mirror.phase()==LocalPreviewPhase::active));
     // Stale/resume cannot reset the original ninety-second bound.
     require(!mirror.step(frame(resume,5,91050),91050).motion&&mirror.phase()==LocalPreviewPhase::time_limit);}
    {LocalPreviewMirror mirror(0);const auto old=mirror.step(frame(m,1,1000),1601);
     require(!old.motion&&old.acknowledge==1&&mirror.phase()==LocalPreviewPhase::stale_source);
     require(!mirror.step(frame(m,2,1650),1650).motion&&mirror.phase()==LocalPreviewPhase::waiting_separation);}
    {auto mirror=running();require(!mirror.step(frame(m,3,1100),1049).motion&&mirror.phase()==LocalPreviewPhase::failed);}
    {auto mirror=running();require(!mirror.step(frame(m,3,1100),1100-1).motion&&mirror.phase()==LocalPreviewPhase::failed);}
    {auto mirror=running();auto changed=scope();changed.connection[0]++;
     require(!mirror.step(frame(m,3,1100,changed),1100).motion&&mirror.phase()==LocalPreviewPhase::failed);}
    {auto mirror=running();auto bad=frame(m,3,1100);auto later=m;later.position[0]++;
     require(bad.remote.push(later,1100,4)==PushResult::accepted);
     require(!mirror.step(bad,1100).motion&&mirror.phase()==LocalPreviewPhase::failed);}
    {auto mirror=running();ActionCapture capture(7);require(capture.capture(ActionKind::guard_enter,1));
     auto different=*capture.snapshot(2);View mismatch;mismatch.status.state=State::connected;
     require(mismatch.remote.push(m,1100,3)==PushResult::accepted);require(mismatch.actions.push({scope(),different,3,1100})==ActionFramePush::accepted);
     require(!mirror.step(mismatch,1100).motion&&mirror.phase()==LocalPreviewPhase::failed);}
    {auto mirror=running();auto changed=m;changed.epoch++;changed.actions.epoch++;auto s=scope();s.epoch++;
     require(!mirror.step(frame(changed,3,1100,s),1100).motion&&mirror.phase()==LocalPreviewPhase::failed);}
    {auto mirror=running();auto bad=frame(m,3,1100);bad.status.state=State::lost;bad.status.failure=Failure::timeout;
     require(!mirror.step(bad,1100).motion&&mirror.phase()==LocalPreviewPhase::source_ended);}
    {auto mirror=running();auto bad=frame(m,3,1100);bad.status.action_fault=ActionFault::overflow;
     require(!mirror.step(bad,1100).motion&&mirror.phase()==LocalPreviewPhase::failed);}
    {auto mirror=running();require(!mirror.step(frame(m,3,91050),91050).motion&&mirror.phase()==LocalPreviewPhase::time_limit);}
    {LocalPreviewMirror mirror(0);View waiting;require(!mirror.step(waiting,360000).motion&&mirror.phase()==LocalPreviewPhase::time_limit);}
    {LocalPreviewMirror mirror(0);mirror.step(frame(m,1,1000),1000);auto distant=m;distant.position[0]+=7;
     require(!mirror.step(frame(distant,2,1050),1050).motion&&mirror.phase()==LocalPreviewPhase::path_limit);
     auto separated=m;separated.position[2]+=2;
     require(!mirror.step(frame(separated,3,1100),1100).motion);
     const auto resumed=mirror.step(frame(separated,4,2150),2150);
     require(resumed.motion&&resumed.motion->position==m.position);}
    {auto mirror=running();auto distant=m;distant.position[0]+=7;
     const auto outside=mirror.step(frame(distant,3,1100),1100);
     require(!outside.motion&&outside.acknowledge==3);
     distant.position[0]+=1;
     require(!mirror.step(frame(distant,4,2300),2300).motion); // Never clamp an outside pose.
     auto returned=m;returned.position[2]+=2;
     require(mirror.step(frame(returned,5,2350),2350).motion.has_value());
     returned.position[0]+=7;require(!mirror.step(frame(returned,6,2400),2400).motion);
     returned.position[0]-=7;require(!mirror.step(frame(returned,7,2450),2450).motion); // Each pause must expire.
     require(mirror.step(frame(returned,8,3500),3500).motion.has_value());}
    {auto mirror=running();auto edge=m;edge.position[2]+=2;edge.position[0]+=6;
     require(mirror.step(frame(edge,3,1100),1100).motion.has_value()); // Six metres is inside.
     edge.position[0]+=.01f;const auto outside=mirror.step(frame(edge,4,1150),1150);
     require(!outside.motion&&outside.acknowledge==4&&mirror.phase()==LocalPreviewPhase::path_limit);}
    require(finished(LocalPreviewPhase::time_limit)&&finished(LocalPreviewPhase::stopped)&&!finished(LocalPreviewPhase::active)&&!finished(LocalPreviewPhase::path_limit));
    require(local_preview_phase_name(LocalPreviewPhase::path_limit)=="path_paused");
}
void resumed_actions_are_baseline(){
    auto mirror=running();auto m=source();m.position[2]+=2;
    ActionFrameBuffer destination;skcoop::actors::ActionReceiver receiver,missed_pause;
    auto forward=[&](const Motion& motion,std::uint64_t sequence,std::uint64_t time){
        const auto next=mirror.step(frame(motion,sequence,time),time);require(next.motion.has_value());
        require(destination.push({scope(),next.motion->actions,sequence,time})==ActionFramePush::accepted);
        receiver.observe(destination,true,time);missed_pause.observe(destination,true,time);
        require(destination.acknowledge(receiver.acknowledgement()));
    };
    forward(m,3,1100); // Initial neutral baseline.
    ActionCapture capture(7);require(capture.capture(ActionKind::attack_combo1,10));
    require(capture.capture(ActionKind::guard_enter,20));m.actions=*capture.snapshot(30);forward(m,4,1150);
    auto event=receiver.take(1250);require(event&&event->kind==ActionKind::attack_combo1);
    event=receiver.take(1250);require(event&&event->kind==ActionKind::guard_enter);require(receiver.held_guard());
    event=missed_pause.take(1250);require(event&&event->kind==ActionKind::attack_combo1);
    event=missed_pause.take(1250);require(event&&event->kind==ActionKind::guard_enter);
    require(capture.offered(m.actions.latest));m.position[0]+=7;
    for(unsigned i=0;i<8;++i){
        const auto kind=i==0?ActionKind::guard_exit:(i==7?ActionKind::guard_enter:ActionKind::attack_combo1);
        require(capture.capture(kind,40+i*10));m.actions=*capture.snapshot(40+i*10);
        const auto paused=mirror.step(frame(m,5+i,1300+i*100),1300+i*100);
        require(!paused.motion&&paused.acknowledge==5+i);require(capture.offered(m.actions.latest));
    }
    receiver.observe(destination,true,2100);require(!receiver.active()&&!receiver.held_guard());
    m.position[0]-=7;m.actions=*capture.snapshot(1200);forward(m,13,2450);
    // More than four genuine source events happened while no pose was offered.
    // The expired destination frame is explicitly rebased, not replayed as a
    // new attack/deflect queue. Native held-guard restoration requires rearm.
    require(receiver.fault()==ActionFault::none&&receiver.restored()&&receiver.pending()==0);
    require(!receiver.take(2550));
    // A world callback may itself have paused. Generation still forces the
    // same discard/release boundary without requiring observation of old age.
    require(missed_pause.fault()==ActionFault::none&&missed_pause.rebaselined()&&missed_pause.pending()==0);
    require(!missed_pause.take(2550));
    require(capture.capture(ActionKind::guard_exit,1210));m.actions=*capture.snapshot(1220);forward(m,14,2600);
    event=receiver.take(2700);require(event&&event->kind==ActionKind::guard_exit); // A real new edge remains deliverable.
}
template<class F>void wait(F ready,const char* stage){const auto end=GetTickCount64()+6000;while(GetTickCount64()<end){if(ready())return;Sleep(5);}throw std::runtime_error(std::string("Loopback preview result timed out: ")+stage);}
void actual_loopback(){
    Ticket ticket{"127.0.0.1",0,random_id(),random_key()};Peer host({Role::host,ticket,local_preview_build(),101},"127.0.0.1");
    wait([&]{return host.read().status.state==State::listening;},"host listen");ticket.port=host.read().status.local_port;
    LocalPreview preview(ticket);auto m=source();
    wait([&]{host.try_offer(m);return preview.read().phase==LocalPreviewPhase::waiting_separation;},"source origin");
    require(host.read().status.frames_received==0); // Helper never sends a guessed spawn.
    m.position[2]+=2;ActionCapture capture(7);require(capture.capture(ActionKind::attack_combo1,1));m.actions=*capture.snapshot(2);
    wait([&]{host.try_offer(m);const auto v=host.read();if(!v.actions.latest())return false;host.try_ack_actions(v.actions.latest()->packet_sequence);
        return preview.read().phase==LocalPreviewPhase::active&&v.remote.newest()&&v.remote.newest()->motion.position==source().position&&v.actions.latest()->history==m.actions;},"attack mirror");
    require(capture.offered(m.actions.latest));require(capture.capture(ActionKind::guard_enter,3));m.actions=*capture.snapshot(4);m.position[0]++;
    wait([&]{host.try_offer(m);const auto v=host.read();if(!v.actions.latest())return false;host.try_ack_actions(v.actions.latest()->packet_sequence);
        return v.actions.latest()->history==m.actions&&v.remote.newest()->motion.position==std::array<float,3>{6,0,5};},"guard mirror");
    const auto before=preview.read();require(before.offers&&before.received&&before.sent&&before.mirrored_serial==2);
    m.position[0]+=7;
    wait([&]{host.try_offer(m);return preview.read().phase==LocalPreviewPhase::path_limit;},"path pause");
    const auto paused=preview.read();const auto paused_at=GetTickCount64();unsigned discarded{};
    while(GetTickCount64()-paused_at<1200){
        if(discarded<8&&GetTickCount64()-paused_at>=discarded*120){
            require(capture.capture(ActionKind::attack_combo1,10+discarded*10));++discarded;
            m.actions=*capture.snapshot(10+(discarded-1)*10);
        }
        if(host.try_offer(m))require(capture.offered(m.actions.latest));
        const auto view=host.read();if(view.actions.latest())host.try_ack_actions(view.actions.latest()->packet_sequence);
        require(view.status.state==State::connected&&preview.read().offers==paused.offers);Sleep(10);
    }
    require(discarded==8&&preview.read().path_pauses==1);
    m.position[0]-=7;
    wait([&]{host.try_offer(m);const auto view=host.read();if(view.actions.latest())host.try_ack_actions(view.actions.latest()->packet_sequence);
        return preview.read().phase==LocalPreviewPhase::active&&view.actions.latest()&&view.actions.latest()->history==m.actions;},"resume after discarded actions");
    require(host.read().actions.fault()==ActionFault::none&&preview.read().mirrored_serial==10);
    preview.stop();require(preview.read().phase==LocalPreviewPhase::stopped);
    wait([&]{const auto state=host.read().status.state;return state==State::lost||state==State::failed;},"helper close");host.stop();
    ticket.address="192.168.1.1";bool refused=false;try{LocalPreview invalid(ticket);}catch(const std::invalid_argument&){refused=true;}require(refused);
}
}
int main(){try{policy();guards();resumed_actions_are_baseline();actual_loopback();std::cout<<"PASS local combat mirror bounds, freshness/scope checks, discarded pause backlog and owned loopback lifecycle; no game process used\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
