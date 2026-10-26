#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "skcoop/lan/peer.hpp"
#include <atomic>
#include <algorithm>
#include <mutex>
#include <limits>
#include <thread>
#include <stdexcept>
namespace skcoop::lan {
namespace {
struct Wsa { Wsa(){WSADATA d{};if(WSAStartup(MAKEWORD(2,2),&d))throw std::runtime_error("WSAStartup failed");}~Wsa(){WSACleanup();} };
struct Socket { SOCKET value{INVALID_SOCKET};~Socket(){if(value!=INVALID_SOCKET)closesocket(value);}Socket()=default;Socket(const Socket&)=delete;Socket& operator=(const Socket&)=delete; };
struct SocketError {int code;};
void check(bool yes){if(!yes)throw SocketError{WSAGetLastError()};}
sockaddr_in address(std::string_view ip,std::uint16_t port){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);const std::string s(ip);if(InetPtonA(AF_INET,s.c_str(),&a.sin_addr)!=1)throw std::invalid_argument("LAN address must be numeric IPv4");return a;}
void nonblocking(SOCKET s){u_long enabled=1;check(ioctlsocket(s,FIONBIO,&enabled)==0);BOOL yes=TRUE;check(setsockopt(s,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&yes),sizeof(yes))==0);}
std::uint16_t bound_port(SOCKET s){sockaddr_in a{};int n=sizeof(a);check(getsockname(s,reinterpret_cast<sockaddr*>(&a),&n)==0);return ntohs(a.sin_port);}
}
ActionFramePush ActionFrameBuffer::fail(ActionFault fault)noexcept{
 fault_=fault;count_=0;return ActionFramePush::failed;
}
ActionFramePush ActionFrameBuffer::push(const ActionFrame& frame)noexcept{
 if(fault_!=ActionFault::none)return ActionFramePush::failed;
 if(!valid(frame.scope)||!valid(frame.history)||frame.scope.epoch!=frame.history.epoch||!frame.packet_sequence)
  return fail(ActionFault::invalid);
 if(frame.arrival_ms>std::numeric_limits<std::uint64_t>::max()-action_delay_ms)return fail(ActionFault::clock_regressed);
 bool changed=frame.history.latest>0,expired=false;
 if(latest_){
  const auto& old=*latest_;
  if(frame.scope!=old.scope)return fail(ActionFault::scope_changed);
  if(frame.packet_sequence<=old.packet_sequence)return ActionFramePush::stale;
  if(frame.arrival_ms<old.arrival_ms||frame.history.source_ms<old.history.source_ms)return fail(ActionFault::clock_regressed);
  if(frame.history.latest<old.history.latest)return fail(ActionFault::invalid);
  for(std::size_t i=0;i<old.history.count;++i)for(std::size_t j=0;j<frame.history.count;++j)
   if(old.history.events[i].serial==frame.history.events[j].serial&&old.history.events[i]!=frame.history.events[j])return fail(ActionFault::invalid);
  if(!consistent_guard(old.history,frame.history))return fail(ActionFault::invalid);
  expired=frame.arrival_ms-old.arrival_ms>action_frame_expiry_ms;
  changed=frame.history.latest>old.history.latest;
  if(changed&&frame.history.events[0].serial>old.history.latest+1&&!expired)return fail(ActionFault::gap);
 }
 if(expired){
  if(baseline_generation_==std::numeric_limits<std::uint64_t>::max())return fail(ActionFault::serial_exhausted);
  ++baseline_generation_;count_=0;pending_.fill({});
 }
 if(changed){if(count_==pending_.size())return fail(ActionFault::overflow);pending_[count_++]=frame;}
 latest_=frame;return ActionFramePush::accepted;
}
bool ActionFrameBuffer::acknowledge(std::uint64_t sequence)noexcept{
 if(fault_!=ActionFault::none)return false;
 if(!latest_||!sequence||sequence>latest_->packet_sequence){fail(ActionFault::invalid);return false;}
 if(sequence<=acknowledged_)return true;
 std::size_t removed{};while(removed<count_&&pending_[removed].packet_sequence<=sequence)++removed;
 if(removed){std::move(pending_.begin()+static_cast<std::ptrdiff_t>(removed),pending_.begin()+static_cast<std::ptrdiff_t>(count_),pending_.begin());
  const auto before=count_;count_-=removed;std::fill(pending_.begin()+static_cast<std::ptrdiff_t>(count_),pending_.begin()+static_cast<std::ptrdiff_t>(before),ActionFrame{});}
 acknowledged_=sequence;return true;
}
struct Peer::Impl {
 SessionConfig config;std::string bind;std::atomic<bool> stop_requested{};std::thread thread;
 mutable std::mutex mutex;View published;std::optional<Motion> local;std::uint64_t local_at{};
 Impl(SessionConfig c,std::string b):config(std::move(c)),bind(std::move(b)){if(!config.participant)config.participant=random_participant();published.status.participant=config.participant;}
 void run()noexcept {
  Status status;status.participant=config.participant;MotionBuffer buffer;
  const auto update=[&]{std::lock_guard lock(mutex);status.action_fault=published.actions.fault();if(status.action_fault!=ActionFault::none){status.failure=Failure::protocol;status.state=State::failed;}published.status=status;published.remote=buffer;return status.state!=State::failed;};
  try {
   Wsa wsa;Socket listener,socket;Session session(config);const auto started=GetTickCount64();
   if(config.role==Role::host){
    listener.value=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);check(listener.value!=INVALID_SOCKET);
    BOOL exclusive=TRUE;check(setsockopt(listener.value,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))==0);
    auto a=address(bind,config.ticket.port);check(::bind(listener.value,reinterpret_cast<const sockaddr*>(&a),sizeof(a))==0);check(listen(listener.value,1)==0);
    u_long yes=1;check(ioctlsocket(listener.value,FIONBIO,&yes)==0);status.local_port=bound_port(listener.value);status.state=State::listening;update();
    while(!stop_requested){fd_set read;FD_ZERO(&read);FD_SET(listener.value,&read);timeval timeout{0,10000};const auto n=select(0,&read,nullptr,nullptr,&timeout);check(n!=SOCKET_ERROR);if(!n)continue;
     socket.value=accept(listener.value,nullptr,nullptr);if(socket.value==INVALID_SOCKET){if(WSAGetLastError()==WSAEWOULDBLOCK)continue;throw SocketError{WSAGetLastError()};}break;}
   }else{
    if(!valid_ipv4(config.ticket.address)||!config.ticket.port)throw std::invalid_argument("Invalid guest endpoint");
    socket.value=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);check(socket.value!=INVALID_SOCKET);nonblocking(socket.value);
    auto a=address(config.ticket.address,config.ticket.port);status.state=State::connecting;update();
    const auto result=connect(socket.value,reinterpret_cast<const sockaddr*>(&a),sizeof(a));
    if(result==SOCKET_ERROR){check(WSAGetLastError()==WSAEWOULDBLOCK);
     while(!stop_requested){fd_set write,errors;FD_ZERO(&write);FD_ZERO(&errors);FD_SET(socket.value,&write);FD_SET(socket.value,&errors);timeval timeout{0,10000};const auto n=select(0,nullptr,&write,&errors,&timeout);check(n!=SOCKET_ERROR);
      if(GetTickCount64()-started>15000)throw SocketError{WSAETIMEDOUT};if(!n)continue;int error{},len=sizeof(error);check(getsockopt(socket.value,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&len)==0);if(error)throw SocketError{error};break;}
    }
   }
   if(stop_requested){status.state=State::stopped;update();return;}
   nonblocking(socket.value);if(!status.local_port)status.local_port=bound_port(socket.value);
   session.opened(GetTickCount64(),config.role==Role::host?random_id():Id{});status.state=State::handshaking;update();
   std::vector<std::uint8_t> rx,tx;std::size_t tx_at{};std::uint64_t next_frame{},next_heartbeat{};
   const auto enqueue=[&](const Packet& p){const auto encoded=encode(p,config.ticket.key);if(tx.size()+encoded.size()+2>2048)throw std::runtime_error("Bounded LAN send queue exceeded");const auto length=static_cast<std::uint16_t>(encoded.size());tx.push_back(static_cast<std::uint8_t>(length));tx.push_back(static_cast<std::uint8_t>(length>>8));tx.insert(tx.end(),encoded.begin(),encoded.end());++status.sent;if(p.kind==Kind::frame)++status.frames_sent;};
   while(!stop_requested){
    const auto now=GetTickCount64();session.tick(now);status.state=session.state();status.failure=session.failure();status.peer=session.peer();
    if(status.state==State::lost||status.state==State::failed)break;
    for(const auto& p:session.take_outgoing())enqueue(p);
    if(status.state==State::connected&&tx.empty()){
     std::optional<Motion> offered;std::uint64_t offered_at{};{std::lock_guard lock(mutex);offered=local;offered_at=local_at;}
     if(offered&&now>=offered_at&&now-offered_at<=500&&now>=next_frame){session.send_frame(*offered);next_frame=now+50;next_heartbeat=now+250;}
     else if(now>=next_heartbeat){session.heartbeat();next_heartbeat=now+250;}
     for(const auto& p:session.take_outgoing())enqueue(p);
    }
    fd_set read,write,errors;FD_ZERO(&read);FD_ZERO(&write);FD_ZERO(&errors);FD_SET(socket.value,&read);FD_SET(socket.value,&errors);if(!tx.empty())FD_SET(socket.value,&write);
    timeval timeout{0,10000};check(select(0,&read,&write,&errors,&timeout)!=SOCKET_ERROR);
    if(FD_ISSET(socket.value,&errors))throw SocketError{WSAECONNRESET};
    if(FD_ISSET(socket.value,&write)&&!tx.empty()){
     const auto n=send(socket.value,reinterpret_cast<const char*>(tx.data()+tx_at),static_cast<int>(tx.size()-tx_at),0);
     if(n>0){tx_at+=static_cast<std::size_t>(n);if(tx_at==tx.size()){tx.clear();tx_at=0;}}
     else if(n==SOCKET_ERROR&&WSAGetLastError()!=WSAEWOULDBLOCK)throw SocketError{WSAGetLastError()};
    }
    if(FD_ISSET(socket.value,&read)){
     std::array<char,4096> chunk{};const auto n=recv(socket.value,chunk.data(),static_cast<int>(chunk.size()),0);
     if(n==0){status.state=State::lost;break;}
     if(n==SOCKET_ERROR){if(WSAGetLastError()!=WSAEWOULDBLOCK)throw SocketError{WSAGetLastError()};}
     else {if(rx.size()+static_cast<std::size_t>(n)>8192)throw std::runtime_error("Bounded LAN receive queue exceeded");rx.insert(rx.end(),chunk.begin(),chunk.begin()+n);}
    }
    for(unsigned budget=0;budget<32&&rx.size()>=2;++budget){
     const auto size=static_cast<std::size_t>(rx[0])|(static_cast<std::size_t>(rx[1])<<8);
     if(size<header_bytes+tag_bytes||size>max_packet_bytes)throw std::runtime_error("Invalid LAN stream frame size");if(rx.size()<size+2)break;
     const auto decoded=decode(std::span<const std::uint8_t>(rx).subspan(2,size),config.ticket.room,config.ticket.key);
     rx.erase(rx.begin(),rx.begin()+static_cast<std::ptrdiff_t>(size+2));
     if(!decoded){++status.rejected;status.failure=decoded.error==Error::authentication?Failure::authentication:Failure::protocol;status.state=State::failed;break;}
     ++status.received;const auto& p=*decoded.packet;
     if(!session.receive(p,GetTickCount64())){++status.rejected;if(session.state()==State::failed){status.state=State::failed;status.failure=session.failure();break;}continue;}
     if(const auto frame=session.take_remote_frame()){
      const auto result=buffer.push(frame->motion,frame->arrival_ms,frame->packet_sequence);if(result==PushResult::scene_changed){status.state=State::failed;status.failure=Failure::scene;break;}
      if(result==PushResult::invalid){status.state=State::failed;status.failure=Failure::protocol;break;}
      if(result==PushResult::accepted){
       ++status.frames_received;
       if(frame->motion.actions.epoch){
        std::lock_guard lock(mutex);const ActionFrame actions{frame->scope,frame->motion.actions,frame->packet_sequence,frame->arrival_ms};
        if(published.actions.push(actions)==ActionFramePush::failed){status.state=State::failed;status.failure=Failure::protocol;status.action_fault=published.actions.fault();published.status=status;break;}
        published.remote=buffer; // Publish each new action batch with its corresponding pose samples.
       }
      }
     }
    }
    if(status.state==State::failed)break;
    status.state=session.state();status.failure=session.failure();status.peer=session.peer();if(!update())break;
   }
   if(stop_requested)status.state=State::stopped;update();
  }catch(const SocketError& e){status.system_error=static_cast<std::uint32_t>(e.code);status.failure=Failure::socket;status.state=State::failed;update();}
   catch(...){status.failure=Failure::internal;status.state=State::failed;update();}
 }
};
Peer::Peer(SessionConfig c,std::string bind):impl_(std::make_unique<Impl>(std::move(c),std::move(bind))){impl_->thread=std::thread([p=impl_.get()]{p->run();});}
Peer::~Peer(){stop();}
void Peer::request_stop()noexcept{impl_->stop_requested=true;}
void Peer::stop(){request_stop();if(impl_->thread.joinable())impl_->thread.join();}
bool Peer::try_offer(const Motion& m){if(!valid(impl_->config.build,m))return false;std::unique_lock lock(impl_->mutex,std::try_to_lock);if(!lock)return false;impl_->local=m;impl_->local_at=GetTickCount64();return true;}
bool Peer::try_read(View& v)const{std::unique_lock lock(impl_->mutex,std::try_to_lock);if(!lock)return false;v=impl_->published;return true;}
bool Peer::try_ack_actions(std::uint64_t sequence){
 std::unique_lock lock(impl_->mutex,std::try_to_lock);if(!lock)return false;
 const bool ok=impl_->published.actions.acknowledge(sequence);
 if(!ok){impl_->published.status.action_fault=impl_->published.actions.fault();impl_->published.status.failure=Failure::protocol;impl_->published.status.state=State::failed;}
 return ok;
}
View Peer::read()const{std::lock_guard lock(impl_->mutex);return impl_->published;}
std::vector<std::string> local_addresses(){
 Wsa wsa;ULONG size=16384;std::vector<std::uint8_t> bytes(size);auto* list=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(bytes.data());
 auto result=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,list,&size);
 if(result==ERROR_BUFFER_OVERFLOW){if(size>1024*1024)throw std::runtime_error("Adapter list limit");bytes.resize(size);list=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(bytes.data());result=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,list,&size);}
 if(result!=NO_ERROR)throw std::runtime_error("Cannot enumerate local IPv4 addresses");std::vector<std::string> out;
 for(auto* a=list;a;a=a->Next)if(a->OperStatus==IfOperStatusUp&&a->IfType!=IF_TYPE_SOFTWARE_LOOPBACK)for(auto* u=a->FirstUnicastAddress;u;u=u->Next){
  if(!u->Address.lpSockaddr||u->Address.lpSockaddr->sa_family!=AF_INET)continue;char ip[INET_ADDRSTRLEN]{};const auto* v=reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);if(InetNtopA(AF_INET,const_cast<IN_ADDR*>(&v->sin_addr),ip,sizeof(ip))&&valid_ipv4(ip)&&!std::string_view(ip).starts_with("169.254."))out.emplace_back(ip);
 }
 std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());return out;
}
}
