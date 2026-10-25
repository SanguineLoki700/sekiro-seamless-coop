#pragma once
#include "skcoop/launch_protocol.h"
#include "skcoop/lan/protocol.hpp"
#include <algorithm>
#include <iterator>
namespace skcoop::actors {
inline bool valid_lan_payload(const launch::Payload& p){
 const bool scenario=std::any_of(std::begin(p.scenario_sha256),std::end(p.scenario_sha256),[](auto c){return c!=0;});
 if(p.actor_test!=launch::ActorTest::lan)return !p.lan_role&&!scenario&&std::all_of(std::begin(p.lan_ticket),std::end(p.lan_ticket),[](char c){return c==0;});
 const auto end=std::find(std::begin(p.lan_ticket),std::end(p.lan_ticket),'\0');
 return (p.lan_role==1||p.lan_role==2)&&scenario&&end!=std::end(p.lan_ticket)&&
  std::all_of(end,std::end(p.lan_ticket),[](char c){return c==0;})&&lan::parse_ticket(std::string_view(p.lan_ticket,static_cast<std::size_t>(end-p.lan_ticket))).has_value();
}
}
