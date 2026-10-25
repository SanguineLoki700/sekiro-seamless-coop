#pragma once
#include "skcoop/lan/protocol.hpp"
#include <memory>
namespace skcoop::lan {
struct Room {Ticket ticket;std::uint64_t seen{};};
struct DiscoveryView {std::uint16_t port{};std::uint32_t error{};std::vector<Room> rooms;};
// Discovery is for a trusted private LAN. Room credentials travel in cleartext
// only in a unicast reply to a local probe. This is not Steam authentication.
class Discovery {
public:
 explicit Discovery(std::optional<Ticket> host={},std::uint16_t port=24861,std::vector<std::string> probe_targets={});
 ~Discovery();Discovery(const Discovery&)=delete;Discovery& operator=(const Discovery&)=delete;
 DiscoveryView read()const;void stop();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
