// Explicit scripted peer for one-game adapter validation. This is not evidence
// of two real games or two machines, and is never shipped in the owner kit.
#include <Windows.h>
#include "skcoop/lan/peer.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
using namespace skcoop::lan;
bool digest(std::string_view s,skcoop::core::Sha256& d){if(s.size()!=64)return false;
 const auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
 for(std::size_t i=0;i<32;++i){const int a=digit(s[2*i]),b=digit(s[2*i+1]);if(a<0||b<0)return false;d[i]=static_cast<std::uint8_t>((a<<4)|b);}return true;}
int wmain(int argc,wchar_t** argv){try{if(argc!=3)return 2;std::ifstream in(argv[1],std::ios::binary);std::string role,code,game,scenario,extra;
 std::getline(in,role);std::getline(in,code);std::getline(in,game);std::getline(in,scenario);
 if(!in||std::getline(in,extra)||code.size()>159||(role!="host"&&role!="guest"))return 2;
 SessionConfig c;c.role=role=="host"?Role::host:Role::guest;const auto t=parse_ticket(code);if(!t)return 2;c.ticket=*t;c.participant=random_participant();c.build.purpose=Purpose::native_avatar;
 if(!digest(game,c.build.game)||!digest(scenario,c.build.scenario))return 2;Peer peer(c);const auto start=GetTickCount64();std::uint64_t first{},frames{},fresh{};Motion origin;Status status;bool failed=false;
 while(GetTickCount64()-start<150000){const auto now=GetTickCount64();const auto view=peer.read();status=view.status;
  if(status.state==State::failed||status.state==State::lost){failed=true;break;}
  const auto remote=view.remote.sample(now);
  if(remote&&remote->fresh){++fresh;if(!first){first=now;origin=remote->motion;origin.epoch=1;origin.position[0]+=3;}}
  if(first){auto m=origin;const auto elapsed=now-first;
   // Only this peer's script decides the path. Owner keyboard/camera is never
   // used after the initial safe starting position is sampled.
   m.position[2]+=.8f*static_cast<float>(std::sin(static_cast<double>(elapsed)*.0004));m.speed=elapsed<35000?.5f:0.f;m.angle=0;
   peer.try_offer(m);++frames;if(elapsed>=45000)break;}
  Sleep(20);
 }
 const bool complete=first&&GetTickCount64()-first>=45000&&!failed&&status.frames_sent>20&&status.frames_received>20;
 peer.stop();std::ofstream out(argv[2],std::ios::binary);out<<"{\"kind\":\"scripted-lan-peer\",\"two_game_acceptance\":false,\"completed\":"<<(complete?"true":"false")<<",\"state\":"<<static_cast<int>(status.state)<<",\"error\":"<<static_cast<int>(status.failure)<<",\"frames_sent\":"<<status.frames_sent<<",\"frames_received\":"<<status.frames_received<<",\"fresh_samples\":"<<fresh<<",\"offered_samples\":"<<frames<<"}\n";
 return complete&&out?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
