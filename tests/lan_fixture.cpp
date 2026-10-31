// Explicit scripted peer for one-game adapter validation. This is not evidence
// of two real games or two machines, and is never shipped in the owner kit.
#include <Windows.h>
#include "skcoop/lan/peer.hpp"
#include "skcoop/lan/scenario.hpp"
#include "fixture_mirror.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <string_view>
using namespace skcoop::lan;
bool digest(std::string_view s,skcoop::core::Sha256& d){if(s.size()!=64)return false;
 const auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
 for(std::size_t i=0;i<32;++i){const int a=digit(s[2*i]),b=digit(s[2*i+1]);if(a<0||b<0)return false;d[i]=static_cast<std::uint8_t>((a<<4)|b);}return true;}
int wmain(int argc,wchar_t** argv){try{if(argc!=3&&argc!=4)return 2;
 const bool mirror=argc==4&&std::wstring_view(argv[3])==L"--mirror-actions";
 const bool independent=argc==4;
 if(independent&&!mirror&&std::wstring_view(argv[3])!=L"--independent")return 2;
 std::ifstream in(argv[1],std::ios::binary);std::string role,code,game,scenario,extra;
 std::getline(in,role);std::getline(in,code);std::getline(in,game);std::getline(in,scenario);
 if(!in||std::getline(in,extra)||code.size()>159||(role!="host"&&role!="guest"))return 2;
 SessionConfig c;c.role=role=="host"?Role::host:Role::guest;const auto t=parse_ticket(code);if(!t)return 2;c.ticket=*t;c.participant=random_participant();c.build.purpose=Purpose::native_avatar;
 if(!digest(game,c.build.game)||!digest(scenario,c.build.scenario))return 2;
 if(independent){if(c.build.scenario!=independent_motion_scenario)return 2;c.build.adapter=independent_adapter;}
 Peer peer(c);const auto start=GetTickCount64();std::uint64_t first{},first_origin_at{},frames{},fresh{};Motion origin;Status status;
 Motion separated;std::array<std::uint64_t,action_kind_count+1> seen{};
 const std::uint64_t duration=mirror?90000:45000;
 bool failed=false,origin_captured=false,source_changed=false;
 while(GetTickCount64()-start<(mirror?360000u:150000u)){const auto now=GetTickCount64();const auto view=peer.read();status=view.status;
  if(status.state==State::failed||status.state==State::lost){failed=true;break;}
  if(independent&&view.actions.latest())peer.try_ack_actions(view.actions.latest()->packet_sequence);
  const auto remote=view.remote.sample(now);
  if(remote&&remote->fresh){++fresh;
   if(!origin_captured){origin=mirror?view.remote.newest()->motion:remote->motion;origin_captured=true;first_origin_at=now-start;
    if(independent){origin.speed=0;origin.angle=0;std::cout<<"{\"first_origin_captured\":true,\"waiting_for_separation\":true}\n"<<std::flush;}
    else{first=now;origin.epoch=1;origin.position[0]+=3;}}
   if(independent){
    if(remote->motion.epoch!=origin.epoch||remote->motion.descriptor!=origin.descriptor){source_changed=true;failed=true;break;}
    if(!first){const auto& p=remote->motion.position;const auto& q=origin.position;
     const auto gap=std::hypot(p[0]-q[0],p[1]-q[1],p[2]-q[2]);
     if(gap>=1.25f&&gap<=(mirror?6.f:10.f)){first=now;
      // Keep the actual newest source pose; it must be paired with the action
      // history below, rather than run through pose interpolation twice.
      separated=mirror?view.remote.newest()->motion:remote->motion;
      std::cout<<"{\"waiting_for_separation\":false,\"separation_observed\":true}\n"<<std::flush;}}
   }
  }
  if(first){auto m=origin;const auto elapsed=now-first;
   if(mirror){
    if(elapsed>=duration)break;
    // Feed the actual captured commands back as a separate authenticated peer.
    // Start at a previously observed safe pose; preserve the measured separation
    // while mirroring a short path in the same flat area. No invented +X spawn.
    if(!remote||!remote->fresh||!view.actions.latest()||now<view.actions.latest()->arrival_ms||
       now-view.actions.latest()->arrival_ms>action_frame_expiry_ms){Sleep(20);continue;}
    const auto latest=view.remote.newest();
    if(!latest||latest->sequence!=view.actions.latest()->packet_sequence||latest->motion.actions!=view.actions.latest()->history){failed=true;break;}
    const auto mirrored=skcoop::fixture::mirror(origin,separated,latest->motion);
    if(!mirrored){failed=true;break;}m=*mirrored;
    for(std::size_t i=0;i<m.actions.count;++i){const auto& event=m.actions.events[i];const auto kind=static_cast<std::size_t>(event.kind);seen[kind]=std::max(seen[kind],event.serial);}
   }
   else if(independent){m.actions={};m.actions.epoch=m.epoch;m.actions.source_ms=static_cast<std::uint32_t>(elapsed);}
   // Independent mode stays at a previously observed player position. It never
   // guesses an offset into geometry; the owner first walks away from that pose.
   // Legacy mode retains its explicit independent scripted path.
   if(!independent){m.position[2]+=.8f*static_cast<float>(std::sin(static_cast<double>(elapsed)*.0004));m.speed=elapsed<35000?.5f:0.f;m.angle=0;}
   if(peer.try_offer(m))++frames;if(elapsed>=duration)break;}
  Sleep(20);
 }
 const bool complete=first&&GetTickCount64()-first>=duration&&!failed&&status.frames_sent>20&&status.frames_received>20;
 peer.stop();std::ofstream out(argv[2],std::ios::binary);out<<"{\"kind\":\"scripted-lan-peer\",\"two_game_acceptance\":false,\"completed\":"<<(complete?"true":"false")<<",\"state\":"<<static_cast<int>(status.state)<<",\"error\":"<<static_cast<int>(status.failure)<<",\"frames_sent\":"<<status.frames_sent<<",\"frames_received\":"<<status.frames_received<<",\"fresh_samples\":"<<fresh<<",\"offered_samples\":"<<frames
 <<",\"independent\":"<<(independent?"true":"false")<<",\"mirror_actions\":"<<(mirror?"true":"false")<<",\"jump_observed_at_source\":"<<(seen[static_cast<std::size_t>(ActionKind::ground_jump_ready)]?"true":"false")
 <<",\"step_observed_at_source\":"<<(seen[static_cast<std::size_t>(ActionKind::ground_step)]?"true":"false")<<",\"sprint_observed_at_source\":"<<(seen[static_cast<std::size_t>(ActionKind::sprint_start_from_step)]?"true":"false")
 <<",\"first_origin_captured\":"<<(origin_captured?"true":"false")<<",\"first_origin_at_ms\":"<<first_origin_at<<",\"waiting_for_separation\":"<<(independent&&origin_captured&&!first&&!source_changed?"true":"false")<<",\"separation_observed\":"<<(independent&&first?"true":"false")<<",\"source_changed\":"<<(source_changed?"true":"false")<<"}\n";
 return complete&&out?0:1;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
