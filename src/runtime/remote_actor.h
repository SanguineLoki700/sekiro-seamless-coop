#pragma once
#include "actor_demo.h"
#include "skcoop/lan/session.hpp"
namespace skcoop::actors {
class RemoteGame:public Game {
public:virtual void remote_motion(Token,const lan::Motion&)=0;
};
enum class RemoteWait:long { local,connection,frame,separation,area,ready };
class RemoteActor {
public:
 void tick(RemoteGame&,const Frame&,bool local_ready,bool armed,lan::State,const std::optional<lan::Smoothed>&);
 Phase phase{Phase::waiting};Error error{};RemoteWait waiting{RemoteWait::local};
 bool stop_requested{},paused{};
 std::uint64_t creation_calls{},ticks{},pose_writes{},commands{},stale_frames{},displacement_mm{};
 void fail(Error e)noexcept{phase=Phase::failed;error=e;}
 // Revalidate the local lifetime before any additional action release/replay.
 // A changed world, source or registry must never trigger writes to an old actor.
 Token owned_actor(const Frame&)const noexcept;
private:
 Frame original_{};Token actor_{};std::uint64_t created_{},placed_{},active_at_{},remote_epoch_{};
 Vector origin_{};
 void stop(RemoteGame&,bool release);
};
}
