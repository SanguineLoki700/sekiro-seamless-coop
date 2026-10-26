#include "skcoop/lan/protocol.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace skcoop::lan {
namespace {
template<class T>bool nonzero(const T& a){return std::any_of(a.begin(),a.end(),[](auto v){return v!=0;});}
struct Bytes {
 std::vector<std::uint8_t> data;
 template<class T>void put(T n){static_assert(std::is_unsigned_v<T>);for(unsigned i=0;i<sizeof(T);++i)data.push_back(static_cast<std::uint8_t>(n>>(8*i)));}
 template<std::size_t N>void put(const std::array<std::uint8_t,N>& a){data.insert(data.end(),a.begin(),a.end());}
 void real(float f){put(std::bit_cast<std::uint32_t>(f));}
};
struct Read {
 std::span<const std::uint8_t> data;std::size_t at{};
 template<class T>T get(){static_assert(std::is_unsigned_v<T>);if(sizeof(T)>data.size()-at)throw std::runtime_error("LAN truncated scalar");std::uint64_t n{};for(unsigned i=0;i<sizeof(T);++i)n|=std::uint64_t{data[at++]}<<(8*i);return static_cast<T>(n);}
 template<std::size_t N>std::array<std::uint8_t,N> fixed(){if(N>data.size()-at)throw std::runtime_error("LAN truncated array");std::array<std::uint8_t,N> a{};std::copy_n(data.begin()+static_cast<std::ptrdiff_t>(at),N,a.begin());at+=N;return a;}
 float real(){return std::bit_cast<float>(get<std::uint32_t>());}
};
Key mac(std::span<const std::uint8_t> bytes,const Key& key){
 BCRYPT_ALG_HANDLE alg{};if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)<0)throw std::runtime_error("LAN HMAC provider failed");
 Key out{};const auto result=BCryptHash(alg,const_cast<PUCHAR>(key.data()),static_cast<ULONG>(key.size()),const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),out.data(),static_cast<ULONG>(out.size()));
 BCryptCloseAlgorithmProvider(alg,0);if(result<0)throw std::runtime_error("LAN HMAC failed");return out;
}
bool build_kind(Kind k){return k==Kind::challenge||k==Kind::hello||k==Kind::welcome;}
bool good_kind(Kind k){return k>=Kind::challenge&&k<=Kind::bye;}
std::string hex(std::span<const std::uint8_t> a){constexpr char h[]="0123456789abcdef";std::string s;for(auto v:a){s+=h[v>>4];s+=h[v&15];}return s;}
template<std::size_t N>bool unhex(std::string_view s,std::array<std::uint8_t,N>& out){if(s.size()!=N*2)return false;for(std::size_t i=0;i<N;++i){unsigned v{};const auto r=std::from_chars(s.data()+i*2,s.data()+i*2+2,v,16);if(r.ec!=std::errc{}||r.ptr!=s.data()+i*2+2)return false;out[i]=static_cast<std::uint8_t>(v);}return true;}
template<std::size_t N>std::array<std::uint8_t,N> random(){std::array<std::uint8_t,N> a{};do{if(BCryptGenRandom(nullptr,a.data(),static_cast<ULONG>(N),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("LAN random source failed");}while(!nonzero(a));return a;}
}
bool valid(const Build& b)noexcept{return (b.purpose==Purpose::connection_check&&b.adapter==1&&!nonzero(b.game)&&!nonzero(b.scenario))||(b.purpose==Purpose::native_avatar&&(b.adapter==1||b.adapter==2||b.adapter==action_adapter)&&nonzero(b.game)&&nonzero(b.scenario));}
bool valid(const Motion& m)noexcept{
 if(!m.epoch||!std::isfinite(m.speed)||m.speed<0||m.speed>1||!std::isfinite(m.angle)||std::abs(m.angle)>360)return false;
 for(float f:m.position)if(!std::isfinite(f)||std::abs(f)>=1e7f)return false;
 for(float f:m.rotation)if(!std::isfinite(f)||std::abs(f)>6.4f)return false;
 return m.actions==ActionSnapshot{}||(valid(m.actions)&&m.actions.epoch==m.epoch);
}
bool valid(const Build& b,const Motion& m)noexcept{
 if(!valid(b)||!valid(m))return false;
 if(b.purpose==Purpose::connection_check)return m.descriptor==0&&m.actions==ActionSnapshot{};
 if(m.descriptor!=0xa000)return false;
 return b.adapter==action_adapter?valid(m.actions)&&m.actions.epoch==m.epoch:m.actions==ActionSnapshot{};
}
bool valid_ipv4(std::string_view s)noexcept{
 unsigned first{};for(unsigned i=0;i<4;++i){const auto dot=s.find('.');const auto part=s.substr(0,dot);if(part.empty()||part.size()>3||(part.size()>1&&part[0]=='0'))return false;unsigned n{};const auto r=std::from_chars(part.data(),part.data()+part.size(),n);if(r.ec!=std::errc{}||r.ptr!=part.data()+part.size()||n>255)return false;if(i==0)first=n;
 if(i<3){if(dot==std::string_view::npos)return false;s.remove_prefix(dot+1);}else if(dot!=std::string_view::npos)return false;}
 return first>0&&first<224;
}
Id random_id(){return random<16>();}Key random_key(){return random<32>();}
std::uint64_t random_participant(){const auto a=random<8>();std::uint64_t n{};for(unsigned i=0;i<8;++i)n|=std::uint64_t{a[i]}<<(i*8);return n;}
std::string ticket_text(const Ticket& t){if(!valid_ipv4(t.address)||!t.port||!nonzero(t.room)||!nonzero(t.key))throw std::invalid_argument("Invalid LAN room");return "SKL1|"+t.address+'|'+std::to_string(t.port)+'|'+hex(t.room)+'|'+hex(t.key);}
std::optional<Ticket> parse_ticket(std::string_view s){
 if(s.size()>160||!s.starts_with("SKL1|"))return {};s.remove_prefix(5);std::array<std::string_view,4> p{};
 for(unsigned i=0;i<4;++i){const auto at=s.find('|');p[i]=s.substr(0,at);if(i<3){if(at==std::string_view::npos)return {};s.remove_prefix(at+1);}else if(at!=std::string_view::npos)return {};}
 Ticket t;t.address=p[0];unsigned port{};const auto r=std::from_chars(p[1].data(),p[1].data()+p[1].size(),port);
 if(!valid_ipv4(t.address)||r.ec!=std::errc{}||r.ptr!=p[1].data()+p[1].size()||!port||port>65535||!unhex(p[2],t.room)||!unhex(p[3],t.key)||!nonzero(t.room)||!nonzero(t.key))return {};
 t.port=static_cast<std::uint16_t>(port);return t;
}
std::vector<std::uint8_t> encode(const Packet& p,const Key& key){
 static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559);
 static_assert(header_bytes+tag_bytes+motion_body_bytes<=max_packet_bytes);
 if(!good_kind(p.kind)||!nonzero(p.room)||!nonzero(p.connection)||!nonzero(key)||!p.sender||!p.sequence||
    ((p.kind==Kind::challenge)?p.receiver!=0:p.receiver==0))throw std::invalid_argument("Invalid LAN envelope");
 Bytes body;if(build_kind(p.kind)){if(!valid(p.build))throw std::invalid_argument("Invalid LAN identity");body.put(static_cast<std::uint8_t>(p.build.purpose));body.put(std::uint8_t{});body.put(std::uint16_t{});body.put(p.build.adapter);body.put(p.build.game);body.put(p.build.scenario);}
 else if(p.kind==Kind::frame){
  if(!valid(p.motion))throw std::invalid_argument("Invalid LAN motion");body.put(p.motion.epoch);body.put(p.motion.descriptor);body.put(std::uint32_t{});for(float f:p.motion.position)body.real(f);for(float f:p.motion.rotation)body.real(f);body.real(p.motion.speed);body.real(p.motion.angle);
  const auto& actions=p.motion.actions;body.put(static_cast<std::uint8_t>(actions.epoch?1:0));body.put(actions.count);body.put(static_cast<std::uint8_t>((actions.sprinting?1:0)|(actions.guarding?2:0)));body.put(std::uint8_t{});body.put(actions.latest);body.put(actions.source_ms);
  for(const auto& event:actions.events){body.put(event.serial);body.put(event.source_ms);body.put(static_cast<std::uint8_t>(event.kind));body.put(std::uint8_t{});body.put(std::uint16_t{});body.real(event.direction);}
  if(body.data.size()!=motion_body_bytes)throw std::logic_error("LAN motion layout mismatch");
 }
 Bytes out;out.put(std::array<std::uint8_t,4>{'S','K','L','1'});out.put(version);out.put(static_cast<std::uint8_t>(p.kind));out.put(std::uint8_t{});out.put(static_cast<std::uint16_t>(body.data.size()));out.put(std::uint16_t{});out.put(p.room);out.put(p.connection);out.put(p.sender);out.put(p.receiver);out.put(p.sequence);
 out.data.insert(out.data.end(),body.data.begin(),body.data.end());out.put(mac(out.data,key));if(out.data.size()>max_packet_bytes)throw std::length_error("LAN packet limit");return out.data;
}
Decoded decode(std::span<const std::uint8_t> bytes,const Id& room,const Key& key){
 if(bytes.size()<header_bytes+tag_bytes||bytes.size()>max_packet_bytes)return {Error::size,{}};
 if(!nonzero(room)||!nonzero(key))return {Error::value,{}};
 try{
 Read r{bytes};if(r.fixed<4>()!=std::array<std::uint8_t,4>{'S','K','L','1'})return {Error::format,{}};
 if(r.get<std::uint16_t>()!=version)return {Error::version,{}};
 Packet p;p.kind=static_cast<Kind>(r.get<std::uint8_t>());if(r.get<std::uint8_t>()||!good_kind(p.kind))return {Error::format,{}};
 const auto size=r.get<std::uint16_t>();if(r.get<std::uint16_t>()||size+header_bytes+tag_bytes!=bytes.size())return {Error::size,{}};
 p.room=r.fixed<16>();if(p.room!=room)return {Error::room,{}};
 const auto expected=mac(bytes.first(bytes.size()-tag_bytes),key);std::uint8_t difference{};for(std::size_t i=0;i<tag_bytes;++i)difference|=expected[i]^bytes[bytes.size()-tag_bytes+i];if(difference)return {Error::authentication,{}};
 p.connection=r.fixed<16>();p.sender=r.get<std::uint64_t>();p.receiver=r.get<std::uint64_t>();p.sequence=r.get<std::uint64_t>();
 if(!nonzero(p.connection)||!p.sender||!p.sequence||((p.kind==Kind::challenge)?p.receiver!=0:p.receiver==0))return {Error::value,{}};
 if(build_kind(p.kind)){p.build.purpose=static_cast<Purpose>(r.get<std::uint8_t>());if(r.get<std::uint8_t>()||r.get<std::uint16_t>())return {Error::format,{}};p.build.adapter=r.get<std::uint32_t>();p.build.game=r.fixed<32>();p.build.scenario=r.fixed<32>();if(!valid(p.build))return {Error::value,{}};}
 else if(p.kind==Kind::frame){
  if(size!=motion_body_bytes)return {Error::size,{}};
  p.motion.epoch=r.get<std::uint64_t>();p.motion.descriptor=r.get<std::uint32_t>();if(r.get<std::uint32_t>())return {Error::format,{}};for(auto& f:p.motion.position)f=r.real();for(auto& f:p.motion.rotation)f=r.real();p.motion.speed=r.real();p.motion.angle=r.real();
  auto& actions=p.motion.actions;const auto enabled=r.get<std::uint8_t>();if(enabled>1)return {Error::format,{}};actions.count=r.get<std::uint8_t>();const auto flags=r.get<std::uint8_t>();if((flags&~3u)||r.get<std::uint8_t>())return {Error::format,{}};actions.sprinting=(flags&1u)!=0;actions.guarding=(flags&2u)!=0;actions.latest=r.get<std::uint64_t>();actions.source_ms=r.get<std::uint32_t>();if(enabled)actions.epoch=p.motion.epoch;
  for(auto& event:actions.events){event.serial=r.get<std::uint64_t>();event.source_ms=r.get<std::uint32_t>();event.kind=static_cast<ActionKind>(r.get<std::uint8_t>());if(r.get<std::uint8_t>()||r.get<std::uint16_t>())return {Error::format,{}};event.direction=r.real();}
  if(!valid(p.motion))return {Error::value,{}};
 }
 if(r.at!=bytes.size()-tag_bytes)return {Error::size,{}};return {Error::none,p};
 }catch(...){return {Error::crypto,{}};}
}
}
