#include "skcoop/lan/peer.hpp"
#include "skcoop/lan/scenario.hpp"
#include <Windows.h>
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
 Motion m=motion();m.descriptor=0xa000;host.send_frame(m);deliver(host,guest,20);require(guest.take_remote().has_value(),"Independent scenario refused movement");
 auto legacy=build;legacy.adapter=1;legacy.scenario[0]^=1;Session old({Role::host,t,legacy,303}),fresh({Role::guest,t,build,404});old.opened(1,random_id());fresh.opened(1,{});const auto packets=old.take_outgoing();require(!fresh.receive(packets[0],2)&&fresh.failure()==Failure::compatibility,"Legacy save fixture mixed with independent profile");
}
void smoothing(){
 MotionBuffer b;auto a=motion(0),c=motion(10);a.rotation[1]=3.1f;c.rotation[1]=-3.1f;a.angle=179;c.angle=-179;
 require(b.push(a,1000,1)==PushResult::accepted&&b.push(c,1200,3)==PushResult::accepted,"Buffer input rejected");
 require(b.push(motion(99),1100,2)==PushResult::stale,"Reordered snapshot applied");const auto m=b.sample(1200);require(m&&std::abs(m->motion.position[0]-5)<.001f&&std::abs(m->motion.rotation[1])>3,"Interpolation/angle wrap wrong");
 require(b.sample(1800)&&!b.sample(1800)->fresh&&b.sample(1800)->motion.speed==0,"Stale command did not release movement");
 c.epoch=2;require(b.push(c,1800,4)==PushResult::scene_changed,"Changed loading incarnation reused old actor");
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
int main(){try{codec();state_machine();smoothing();independent_profiles();loopback();std::cout<<"PASS authenticated framing, incarnation/replay rejection, stale-motion release and actual loopback peers\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
