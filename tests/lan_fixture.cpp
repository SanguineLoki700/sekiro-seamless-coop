// Explicit scripted peer for one-game adapter validation. This is not evidence
// of two real games or two machines, and is never shipped in the owner kit.
#include <Windows.h>
#include "skcoop/lan/peer.hpp"
#include "skcoop/lan/scenario.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
#include <string_view>
using namespace skcoop::lan;
bool digest(std::string_view s,skcoop::core::Sha256& d){if(s.size()!=64)return false;
 const auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
 for(std::size_t i=0;i<32;++i){const int a=digit(s[2*i]),b=digit(s[2*i+1]);if(a<0||b<0)return false;d[i]=static_cast<std::uint8_t>((a<<4)|b);}return true;}
int wmain(int argc,wchar_t** argv){try{if(argc!=3&&argc!=4)return 2;const bool independent=argc==4;
 if(independent&&std::wstring_view(argv[3])!=L"--independent")return 2;
 std::ifstream in(argv[1],std::ios::binary);std::string role,code,game,scenario,extra;
 std::getline(in,role);std::getline(in,code);std::getline(in,game);std::getline(in,scenario);
 if(!in||std::getline(in,extra)||code.size()>159||(role!="host"&&role!="guest"))return 2;
 SessionConfig c;c.role=role=="host"?Role::host:Role::guest;const auto t=parse_ticket(code);if(!t)return 2;c.ticket=*t;c.participant=random_participant();c.build.purpose=Purpose::native_avatar;
 if(!digest(game,c.build.game)||!digest(scenario,c.build.scenario))return 2;
 if(independent){if(c.build.scenario!=independent_motion_scenario)return 2;c.build.adapter=independent_adapter;}
 Peer peer(c);const auto start=GetTickCount64();std::uint64_t first{},first_origin_at{},frames{},fresh{};Motion origin;Status status;
 bool failed=false,origin_captured=false,source_changed=false;
 while(GetTickCount64()-start<150000){const auto now=GetTickCount64();const auto view=peer.read();status=view.status;
  if(status.state==State::failed||status.state==State::lost){failed=true;break;}
  if(independent&&view.actions.latest())peer.try_ack_actions(view.actions.latest()->packet_sequence);
  const auto remote=view.remote.sample(now);
  if(remote&&remote->fresh){++fresh;
   if(!origin_captured){origin=remote->motion;origin_captured=true;first_origin_at=now-start;
    if(independent){origin.speed=0;origin.angle=0;std::cout<<"{\"first_origin_captured\":true,\"waiting_for_separation\":true}\n"<<std::flush;}
    else{first=now;origin.epoch=1;origin.position[0]+=3;}}
   if(independent){
    if(remote->motion.epoch!=origin.epoch||remote->motion.descriptor!=origin.descriptor){source_changed=true;failed=true;break;}
    if(!first){const auto& p=remote->motion.position;const auto& q=origin.position;
     const auto gap=std::hypot(p[0]-q[0],p[1]-q[1],p[2]-q[2]);
     if(gap>=1.25f&&gap<=10.f){first=now;std::cout<<"{\"waiting_for_separation\":false,\"separation_observed\":true}\n"<<std::flush;}}
   }
  }
  if(first){auto m=origin;const auto elapsed=now-first;
   if(independent){m.actions={};m.actions.epoch=m.epoch;m.actions.source_ms=static_cast<std::uint32_t>(elapsed);}
   // Independent mode stays at a previously observed player position. It never
   // guesses an offset into geometry; the owner first walks away from that pose.
   // Legacy mode retains its explicit independent scripted path.
   if(!independent){m.position[2]+=.8f*static_cast<float>(std::sin(static_cast<double>(elapsed)*.0004));m.speed=elapsed<35000?.5f:0.f;m.angle=0;}
   peer.try_offer(m);++frames;if(elapsed>=45000)break;}
  Sleep(20);
 }
 const bool complete=first&&GetTickCount64()-first>=45000&&!failed&&status.frames_sent>20&&status.frames_received>20;
 peer.stop();std::ofstream out(argv[2],std::ios::binary);out<<"{\"kind\":\"scripted-lan-peer\",\"two_game_acceptance\":false,\"completed\":"<<(complete?"true":"false")<<",\"state\":"<<static_cast<int>(status.state)<<",\"error\":"<<static_cast<int>(status.failure)<<",\"frames_sent\":"<<status.frames_sent<<",\"frames_received\":"<<status.frames_received<<",\"fresh_samples\":"<<fresh<<",\"offered_samples\":"<<frames
 <<",\"independent\":"<<(independent?"true":"false")<<",\"first_origin_captured\":"<<(origin_captured?"true":"false")<<",\"first_origin_at_ms\":"<<first_origin_at<<",\"waiting_for_separation\":"<<(independent&&origin_captured&&!first&&!source_changed?"true":"false")<<",\"separation_observed\":"<<(independent&&first?"true":"false")<<",\"source_changed\":"<<(source_changed?"true":"false")<<"}\n";
 return complete&&out?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
