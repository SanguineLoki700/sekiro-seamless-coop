#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "skcoop/lan/discovery.hpp"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <stdexcept>
namespace skcoop::lan {
namespace {
struct Wsa {Wsa(){WSADATA d{};if(WSAStartup(MAKEWORD(2,2),&d))throw std::runtime_error("WSAStartup");}~Wsa(){WSACleanup();}};
struct Socket {SOCKET s{INVALID_SOCKET};~Socket(){if(s!=INVALID_SOCKET)closesocket(s);}};
struct Subnet {std::uint32_t ip,mask;};
bool private_ip(std::uint32_t n){return (n>>24)==10||(n>>20)==0xac1||(n>>16)==0xc0a8||(n>>24)==127;}
std::vector<Subnet> subnets(){
 ULONG size=16384;std::vector<std::uint8_t> bytes(size);auto* list=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(bytes.data());
 auto result=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,list,&size);
 if(result==ERROR_BUFFER_OVERFLOW){if(size>1024*1024)throw std::runtime_error("Adapter limit");bytes.resize(size);list=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(bytes.data());result=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,list,&size);}
 if(result!=NO_ERROR)throw std::runtime_error("Adapter query");std::vector<Subnet> out;
 for(auto* a=list;a;a=a->Next)if(a->OperStatus==IfOperStatusUp&&(a->IfType==IF_TYPE_ETHERNET_CSMACD||a->IfType==IF_TYPE_IEEE80211))for(auto* u=a->FirstUnicastAddress;u;u=u->Next){
  if(u->Address.lpSockaddr&&u->Address.lpSockaddr->sa_family==AF_INET&&u->OnLinkPrefixLength>0&&u->OnLinkPrefixLength<31){const auto ip=ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);if(private_ip(ip))out.push_back({ip,0xffffffffu<<(32-u->OnLinkPrefixLength)});}
 }
 out.push_back({0x7f000001u,0xff000000u});return out;
}
void put(std::vector<std::uint8_t>& b,std::uint64_t n,unsigned size){for(unsigned i=0;i<size;++i)b.push_back(static_cast<std::uint8_t>(n>>(8*i)));}
std::uint64_t get(std::span<const std::uint8_t> b,unsigned at,unsigned size){std::uint64_t n{};for(unsigned i=0;i<size;++i)n|=std::uint64_t{b[at+i]}<<(8*i);return n;}
std::vector<std::uint8_t> packet(unsigned kind,std::uint64_t nonce){std::vector<std::uint8_t> b{'S','K','D','1',1,static_cast<std::uint8_t>(kind),0,0};put(b,nonce,8);return b;}
bool header(std::span<const std::uint8_t> b,unsigned kind){return b.size()>=16&&b[0]=='S'&&b[1]=='K'&&b[2]=='D'&&b[3]=='1'&&b[4]==1&&b[5]==kind&&!b[6]&&!b[7]&&get(b,8,8);}
sockaddr_in endpoint(std::string_view ip,std::uint16_t port){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);const std::string s(ip);if(InetPtonA(AF_INET,s.c_str(),&a.sin_addr)!=1)throw std::runtime_error("Invalid probe IP");return a;}
}
struct Discovery::Impl {
 std::optional<Ticket> host;std::uint16_t port;std::vector<std::string> targets;std::atomic<bool> stopped{};std::thread thread;mutable std::mutex mutex;DiscoveryView view;
 Impl(std::optional<Ticket> h,std::uint16_t p,std::vector<std::string> t):host(std::move(h)),port(p),targets(std::move(t)){}
 void run()noexcept{DiscoveryView state;try{Wsa wsa;Socket socket;socket.s=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(socket.s==INVALID_SOCKET)throw std::runtime_error("UDP socket");
  BOOL yes=TRUE;if(host&&setsockopt(socket.s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<char*>(&yes),sizeof(yes)))throw std::runtime_error("UDP exclusive");
  if(setsockopt(socket.s,SOL_SOCKET,SO_BROADCAST,reinterpret_cast<char*>(&yes),sizeof(yes)))throw std::runtime_error("UDP broadcast");
  auto local=endpoint("0.0.0.0",host?port:0);if(bind(socket.s,reinterpret_cast<sockaddr*>(&local),sizeof(local)))throw std::runtime_error("UDP bind");
  int length=sizeof(local);if(getsockname(socket.s,reinterpret_cast<sockaddr*>(&local),&length))throw std::runtime_error("UDP port");state.port=ntohs(local.sin_port);
  const auto nets=subnets();if(!host&&targets.empty())for(const auto& n:nets)if((n.ip>>24)!=127){IN_ADDR addr{};addr.s_addr=htonl(n.ip|~n.mask);char text[INET_ADDRSTRLEN]{};if(InetNtopA(AF_INET,&addr,text,sizeof(text)))targets.emplace_back(text);}
  const auto nonce=random_participant();std::uint64_t probe_at{};unsigned response_budget{};std::uint64_t response_epoch{};
  const auto publish=[&]{std::lock_guard lock(mutex);view=state;};publish();
  while(!stopped){const auto now=GetTickCount64();
   if(!host&&now>=probe_at){const auto request=packet(1,nonce);for(const auto& ip:targets){const auto to=endpoint(ip,port);sendto(socket.s,reinterpret_cast<const char*>(request.data()),static_cast<int>(request.size()),0,reinterpret_cast<const sockaddr*>(&to),sizeof(to));}probe_at=now+500;}
   fd_set ready;FD_ZERO(&ready);FD_SET(socket.s,&ready);timeval timeout{0,50000};const auto result=select(0,&ready,nullptr,nullptr,&timeout);if(result==SOCKET_ERROR)throw std::runtime_error("UDP select");
   if(result){std::array<std::uint8_t,256> bytes{};sockaddr_in from{};int from_size=sizeof(from);const auto count=recvfrom(socket.s,reinterpret_cast<char*>(bytes.data()),static_cast<int>(bytes.size()),0,reinterpret_cast<sockaddr*>(&from),&from_size);
    if(count>0){const auto b=std::span<const std::uint8_t>(bytes).first(static_cast<std::size_t>(count));const auto source=ntohl(from.sin_addr.s_addr);
     const bool local_source=private_ip(source)&&std::any_of(nets.begin(),nets.end(),[&](const auto& n){return (source&n.mask)==(n.ip&n.mask);});
     if(local_source&&host&&count==16&&header(b,1)){
      if(now-response_epoch>1000){response_epoch=now;response_budget=0;}if(++response_budget<=16){auto response=packet(2,get(b,8,8));put(response,host->port,2);put(response,0,2);response.insert(response.end(),host->room.begin(),host->room.end());response.insert(response.end(),host->key.begin(),host->key.end());sendto(socket.s,reinterpret_cast<const char*>(response.data()),static_cast<int>(response.size()),0,reinterpret_cast<const sockaddr*>(&from),sizeof(from));}
     }else if(local_source&&!host&&count==68&&header(b,2)&&get(b,8,8)==nonce&&!b[18]&&!b[19]){
      char ip[INET_ADDRSTRLEN]{};if(InetNtopA(AF_INET,&from.sin_addr,ip,sizeof(ip))){Ticket t;t.address=ip;t.port=static_cast<std::uint16_t>(get(b,16,2));std::copy_n(b.begin()+20,16,t.room.begin());std::copy_n(b.begin()+36,32,t.key.begin());
       if(t.port&&std::any_of(t.room.begin(),t.room.end(),[](auto n){return n!=0;})&&std::any_of(t.key.begin(),t.key.end(),[](auto n){return n!=0;})){auto found=std::find_if(state.rooms.begin(),state.rooms.end(),[&](const auto& r){return r.ticket.room==t.room&&r.ticket.address==t.address;});if(found!=state.rooms.end())*found={t,now};else if(state.rooms.size()<8)state.rooms.push_back({t,now});}
      }
     }
    }
   }
   std::erase_if(state.rooms,[&](const auto& r){return now>=r.seen&&now-r.seen>2500;});publish();
  }
 }catch(...){state.error=static_cast<std::uint32_t>(WSAGetLastError());if(!state.error)state.error=ERROR_GEN_FAILURE;std::lock_guard lock(mutex);view=state;}}
};
Discovery::Discovery(std::optional<Ticket> h,std::uint16_t p,std::vector<std::string> t):impl_(std::make_unique<Impl>(std::move(h),p,std::move(t))){impl_->thread=std::thread([p=impl_.get()]{p->run();});}
Discovery::~Discovery(){stop();}void Discovery::stop(){impl_->stopped=true;if(impl_->thread.joinable())impl_->thread.join();}
DiscoveryView Discovery::read()const{std::lock_guard lock(impl_->mutex);return impl_->view;}
}
