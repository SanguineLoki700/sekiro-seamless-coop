#pragma once
#include "skcoop/core/wire.hpp"
#include "skcoop/lan/actions.hpp"
#include <optional>
#include <string>
namespace skcoop::lan {
using Id=std::array<std::uint8_t,16>;
using Key=core::Sha256;
inline constexpr std::uint16_t version=3;
inline constexpr std::size_t header_bytes=68,tag_bytes=32,max_packet_bytes=256;
inline constexpr std::uint32_t action_adapter=4;
inline constexpr std::size_t motion_body_bytes=144;
enum class Purpose:std::uint8_t { connection_check=1,native_avatar=2 };
enum class Kind:std::uint8_t { challenge=1,hello,welcome,ack,ready,frame,heartbeat,bye };
struct Build {
    Purpose purpose{Purpose::connection_check};
    std::uint32_t adapter{1};
    core::Sha256 game{},scenario{};
    bool operator==(const Build&)const=default;
};
struct Motion {
    std::uint64_t epoch{}; // Local loading incarnation; not a native map identifier.
    std::uint32_t descriptor{}; // Observed spawn descriptor, not a native map ID.
    std::array<float,3> position{},rotation{};
    float speed{},angle{}; // Graph speed [0,1], angle in degrees.
    ActionSnapshot actions{}; // All-zero disables actions for legacy/checker builds.
    bool operator==(const Motion&)const=default;
};
struct Packet {
    Kind kind{};Id room{},connection{};
    std::uint64_t sender{},receiver{},sequence{};
    Build build{};Motion motion{};
};
enum class Error { none,size,format,version,room,authentication,value,crypto };
struct Decoded { Error error{Error::none};std::optional<Packet> packet;explicit operator bool()const noexcept{return error==Error::none&&packet.has_value();} };
struct Ticket { std::string address;std::uint16_t port{};Id room{};Key key{};bool operator==(const Ticket&)const=default; };
bool valid(const Build&)noexcept;
bool valid(const Motion&)noexcept;
bool valid(const Build&,const Motion&)noexcept;
bool valid_ipv4(std::string_view)noexcept;
Id random_id();Key random_key();std::uint64_t random_participant();
std::string ticket_text(const Ticket&); // Credential: display/copy, never write to receipts.
std::optional<Ticket> parse_ticket(std::string_view);
std::vector<std::uint8_t> encode(const Packet&,const Key&);
Decoded decode(std::span<const std::uint8_t>,const Id& expected_room,const Key&);
}
