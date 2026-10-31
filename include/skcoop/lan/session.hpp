#pragma once
#include "skcoop/lan/protocol.hpp"
namespace skcoop::lan {
enum class Role { host,guest };
enum class State { starting,listening,connecting,handshaking,connected,lost,failed,stopped };
enum class Failure { none,socket,authentication,protocol,compatibility,timeout,scene,internal };
struct SessionConfig { Role role{};Ticket ticket;Build build;std::uint64_t participant{}; };
struct ReceivedMotion {
 Motion motion;
 ActionScope scope; // Derived only from the accepted authenticated envelope.
 std::uint64_t packet_sequence{},arrival_ms{};
};
struct ReceivedCommand {
 actors::PlayerCommand command;
 actors::CommandScope scope; // Derived only from authenticated session envelope.
 std::uint64_t packet_sequence{},arrival_ms{};
 bool enabled{};
};
class Session {
public:
 explicit Session(SessionConfig);
 void opened(std::uint64_t now,const Id& host_connection);
 bool receive(const Packet&,std::uint64_t now);
 void tick(std::uint64_t now);
 void send_frame(const Motion&);
 void send_command(const actors::PlayerCommand&,bool enabled);
 std::optional<ReceivedCommand> take_remote_command();
 void heartbeat();void stop();
 std::vector<Packet> take_outgoing();
 State state()const noexcept{return state_;}Failure failure()const noexcept{return failure_;}
 std::uint64_t peer()const noexcept{return peer_;}
 std::uint64_t ignored()const noexcept{return ignored_;}
 std::optional<Motion> take_remote();
 std::optional<ReceivedMotion> take_remote_frame();
private:
 enum class Stage { closed,challenge,hello,welcome,ack,ready,active };
 SessionConfig config_;Stage stage_{Stage::closed};State state_{State::starting};Failure failure_{};
 Id connection_{};std::uint64_t peer_{},sequence_{},received_{},started_{},last_receive_{},ignored_{};
 std::vector<Packet> outgoing_;std::optional<ReceivedMotion> remote_;
 std::optional<ReceivedCommand> command_;
 void send(Kind);bool reject(Failure);
};
struct MotionSample { Motion motion;std::uint64_t time{},sequence{}; };
struct Smoothed { Motion motion;std::uint64_t age{};bool fresh{}; };
enum class PushResult { accepted,stale,invalid,scene_changed };
class MotionBuffer {
public:
 PushResult push(const Motion&,std::uint64_t arrival,std::uint64_t sequence);
 std::optional<Smoothed> sample(std::uint64_t now)const;
 // Diagnostics can preserve the exact pose/history pair instead of feeding
 // interpolated state back into another receiver.
 std::optional<MotionSample> newest()const noexcept {return count_?std::optional{samples_[count_-1]}:std::nullopt;}
private:
 std::array<MotionSample,8> samples_{};std::size_t count_{};
};
}
