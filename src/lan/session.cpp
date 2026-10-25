#include "skcoop/lan/session.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace skcoop::lan {
Session::Session(SessionConfig c):config_(std::move(c)){
 if(!valid(config_.build)||!config_.participant||!core::valid_session_id(config_.ticket.room))throw std::invalid_argument("LAN session configuration invalid");
}
bool Session::reject(Failure f){state_=State::failed;failure_=f;remote_.reset();outgoing_.clear();return false;}
void Session::send(Kind kind){
 if(sequence_==std::numeric_limits<std::uint64_t>::max()){reject(Failure::protocol);return;}
 Packet p;p.kind=kind;p.room=config_.ticket.room;p.connection=connection_;p.sender=config_.participant;p.receiver=kind==Kind::challenge?0:peer_;p.sequence=++sequence_;p.build=config_.build;outgoing_.push_back(p);
}
void Session::opened(std::uint64_t now,const Id& connection){
 if(stage_!=Stage::closed)throw std::logic_error("LAN connection cannot reopen in one incarnation");started_=last_receive_=now;state_=State::handshaking;
 if(config_.role==Role::host){if(!core::valid_session_id(connection))throw std::invalid_argument("Missing connection incarnation");connection_=connection;stage_=Stage::hello;send(Kind::challenge);}else stage_=Stage::challenge;
}
bool Session::receive(const Packet& p,std::uint64_t now){
 if(state_==State::failed||state_==State::lost||state_==State::stopped)return false;
 if(p.room!=config_.ticket.room||!p.sender||p.sender==config_.participant||!p.sequence){++ignored_;return false;}
 if(stage_==Stage::challenge){
  if(p.kind!=Kind::challenge||p.receiver||!core::valid_session_id(p.connection)){++ignored_;return false;}
  if(p.build!=config_.build)return reject(Failure::compatibility);
  peer_=p.sender;connection_=p.connection;received_=p.sequence;last_receive_=now;send(Kind::hello);stage_=Stage::welcome;return true;
 }
 if(p.connection!=connection_||p.receiver!=config_.participant||(peer_&&p.sender!=peer_)){++ignored_;return false;}
 if(p.sequence<=received_){++ignored_;return false;}
 if(stage_==Stage::hello){
  if(p.kind!=Kind::hello)return reject(Failure::protocol);if(p.build!=config_.build)return reject(Failure::compatibility);
  peer_=p.sender;received_=p.sequence;last_receive_=now;send(Kind::welcome);stage_=Stage::ack;return true;
 }
 received_=p.sequence;last_receive_=now;
 if(stage_==Stage::welcome){if(p.kind!=Kind::welcome)return reject(Failure::protocol);if(p.build!=config_.build)return reject(Failure::compatibility);send(Kind::ack);stage_=Stage::ready;return true;}
 if(stage_==Stage::ack){if(p.kind!=Kind::ack)return reject(Failure::protocol);send(Kind::ready);stage_=Stage::active;state_=State::connected;return true;}
 if(stage_==Stage::ready){if(p.kind!=Kind::ready)return reject(Failure::protocol);stage_=Stage::active;state_=State::connected;return true;}
 if(stage_!=Stage::active)return reject(Failure::protocol);
 if(p.kind==Kind::bye){state_=State::lost;remote_.reset();return true;}
 if(p.kind==Kind::heartbeat)return true;
 if(p.kind!=Kind::frame||!valid(p.motion))return reject(Failure::protocol);
 if((config_.build.purpose==Purpose::connection_check&&p.motion.descriptor!=0)||
    (config_.build.purpose==Purpose::native_avatar&&p.motion.descriptor!=0xa000))return reject(Failure::protocol);
 remote_=p.motion;return true;
}
void Session::tick(std::uint64_t now){
 if(state_!=State::handshaking&&state_!=State::connected)return;
 const auto since=state_==State::connected?last_receive_:started_;
 if(now<since||now-since>(state_==State::connected?2000u:15000u)){state_=State::lost;failure_=Failure::timeout;remote_.reset();outgoing_.clear();}
}
void Session::send_frame(const Motion& m){if(state_!=State::connected)return;if(!valid(m))throw std::invalid_argument("Invalid local motion");send(Kind::frame);if(state_==State::connected)outgoing_.back().motion=m;}
void Session::heartbeat(){if(state_==State::connected)send(Kind::heartbeat);}
void Session::stop(){outgoing_.clear();remote_.reset();state_=State::stopped;}
std::vector<Packet> Session::take_outgoing(){auto out=std::move(outgoing_);outgoing_.clear();return out;}
std::optional<Motion> Session::take_remote(){auto r=remote_;remote_.reset();return r;}
PushResult MotionBuffer::push(const Motion& m,std::uint64_t now,std::uint64_t sequence){
 if(!valid(m)||!sequence)return PushResult::invalid;
 if(count_){const auto& last=samples_[count_-1];if(sequence<=last.sequence||now<last.time)return PushResult::stale;
  if(m.epoch!=last.motion.epoch||m.descriptor!=last.motion.descriptor)return PushResult::scene_changed;
  if(now==last.time){samples_[count_-1]={m,now,sequence};return PushResult::accepted;}}
 if(count_==samples_.size()){std::move(samples_.begin()+1,samples_.end(),samples_.begin());--count_;}
 samples_[count_++]={m,now,sequence};return PushResult::accepted;
}
std::optional<Smoothed> MotionBuffer::sample(std::uint64_t now)const{
 if(!count_||now<samples_[count_-1].time)return {};
 const auto age=now-samples_[count_-1].time;const auto target=now>100?now-100:0;
 Motion out=samples_[count_-1].motion;
 if(target<=samples_[0].time)out=samples_[0].motion;
 else for(std::size_t i=1;i<count_;++i)if(target<=samples_[i].time){
  const auto& a=samples_[i-1];const auto& b=samples_[i];const float t=static_cast<float>(target-a.time)/static_cast<float>(b.time-a.time);out=a.motion;
  for(unsigned n=0;n<3;++n){out.position[n]+=t*(b.motion.position[n]-a.motion.position[n]);out.rotation[n]+=t*std::remainder(b.motion.rotation[n]-a.motion.rotation[n],2*std::numbers::pi_v<float>);}
  out.speed+=t*(b.motion.speed-a.motion.speed);out.angle+=t*std::remainder(b.motion.angle-a.motion.angle,360.f);break;
 }
 for(auto& r:out.rotation)r=std::remainder(r,2*std::numbers::pi_v<float>);
 out.angle=std::remainder(out.angle,360.f);if(age>500)out.speed=0;
 return Smoothed{out,age,age<=500};
}
}
