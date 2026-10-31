#pragma once
#include "skcoop/launch_protocol.h"
#include "skcoop/lan/protocol.hpp"
#include "skcoop/lan/scenario.hpp"
#include <algorithm>
#include <iterator>
namespace skcoop::actors {
inline bool valid_lan_payload(const launch::Payload& p){
 const bool scenario=std::any_of(std::begin(p.scenario_sha256),std::end(p.scenario_sha256),[](auto c){return c!=0;});
 const bool commands=p.actor_test==launch::ActorTest::lan_native_commands;
 if(p.actor_test!=launch::ActorTest::lan&&!commands)return !p.lan_role&&!p.lan_profile&&!scenario&&std::all_of(std::begin(p.lan_ticket),std::end(p.lan_ticket),[](char c){return c==0;});
 if(commands&&p.lan_profile!=1)return false;
 if(p.lan_profile>2||(p.lan_profile==1&&!std::equal(std::begin(p.scenario_sha256),std::end(p.scenario_sha256),lan::independent_motion_scenario.begin())))return false;
 const auto end=std::find(std::begin(p.lan_ticket),std::end(p.lan_ticket),'\0');
 if((p.lan_role!=1&&p.lan_role!=2)||!scenario||end==std::end(p.lan_ticket)||
    !std::all_of(end,std::end(p.lan_ticket),[](char c){return c==0;}))return false;
 const auto ticket=lan::parse_ticket(std::string_view(p.lan_ticket,static_cast<std::size_t>(end-p.lan_ticket)));
 if(!ticket)return false;
 return p.lan_profile!=2||(p.lan_role==1&&ticket->address=="127.0.0.1"&&
    std::equal(std::begin(p.scenario_sha256),std::end(p.scenario_sha256),lan::local_combat_scenario.begin()));
}
}
