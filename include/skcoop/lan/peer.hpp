#pragma once
#include "skcoop/lan/session.hpp"
#include <memory>
#include <span>
namespace skcoop::lan {
struct Status {
 State state{State::starting};Failure failure{};
 std::uint32_t system_error{};std::uint16_t local_port{};
 std::uint64_t participant{},peer{},sent{},received{},frames_sent{},frames_received{},rejected{};
 ActionFault action_fault{};
};
struct ActionFrame {
 ActionScope scope;
 ActionSnapshot history;
 std::uint64_t packet_sequence{},arrival_ms{};
 bool operator==(const ActionFrame&)const=default;
};
enum class ActionFramePush { accepted,stale,failed };
// Histories remain separate from pose interpolation. Batches containing new
// serials retain their original envelope/arrival until explicitly acknowledged.
// Repeated histories refresh latest(), never overwrite the first batch arrival.
// A newer frame after expiry begins an internal baseline generation: retained
// expired batches are discarded, and consumers must baseline without replay.
class ActionFrameBuffer {
public:
 ActionFramePush push(const ActionFrame&)noexcept;
 bool acknowledge(std::uint64_t packet_sequence)noexcept;
 const std::optional<ActionFrame>& latest()const noexcept{return latest_;}
 std::span<const ActionFrame> pending()const noexcept{return {pending_.data(),count_};}
 std::uint64_t baseline_generation()const noexcept{return baseline_generation_;}
 ActionFault fault()const noexcept{return fault_;}
private:
 std::array<ActionFrame,action_queue_capacity> pending_{};
 std::optional<ActionFrame> latest_;
 std::size_t count_{};
 std::uint64_t acknowledged_{},baseline_generation_{};
 ActionFault fault_{};
 ActionFramePush fail(ActionFault)noexcept;
};
struct View { Status status;MotionBuffer remote;ActionFrameBuffer actions; };
// All sockets, framing and crypto run on one background thread. The game uses
// only try_offer/try_read; neither waits on that thread or performs network I/O.
class Peer {
public:
 explicit Peer(SessionConfig,std::string bind_address="0.0.0.0");
 ~Peer();Peer(const Peer&)=delete;Peer& operator=(const Peer&)=delete;
 bool try_offer(const Motion&);bool try_read(View&)const;
 // Bounded durable hand-offs: no latest-only overwrite of unsent input edges.
 bool try_offer_command(const actors::PlayerCommand&,bool enabled);
 bool try_take_commands(std::array<ReceivedCommand,actors::command_queue_capacity>&,std::size_t&);
 bool try_set_command_following(bool);
 bool try_ack_actions(std::uint64_t packet_sequence); // Nonblocking hand-off ACK.
 View read()const; // GUI/tests only: short blocking mutex acquisition.
 void request_stop()noexcept;void stop(); // join only outside DllMain/game callbacks.
private:struct Impl;std::unique_ptr<Impl> impl_;
};
std::vector<std::string> local_addresses();
}
