#include "remote_actor.h"
#include <algorithm>
#include <cmath>
namespace skcoop::actors {
namespace {
Vector position(const lan::Motion& m){return {m.position[0],m.position[1],m.position[2],1};}
float distance(const Vector& a,const Vector& b){return std::hypot(a[0]-b[0],a[1]-b[1],a[2]-b[2]);}
}
void RemoteActor::stop(RemoteGame& game,bool release){if(release){lan::Motion zero;game.remote_motion(actor_,zero);++commands;}phase=Phase::stopped;}
Token RemoteActor::owned_actor(const Frame& f)const noexcept{
 return creation_calls&&actor_&&f.primary_ready&&f.world==original_.world&&f.primary==original_.primary&&
  f.registry==original_.registry&&f.field_handle==original_.field_handle&&f.guest==actor_?actor_:0;
}
void RemoteActor::tick(RemoteGame& game,const Frame& f,bool local_ready,bool armed,lan::State network,const std::optional<lan::Smoothed>& incoming){
 if(phase==Phase::failed||phase==Phase::stopped)return;++ticks;
 if(!local_ready){waiting=RemoteWait::local;if(creation_calls){phase=Phase::stopped;error=Error::changed_world;}return;}
 if(!valid_position(f.position)){fail(Error::invalid_pose);return;}
 if(creation_calls&&(f.world!=original_.world||f.primary!=original_.primary||f.registry!=original_.registry||f.field_handle!=original_.field_handle||f.guest!=actor_)){
  phase=Phase::stopped;error=Error::changed_world;return;
 }
 if(stop_requested){stop(game,creation_calls&&phase==Phase::following);return;}
 if(network!=lan::State::connected){waiting=RemoteWait::connection;if(creation_calls)stop(game,phase==Phase::following);return;}
 if(!creation_calls){
  if(!armed)return;
  if(!incoming||!incoming->fresh){waiting=RemoteWait::frame;return;}
  const auto& m=incoming->motion;if(!lan::valid(m)){fail(Error::input_value);return;}
  const auto gap=distance(f.position,position(m));
  if(m.descriptor!=f.field_handle||m.descriptor!=0xa000||gap>25){waiting=RemoteWait::area;return;}
  if(gap<1){waiting=RemoteWait::separation;return;}
  if(f.guest||!f.world||!f.primary||!f.registry){fail(Error::binding);return;}
  original_=f;remote_epoch_=m.epoch;++creation_calls;actor_=game.create();created_=ticks;
  if(!actor_||actor_==f.primary){fail(Error::factory);return;}phase=Phase::placing;return;
 }
 if(incoming&&incoming->motion.epoch!=remote_epoch_){stop(game,phase==Phase::following);error=Error::changed_world;return;}
 if(phase==Phase::placing){
  if(!game.actor_ready(actor_)){if(ticks-created_>=120)fail(Error::model_timeout);return;}
  if(!incoming||!incoming->fresh){waiting=RemoteWait::frame;return;}
  if(!lan::valid(incoming->motion)){fail(Error::input_value);return;}
  if(incoming->motion.descriptor!=f.field_handle||distance(f.position,position(incoming->motion))>25){fail(Error::invalid_pose);return;}
  game.place(actor_,position(incoming->motion));++pose_writes;placed_=ticks;phase=Phase::settling;return;
 }
 if(!game.actor_ready(actor_)){phase=Phase::stopped;error=Error::changed_world;return;}
 if(phase==Phase::settling){if(ticks-placed_<60)return;origin_=game.position(actor_);if(!valid_position(origin_)){fail(Error::invalid_pose);return;}active_at_=f.milliseconds;phase=Phase::following;waiting=RemoteWait::ready;return;}
 if(f.milliseconds<active_at_||f.milliseconds-active_at_>=120000){stop(game,true);return;}
 if(paused||!incoming||!incoming->fresh){lan::Motion zero;game.remote_motion(actor_,zero);++commands;++stale_frames;return;}
 const auto& m=incoming->motion;
 if(!lan::valid(m)||m.descriptor!=f.field_handle){stop(game,true);fail(Error::input_value);return;}
 const auto current=game.position(actor_),target=position(m);
 if(!valid_position(current)||!valid_position(target)||distance(current,target)>10||distance(f.position,target)>25){stop(game,true);fail(Error::invalid_pose);return;}
 displacement_mm=std::max(displacement_mm,static_cast<std::uint64_t>(std::llround(distance(current,origin_)*1000.)));
 game.place(actor_,target);++pose_writes;game.remote_motion(actor_,m);++commands;
}
}
