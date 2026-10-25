#include "remote_actor.h"
#include "lan_payload.h"
#include <iostream>
#include <stdexcept>
using namespace skcoop::actors;
namespace {
void check(bool v,int line){if(!v)throw std::runtime_error("Remote actor safety invariant failed at line "+std::to_string(line));}
#define require(v) check((v),__LINE__)
struct Fake:RemoteGame {
 Frame frame{1,2,3,0,0xa000,true,{0,0,0,1},{0,0,1,0},1000};Vector guest{};unsigned creates{},writes{},local_commands{},remote_commands{};float speed{};
 bool sample(Frame& f)override{f=frame;return true;}Token create()override{++creates;frame.guest=4;return 4;}
 bool actor_ready(Token a)override{require(a==4);return true;}Vector position(Token a)override{require(a==4);return guest;}
 void place(Token a,const Vector& p)override{require(a==4);guest=p;++writes;}
 void command_locomotion(Token,bool)override{++local_commands;throw std::runtime_error("Local fallback must never run");}
 void remote_motion(Token a,const skcoop::lan::Motion& m)override{require(a==4);speed=m.speed;++remote_commands;}
 void tick(RemoteActor& d,const std::optional<skcoop::lan::Smoothed>& m,skcoop::lan::State s=skcoop::lan::State::connected,unsigned n=1){while(n--){frame.milliseconds+=16;d.tick(*this,frame,true,true,s,m);}}
};
skcoop::lan::Smoothed remote(){skcoop::lan::Motion m;m.epoch=9;m.descriptor=0xa000;m.position={0,0,2};m.speed=.75;return {m,0,true};}
}
int main(){try{
 {skcoop::launch::Payload p{};require(valid_lan_payload(p));p.lan_role=1;require(!valid_lan_payload(p));p.actor_test=skcoop::launch::ActorTest::lan;require(!valid_lan_payload(p));
  p.scenario_sha256[0]=1;const auto code=skcoop::lan::ticket_text({"127.0.0.1",24860,skcoop::lan::random_id(),skcoop::lan::random_key()});std::copy(code.begin(),code.end(),p.lan_ticket);require(valid_lan_payload(p));
  p.lan_ticket[159]='x';require(!valid_lan_payload(p));}
 {Fake g;RemoteActor d;g.tick(d,{},skcoop::lan::State::connected,300);require(g.creates==0&&g.writes==0&&g.local_commands==0);}
 {Fake g;RemoteActor d;auto m=remote();g.tick(d,m,skcoop::lan::State::connecting,300);require(g.creates==0);m.motion.position={0,0,0};g.tick(d,m);require(g.creates==0&&d.waiting==RemoteWait::separation);m.motion.position={0,0,30};g.tick(d,m);require(g.creates==0&&d.waiting==RemoteWait::area);}
 {Fake g;RemoteActor d;auto m=remote();g.tick(d,m,skcoop::lan::State::connected,62);require(d.phase==Phase::following&&g.creates==1&&g.writes==1);
  m.motion.position[0]=1;g.tick(d,m);require(g.guest[0]==1&&g.speed==.75f&&g.local_commands==0);
  const auto writes=g.writes;m.fresh=false;g.tick(d,m,skcoop::lan::State::connected,30);require(g.speed==0&&g.writes==writes);
  m.fresh=true;g.tick(d,m);require(g.speed==.75f);g.tick(d,m,skcoop::lan::State::lost);require(d.phase==Phase::stopped&&g.speed==0);
  g.tick(d,m,skcoop::lan::State::connected,50);require(g.creates==1&&g.speed==0);}
 {Fake g;RemoteActor d;auto m=remote();g.tick(d,m,skcoop::lan::State::connected,62);const auto commands=g.remote_commands;++g.frame.primary;g.tick(d,m);require(d.phase==Phase::stopped&&g.remote_commands==commands);}
 {Fake g;RemoteActor d;auto m=remote();g.tick(d,m,skcoop::lan::State::connected,62);m.motion.epoch++;g.tick(d,m);require(d.phase==Phase::stopped&&g.speed==0);}
 {Fake g;RemoteActor d;auto m=remote();g.tick(d,m,skcoop::lan::State::connected,62);d.stop_requested=true;g.tick(d,m);require(d.phase==Phase::stopped&&g.speed==0);}
 {Fake g;RemoteActor d;auto m=remote();g.tick(d,m);m.motion.position[0]=100;g.tick(d,m);require(d.phase==Phase::failed&&g.writes==0);}
 std::cout<<"PASS absent-peer refusal, independent remote commands, stale release and scene lifetime\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

