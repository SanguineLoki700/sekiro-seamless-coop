#pragma once
#include "skcoop/lan/session.hpp"
#include <memory>
namespace skcoop::lan {
struct Status {
 State state{State::starting};Failure failure{};
 std::uint32_t system_error{};std::uint16_t local_port{};
 std::uint64_t participant{},peer{},sent{},received{},frames_sent{},frames_received{},rejected{};
};
struct View { Status status;MotionBuffer remote; };
// All sockets, framing and crypto run on one background thread. The game uses
// only try_offer/try_read; neither waits on that thread or performs network I/O.
class Peer {
public:
 explicit Peer(SessionConfig,std::string bind_address="0.0.0.0");
 ~Peer();Peer(const Peer&)=delete;Peer& operator=(const Peer&)=delete;
 bool try_offer(const Motion&);bool try_read(View&)const;
 View read()const; // GUI/tests only: short blocking mutex acquisition.
 void request_stop()noexcept;void stop(); // join only outside DllMain/game callbacks.
private:struct Impl;std::unique_ptr<Impl> impl_;
};
std::vector<std::string> local_addresses();
}
