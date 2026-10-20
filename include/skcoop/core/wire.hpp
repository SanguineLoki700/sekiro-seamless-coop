#pragma once

#include "skcoop/core/core.hpp"

#include <optional>
#include <variant>

namespace skcoop::core {

inline constexpr std::size_t wire_header_size = 12;
inline constexpr std::size_t max_packet_bytes = 16 * 1024;
enum class MessageKind : std::uint8_t { handshake = 1, snapshot = 2, durable_event = 3, checkpoint = 4 };
using WireMessage = std::variant<Handshake, Snapshot, DurableEvent, CampaignCheckpoint>;
enum class WireError {
    none, too_large, truncated, bad_magic, unsupported_protocol,
    unknown_type, bad_length, invalid_value
};
struct EncodeResult {
    WireError error{WireError::none};
    std::vector<std::uint8_t> bytes;
    [[nodiscard]] explicit operator bool() const noexcept { return error == WireError::none; }
};
struct DecodeResult {
    WireError error{WireError::none};
    std::optional<WireMessage> message;
    [[nodiscard]] explicit operator bool() const noexcept { return message.has_value(); }
};

// Scalars use little-endian byte order, including IEEE-754 binary32 bit patterns.
// No native struct memory, pointers or implicit padding cross the wire.
[[nodiscard]] EncodeResult encode(const WireMessage&);
[[nodiscard]] DecodeResult decode(std::span<const std::uint8_t>);

} // namespace skcoop::core
