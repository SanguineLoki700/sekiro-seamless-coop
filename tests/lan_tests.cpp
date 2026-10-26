#include "skcoop/lan/peer.hpp"
#include "skcoop/lan/scenario.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace skcoop::lan;
namespace {
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
Ticket ticket(){return {"127.0.0.1",0,random_id(),random_key()};}
Motion motion(float x=0){Motion m;m.epoch=1;m.position[0]=x;m.speed=.75f;return m;}
void deliver(Session& from,Session& to,std::uint64_t now){for(const auto& p:from.take_outgoing())require(to.receive(p,now),"Handshake step rejected");}
void authenticated_pair(Session& host,Session& guest){host.opened(1,random_id());guest.opened(1,{});deliver(host,guest,2);deliver(guest,host,3);deliver(host,guest,4);deliver(guest,host,5);deliver(host,guest,6);require(host.state()==State::connected&&guest.state()==State::connected,"Two-sided handshake incomplete");}
template<class F>void wait(F f){const auto end=GetTickCount64()+6000;while(GetTickCount64()<end){if(f())return;Sleep(5);}throw std::runtime_error("Required LAN result timed out");}
void codec(){
 auto t=ticket();t.port=28911;const auto text=ticket_text(t);require(parse_ticket(text)==t,"Ticket roundtrip");
 for(const auto bad:{"","SKL1|127.0.0.1|0|x|y","SKL1|999.1.1.1|12|x|y","SKL1|example.org|12|x|y"})require(!parse_ticket(bad),"Invalid room accepted");
 require(!valid_ipv4("127.1")&&!valid_ipv4("01.2.3.4")&&!valid_ipv4("224.1.2.3")&&valid_ipv4("192.168.1.2"),"IPv4 bounds");
 Packet p;p.kind=Kind::frame;p.room=t.room;p.connection=random_id();p.sender=1;p.receiver=2;p.sequence=1;p.motion=motion(3);
 const auto bytes=encode(p,t.key);const auto d=decode(bytes,t.room,t.key);require(d&&d.packet->motion==p.motion,"Frame roundtrip");
 for(std::size_t i=0;i<bytes.size();++i){auto corrupt=bytes;corrupt[i]^=1;require(!decode(corrupt,t.room,t.key),"Mutated packet authenticated");}
 require(!decode(bytes,random_id(),t.key)&&!decode(bytes,t.room,random_key()),"Wrong session/key accepted");
 for(std::size_t i=0;i<bytes.size();++i)require(!decode(std::span(bytes).first(i),t.room,t.key),"Truncated packet accepted");
 auto extra=bytes;extra.push_back(0);require(!decode(extra,t.room,t.key),"Trailing data accepted");
 auto invalid=motion();invalid.position[0]=std::numeric_limits<float>::quiet_NaN();require(!valid(invalid),"NaN motion accepted");invalid=motion();invalid.speed=1.1f;require(!valid(invalid),"Excessive graph speed accepted");
}
void state_machine(){
 auto t=ticket();Session a({Role::host,t,{},11}),b({Role::guest,t,{},22});authenticated_pair(a,b);
 a.send_frame(motion(5));auto packets=a.take_outgoing();require(packets.size()==1&&b.receive(packets[0],10),"Connected frame rejected");require(b.take_remote()->position[0]==5,"Wrong remote frame");
 require(!b.receive(packets[0],11)&&!b.take_remote(),"Replayed sequence applied");
 auto wrong=packets[0];wrong.connection=random_id();wrong.sequence+=100;require(!b.receive(wrong,12)&&!b.take_remote(),"Previous connection incarnation applied");
 b.tick(3000);require(b.state()==State::lost&&!b.take_remote(),"Missing peer did not expire");
 Session missing({Role::guest,t,{},33});missing.opened(0,{});missing.tick(16000);require(missing.state()==State::lost,"Absent server passed");
 Build native;native.purpose=Purpose::native_avatar;native.game[0]=1;native.scenario[0]=2;
 Session real({Role::host,t,native,44}),probe({Role::guest,t,{},55});real.opened(1,random_id());probe.opened(1,{});auto challenge=real.take_outgoing();require(!probe.receive(challenge[0],2)&&probe.failure()==Failure::compatibility,"Connection check admitted into native session");
 Session pending({Role::host,t,{},66}),guest({Role::guest,t,{},77});pending.opened(1,random_id());guest.opened(1,{});deliver(pending,guest,2);deliver(guest,pending,3);require(pending.state()!=State::connected,"Host declared success before acknowledgement");
}
void independent_profiles(){
 auto t=ticket();Build build;build.purpose=Purpose::native_avatar;build.adapter=independent_adapter;build.game[0]=1;build.scenario=independent_motion_scenario;
 Session host({Role::host,t,build,101}),guest({Role::guest,t,build,202});authenticated_pair(host,guest);
 Motion m=motion();m.descriptor=0xa000;if(build.adapter==action_adapter){ActionCapture capture(m.epoch);m.actions=*capture.snapshot(0);}host.send_frame(m);deliver(host,guest,20);require(guest.take_remote().has_value(),"Independent scenario refused movement");
 auto legacy=build;legacy.adapter=1;legacy.scenario[0]^=1;Session old({Role::host,t,legacy,303}),fresh({Role::guest,t,build,404});old.opened(1,random_id());fresh.opened(1,{});const auto packets=old.take_outgoing();require(!fresh.receive(packets[0],2)&&fresh.failure()==Failure::compatibility,"Legacy save fixture mixed with independent profile");
}
void smoothing(){
 MotionBuffer b;auto a=motion(0),c=motion(10);a.rotation[1]=3.1f;c.rotation[1]=-3.1f;a.angle=179;c.angle=-179;
 require(b.push(a,1000,1)==PushResult::accepted&&b.push(c,1200,3)==PushResult::accepted,"Buffer input rejected");
 require(b.push(motion(99),1100,2)==PushResult::stale,"Reordered snapshot applied");const auto m=b.sample(1200);require(m&&std::abs(m->motion.position[0]-5)<.001f&&std::abs(m->motion.rotation[1])>3,"Interpolation/angle wrap wrong");
 require(b.sample(1800)&&!b.sample(1800)->fresh&&b.sample(1800)->motion.speed==0,"Stale command did not release movement");
 c.epoch=2;require(b.push(c,1800,4)==PushResult::scene_changed,"Changed loading incarnation reused old actor");
}
Build action_build(){Build build;build.purpose=Purpose::native_avatar;build.adapter=action_adapter;build.game[0]=1;build.scenario[0]=2;return build;}
Motion action_motion(ActionCapture& capture,std::uint32_t ms,float x=0){
 const auto history=capture.snapshot(ms);require(history.has_value(),"Action source faulted");auto m=motion(x);m.descriptor=0xa000;m.epoch=history->epoch;m.actions=*history;return m;
}
void retag(std::vector<std::uint8_t>& bytes,const Key& key){
 BCRYPT_ALG_HANDLE alg{};require(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)>=0,"Test HMAC provider");Key tag{};
 const auto result=BCryptHash(alg,const_cast<PUCHAR>(key.data()),static_cast<ULONG>(key.size()),bytes.data(),static_cast<ULONG>(bytes.size()-tag_bytes),tag.data(),static_cast<ULONG>(tag.size()));
 BCryptCloseAlgorithmProvider(alg,0);require(result>=0,"Test HMAC calculation");std::copy(tag.begin(),tag.end(),bytes.end()-static_cast<std::ptrdiff_t>(tag_bytes));
}
void action_codec(){
 const auto t=ticket();ActionCapture capture(7);
 for(std::uint32_t i=1;i<=4;++i)require(capture.capture(static_cast<ActionKind>(i),i,static_cast<float>(i*30)),"Source event missing before frame offer");
 Packet p;p.kind=Kind::frame;p.room=t.room;p.connection=random_id();p.sender=1;p.receiver=2;p.sequence=9;p.motion=action_motion(capture,5);
 const auto bytes=encode(p,t.key);const auto decoded=decode(bytes,t.room,t.key);
 require(bytes.size()==244&&bytes.size()==header_bytes+motion_body_bytes+tag_bytes&&bytes.size()<=max_packet_bytes,"Action wire exceeds fixed packet bound");
 require(decoded&&decoded.packet->motion==p.motion,"Four-entry action history wire roundtrip");
 for(std::size_t i=0;i<bytes.size();++i){auto damaged=bytes;damaged[i]^=1;require(!decode(damaged,t.room,t.key),"Mutated action packet authenticated");}
 auto old=bytes;old[4]=1;require(decode(old,t.room,t.key).error==Error::version,"Wire1 admitted by wire2");
 const auto malformed=[&](std::size_t offset,std::uint8_t value,Error error){auto bad=bytes;bad[offset]=value;retag(bad,t.key);require(decode(bad,t.room,t.key).error==error,"Authenticated malformed action encoding accepted");};
 constexpr auto start=header_bytes+48;
 malformed(start,2,Error::format); // Enabled flag is not an extensible bit mask.
 malformed(start+1,5,Error::value);
 malformed(start+2,2,Error::format); // Unknown held-state flag.
 malformed(start+3,1,Error::format);
 malformed(start+2,1,Error::value); // Sprint state contradicts the retained stop/step event.
 malformed(start+4,5,Error::value); // Inconsistent latest/retained serials.
 malformed(start+12,3,Error::value); // Snapshot predates its final event.
 malformed(start+16+12,200,Error::value); // No raw/unknown native IDs.
 malformed(start+16+13,1,Error::format);
 malformed(start,0,Error::value); // Disabled history must be entirely zero.
 auto mismatched=p.motion;++mismatched.actions.epoch;require(!valid(mismatched),"Action epoch differs from motion epoch");
 auto inactive=motion();inactive.actions.source_ms=1;require(!valid(inactive),"Noncanonical disabled history accepted");
 ActionCapture empty(7);auto zero=action_motion(empty,0);require(valid(action_build(),zero),"Adapter3 zero-event initial snapshot refused");
 auto disabled=zero;disabled.actions={};require(valid(disabled)&&!valid(action_build(),disabled),"Adapter3 allowed absent action history");
 Build checker;require(!valid(checker,zero),"Checker allowed native action frame");
 auto legacy=action_build();legacy.adapter=2;require(valid(legacy,disabled)&&!valid(legacy,zero),"Legacy adapter silently accepted action semantics");
 auto unknown=action_build();unknown.adapter=4;require(!valid(unknown),"Unknown adapter accepted");
 ActionCapture sprint(7);require(sprint.capture(ActionKind::sprint_start_from_step,1),"Sprint state capture failed");p.motion=action_motion(sprint,2);const auto running=encode(p,t.key);const auto run=decode(running,t.room,t.key);
 require(running.size()==244&&running[start+2]==1&&run&&run.packet->motion.actions.sprinting&&run.packet->motion==p.motion,"Held sprint state did not survive fixed-size wire roundtrip");
 auto contradicted=running;contradicted[start+2]=0;retag(contradicted,t.key);require(decode(contradicted,t.room,t.key).error==Error::value,"Wire dropped sprint state while retaining sprint-start event");
 p.motion=disabled;const auto plain=encode(p,t.key);auto disabled_sprint=plain;disabled_sprint[start+2]=1;retag(disabled_sprint,t.key);require(decode(disabled_sprint,t.room,t.key).error==Error::value,"Disabled action history carried held sprint state");
}
void action_envelopes_and_views(){
 const auto t=ticket();const auto build=action_build();Session host({Role::host,t,build,101}),guest({Role::guest,t,build,202});authenticated_pair(host,guest);
 ActionCapture capture(7);MotionBuffer poses;ActionFrameBuffer histories;ActionTimeline timeline;
 const auto receive=[&](const Motion& m,std::uint64_t arrival){
  host.send_frame(m);const auto outgoing=host.take_outgoing();require(outgoing.size()==1,"Action frame not sent");const auto decoded=decode(encode(outgoing[0],t.key),t.room,t.key);require(static_cast<bool>(decoded),"Action frame failed codec");
  require(guest.receive(*decoded.packet,arrival),"Authenticated action frame rejected");
  const auto frame=guest.take_remote_frame();require(frame.has_value(),"Received action envelope metadata missing");
  require(frame->scope.room==t.room&&frame->scope.connection==outgoing[0].connection&&frame->scope.participant==101&&frame->scope.epoch==m.epoch,"Scope was not derived from accepted envelope");
  require(frame->packet_sequence==outgoing[0].sequence&&frame->arrival_ms==arrival,"Packet sequence/arrival not preserved");
  require(poses.push(frame->motion,frame->arrival_ms,frame->packet_sequence)==PushResult::accepted,"Pose buffer rejected action frame");
  const ActionFrame actions{frame->scope,frame->motion.actions,frame->packet_sequence,frame->arrival_ms};
  require(histories.push(actions)==ActionFramePush::accepted,"Authenticated action view rejected");return actions;
 };
 const auto initial=receive(action_motion(capture,0),1000);require(timeline.baseline(initial.scope,initial.history,initial.packet_sequence,initial.arrival_ms),"Timeline baseline failed");
 require(capture.capture(ActionKind::ground_step,10,90),"Step capture failed");const auto first=receive(action_motion(capture,10,1),1100);
 require(capture.capture(ActionKind::ground_jump_ready,20,-90),"Jump capture failed");const auto second=receive(action_motion(capture,20,2),1100);
 require(histories.pending().size()==2&&histories.pending()[0].arrival_ms==1100&&histories.pending()[1].arrival_ms==1100,"Same-arrival actions were overwritten");
 require(poses.sample(1200)->motion.actions==ActionSnapshot{},"Discrete action leaked into interpolated pose");
 for(const auto& frame:histories.pending())require(timeline.push(frame.scope,frame.history,frame.packet_sequence,frame.arrival_ms)==ActionPush::accepted,"Separated action batch lost");
 require(!timeline.take(1199),"Action fired before delayed pose");require(timeline.take(1200)==first.history.events[0],"First same-arrival event lost");require(timeline.take(1200)==second.history.events[1],"Second same-arrival event lost");require(!timeline.take(1200),"Action replayed");
 const auto refreshed=receive(action_motion(capture,200),1300);
 require(histories.latest()->arrival_ms==1300&&histories.pending().size()==2&&histories.pending()[0].arrival_ms==1100,"Repeated frame replaced original action arrival");
 require(histories.acknowledge(refreshed.packet_sequence)&&histories.pending().empty()&&histories.latest().has_value(),"Explicit hand-off ACK lost latest state");
 require(histories.push(first)==ActionFramePush::stale&&histories.pending().empty(),"Replay recreated acknowledged action batch");
 host.send_frame(action_motion(capture,201));auto packet=host.take_outgoing()[0];
 for(unsigned field=0;field<4;++field){auto replay=packet;if(field==0)replay.room=random_id();if(field==1)replay.connection=random_id();if(field==2)++replay.sender;if(field==3)++replay.receiver;require(!guest.receive(replay,1400)&&!guest.take_remote_frame(),"Wrong authenticated scope admitted");}
 require(guest.receive(packet,1401),"Valid scoped frame refused after replay attempts");const auto accepted=guest.take_remote_frame();require(accepted&&accepted->packet_sequence==packet.sequence,"Wrong sequence attached to accepted frame");
 require(!guest.receive(packet,1402)&&!guest.take_remote_frame(),"Sequence replay republished history");
 // New adapter cannot bypass action history via a generic, otherwise valid frame.
 packet.sequence++;packet.motion.actions={};require(!guest.receive(packet,1403)&&guest.failure()==Failure::protocol,"Adapter3 silently fell back to motion-only");
}
void action_view_bounds(){
 const auto t=ticket();ActionScope scope{t.room,random_id(),101,7};ActionCapture capture(7);ActionFrameBuffer buffer;
 const auto frame=[&](std::uint32_t ms,std::uint64_t sequence){return ActionFrame{scope,*capture.snapshot(ms),sequence,ms};};
 require(buffer.push(frame(0,1))==ActionFramePush::accepted,"Empty action baseline rejected");
 for(std::uint32_t i=1;i<=100;++i)require(buffer.push(frame(i,i+1))==ActionFramePush::accepted,"No-action snapshots filled action batch queue");
 require(buffer.pending().empty(),"No-action history created batches");
 for(std::uint32_t i=1;i<=action_queue_capacity+1;++i){
  require(capture.capture(ActionKind::ground_step,100+i),"Action flood capture failed");const auto current=frame(100+i,101+i);const auto result=buffer.push(current);require(capture.offered(current.history.latest),"Action offer watermark failed");
  require(result==(i<=action_queue_capacity?ActionFramePush::accepted:ActionFramePush::failed),"Unacknowledged action history silently overflowed");
 }
 require(buffer.fault()==ActionFault::overflow&&buffer.pending().empty(),"Action overflow is not visible/terminal");
 ActionFrameBuffer gap;ActionCapture source(7);require(gap.push({scope,*source.snapshot(0),1,0})==ActionFramePush::accepted,"Gap baseline");
 for(std::uint32_t i=1;i<=4;++i)require(source.capture(ActionKind::ground_step,i),"Gap source capture");require(source.offered(4),"Gap source offer");require(source.capture(ActionKind::ground_jump_ready,5),"Gap final capture");
 require(gap.push({scope,*source.snapshot(5),2,5})==ActionFramePush::failed&&gap.fault()==ActionFault::gap,"Missing retained action prefix not detected");
 for(unsigned field=0;field<4;++field){
  ActionFrameBuffer scoped;ActionCapture clean(7);const ActionFrame initial{scope,*clean.snapshot(0),1,0};require(scoped.push(initial)==ActionFramePush::accepted,"Scope baseline");
  auto wrong=initial;wrong.packet_sequence=2;wrong.arrival_ms=1;if(field==0)wrong.scope.room=random_id();if(field==1)wrong.scope.connection=random_id();if(field==2)++wrong.scope.participant;if(field==3){++wrong.scope.epoch;++wrong.history.epoch;}
  require(scoped.push(wrong)==ActionFramePush::failed&&scoped.fault()==ActionFault::scope_changed,"Old scope action replay accepted");
 }
 ActionFrameBuffer bad_ack;ActionCapture clean(7);require(bad_ack.push({scope,*clean.snapshot(0),1,0})==ActionFramePush::accepted,"ACK baseline");require(!bad_ack.acknowledge(2)&&bad_ack.fault()==ActionFault::invalid,"Future action ACK accepted");
 ActionFrameBuffer held;ActionCapture running(7);require(held.push({scope,*running.snapshot(0),1,0})==ActionFramePush::accepted,"Held-state baseline");require(running.capture(ActionKind::sprint_start_from_step,1),"Held sprint source");
 require(held.push({scope,*running.snapshot(10),2,10})==ActionFramePush::accepted,"Sprint-start batch");require(held.push({scope,*running.snapshot(100),3,100})==ActionFramePush::accepted,"Repeated sprint snapshot");
 require(held.latest()->history.sprinting&&held.pending().size()==1&&held.pending()[0].arrival_ms==10,"Held sprint repeat recreated event or replaced first arrival");require(held.acknowledge(3)&&held.pending().empty()&&held.latest()->history.sprinting,"ACK cleared held sprint state");
}
void action_loopback(){
 auto t=ticket();const auto build=action_build();Peer host({Role::host,t,build,101},"127.0.0.1");wait([&]{return host.read().status.state==State::listening;});t.port=host.read().status.local_port;Peer guest({Role::guest,t,build,202});
 ActionCapture a(7),b(7);const auto idle_a=action_motion(a,0),idle_b=action_motion(b,0);
 wait([&]{host.try_offer(idle_a);guest.try_offer(idle_b);const auto h=host.read(),g=guest.read();return h.status.state==State::connected&&g.status.state==State::connected&&h.actions.latest()&&g.actions.latest();});
 ActionTimeline at,bt;const auto hi=*host.read().actions.latest(),gi=*guest.read().actions.latest();require(at.baseline(hi.scope,hi.history,hi.packet_sequence,hi.arrival_ms)&&bt.baseline(gi.scope,gi.history,gi.packet_sequence,gi.arrival_ms),"Actual socket action baselines missing");
 require(a.capture(ActionKind::stand_move_start,1,15),"Host logical action capture");require(b.capture(ActionKind::ground_step,1,45)&&b.capture(ActionKind::ground_jump_ready,2,-90),"Guest sub-frame actions lost");const auto ma=action_motion(a,2,1),mb=action_motion(b,2,2);
 auto disabled=ma;disabled.actions={};require(!host.try_offer(disabled),"Native action socket accepted disabled action history");
 std::vector<ActionEvent> seen_a,seen_b;
 const auto drain=[&](Peer& peer,ActionTimeline& timeline,std::vector<ActionEvent>& seen){
  const auto view=peer.read();require(view.status.failure==Failure::none&&view.actions.fault()==ActionFault::none,"Actual socket action view faulted");
  for(const auto& frame:view.actions.pending()){const auto result=timeline.push(frame.scope,frame.history,frame.packet_sequence,frame.arrival_ms);require(result==ActionPush::accepted||result==ActionPush::stale,"Actual socket action batch rejected");}
  if(const auto& latest=view.actions.latest();latest){const auto result=timeline.push(latest->scope,latest->history,latest->packet_sequence,latest->arrival_ms);require(result==ActionPush::accepted||result==ActionPush::stale,"Actual socket action freshness update rejected");peer.try_ack_actions(latest->packet_sequence);}
  while(const auto event=timeline.take(GetTickCount64()))seen.push_back(*event);
 };
 wait([&]{if(host.try_offer(ma))require(a.offered(ma.actions.latest),"Host offer watermark");if(guest.try_offer(mb))require(b.offered(mb.actions.latest),"Guest offer watermark");drain(host,at,seen_a);drain(guest,bt,seen_b);return seen_a.size()==2&&seen_b.size()==1;});
 require(seen_a[0]==mb.actions.events[0]&&seen_a[1]==mb.actions.events[1]&&seen_b[0]==ma.actions.events[0],"Actual socket actions were lost/duplicated/changed");
 for(unsigned i=0;i<10;++i){drain(host,at,seen_a);drain(guest,bt,seen_b);Sleep(5);}require(seen_a.size()==2&&seen_b.size()==1,"Repeated socket histories retriggered actions");guest.stop();host.stop();
 // A runtime that never acknowledges action batches fails visibly, rather
 // than having its unread event history overwritten in the socket worker.
 auto room=ticket();Peer sender({Role::host,room,build,303},"127.0.0.1");wait([&]{return sender.read().status.state==State::listening;});room.port=sender.read().status.local_port;Peer receiver({Role::guest,room,build,404});ActionCapture flood(7);const auto idle=action_motion(flood,0);
 wait([&]{sender.try_offer(idle);return receiver.read().actions.latest().has_value();});
 for(std::uint32_t i=1;i<=action_queue_capacity+1;++i){require(flood.capture(ActionKind::ground_step,i),"Flood source overflow despite successful handoffs");const auto next=action_motion(flood,i);wait([&]{if(!sender.try_offer(next))return false;return flood.offered(next.actions.latest);});wait([&]{const auto view=receiver.read();return i<=action_queue_capacity?(view.actions.latest()&&view.actions.latest()->history.latest==i):view.status.state==State::failed;});}
 wait([&]{return receiver.read().status.state==State::failed;});const auto failed=receiver.read();require(failed.status.failure==Failure::protocol&&failed.status.action_fault==ActionFault::overflow&&failed.actions.fault()==ActionFault::overflow,"Socket history overflow was hidden");receiver.stop();sender.stop();
}
void loopback(){
 auto t=ticket();Peer host({Role::host,t,{},101},"127.0.0.1");wait([&]{return host.read().status.state==State::listening;});t.port=host.read().status.local_port;
 Peer guest({Role::guest,t,{},202});
 wait([&]{host.try_offer(motion(2));guest.try_offer(motion(4));const auto a=host.read(),b=guest.read();return a.status.state==State::connected&&b.status.state==State::connected&&a.status.frames_received>=4&&b.status.frames_received>=4;});
 require(host.read().remote.sample(GetTickCount64())->motion.position[0]==4,"Host did not receive actual guest frame");
 require(guest.read().remote.sample(GetTickCount64())->motion.position[0]==2,"Guest did not receive actual host frame");
 guest.stop();wait([&]{return host.read().status.state!=State::connected;});require(host.read().status.state==State::lost||host.read().status.state==State::failed,"Client termination hidden");host.stop();
 auto bad=ticket();Peer server({Role::host,bad,{},303},"127.0.0.1");wait([&]{return server.read().status.state==State::listening;});bad.port=server.read().status.local_port;bad.key=random_key();Peer wrong({Role::guest,bad,{},404});
 wait([&]{return wrong.read().status.state==State::failed;});require(wrong.read().status.failure==Failure::authentication&&wrong.read().status.frames_received==0,"Wrong-key socket connected");wrong.stop();server.stop();
 auto scene=ticket();Peer x({Role::host,scene,{},505},"127.0.0.1");wait([&]{return x.read().status.state==State::listening;});scene.port=x.read().status.local_port;Peer y({Role::guest,scene,{},606});
 wait([&]{x.try_offer(motion());y.try_offer(motion());return x.read().status.frames_received>=2&&y.read().status.frames_received>=2;});auto changed=motion();changed.epoch=2;y.try_offer(changed);
 wait([&]{return x.read().status.state==State::failed;});require(x.read().status.failure==Failure::scene,"Loading change not reported");y.stop();x.stop();
}
}
int main(){try{codec();state_machine();smoothing();independent_profiles();action_codec();action_envelopes_and_views();action_view_bounds();loopback();action_loopback();std::cout<<"PASS wire2 fixed action history, authenticated metadata, scope/replay/lifetime guards, bounded views and actual loopback actions\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
