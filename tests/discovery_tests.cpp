#include <winsock2.h>
#include "skcoop/lan/discovery.hpp"
#include "skcoop/lan/peer.hpp"
#include "skcoop/lan/scenario.hpp"
#include <iostream>
#include <stdexcept>
using namespace skcoop::lan;
void require(bool b,const char* reason){if(!b)throw std::runtime_error(reason);}
template<class F>void wait(F f){const auto end=GetTickCount64()+6500;while(GetTickCount64()<end){if(f())return;Sleep(15);}throw std::runtime_error("Required discovery/network result missing");}
int main(){try{
 Ticket t{"192.168.77.99",0,random_id(),random_key()};Build build;build.purpose=Purpose::native_avatar;build.adapter=independent_adapter;build.game[0]=1;build.scenario=independent_motion_scenario;
 Peer server({Role::host,t,build,101},"127.0.0.1");wait([&]{return server.read().status.state==State::listening;});t.port=server.read().status.local_port;
 Discovery host(t,0);wait([&]{return host.read().port||host.read().error;});require(!host.read().error,"Discovery host socket failed");
 Discovery client({},host.read().port,{"127.0.0.1"});wait([&]{return !client.read().rooms.empty()||client.read().error;});const auto found=client.read();require(!found.error&&found.rooms.size()==1,"Missing discovered host");
 const auto room=found.rooms[0].ticket;require(room.address=="127.0.0.1"&&room.port==t.port&&room.room==t.room&&room.key==t.key,"Discovery must use response source and exact room");
 // A response that did not echo this client's probe nonce must not add a room.
 WSADATA data{};require(WSAStartup(MAKEWORD(2,2),&data)==0,"Test WSA startup");SOCKET raw=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);require(raw!=INVALID_SOCKET,"Test UDP socket");
 sockaddr_in destination{};destination.sin_family=AF_INET;destination.sin_addr.s_addr=htonl(INADDR_LOOPBACK);destination.sin_port=htons(client.read().port);std::array<char,68> unsolicited{};unsolicited[0]='S';unsolicited[1]='K';unsolicited[2]='D';unsolicited[3]='1';unsolicited[4]=1;unsolicited[5]=2;
 sendto(raw,unsolicited.data(),static_cast<int>(unsolicited.size()),0,reinterpret_cast<sockaddr*>(&destination),sizeof(destination));closesocket(raw);WSACleanup();Sleep(80);require(client.read().rooms.size()==1&&!client.read().error,"Unsolicited discovery response was trusted");
 Peer guest({Role::guest,room,build,202});
 wait([&]{Motion a;a.epoch=1;a.descriptor=0xa000;a.position[0]=1;Motion b=a;b.position[0]=3;server.try_offer(a);guest.try_offer(b);return server.read().status.frames_received>=3&&guest.read().status.frames_received>=3;});
 require(server.read().remote.sample(GetTickCount64())->motion.position[0]==3,"Discovered peer did not provide independent motion");
 guest.stop();server.stop();host.stop();wait([&]{return client.read().rooms.empty();});require(!client.read().error,"Absent host was reported as a connection");client.stop();
 std::cout<<"PASS UDP discovery, source-address selection, unsolicited-response rejection, actual authenticated peers and room expiry\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
