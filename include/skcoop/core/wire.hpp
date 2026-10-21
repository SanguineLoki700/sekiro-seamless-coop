#pragma once

#include "skcoop/core/core.hpp"

#include <optional>
#include <variant>

namespace skcoop::core {

inline constexpr std::size_t wire_header_size = 28;
inline constexpr std::size_t max_packet_bytes = 16 * 1024;
// An incarnation of a running host session, separate from the persistent campaign.
// This is a declared scope identifier, not authentication or a secret credential.
using SessionId = std::array<std::uint8_t, 16>;
[[nodiscard]] bool valid_session_id(const SessionId&) noexcept;
enum class MessageKind : std::uint8_t { handshake = 1, snapshot = 2, durable_event = 3, checkpoint = 4 };
using WireMessage = std::variant<Handshake, Snapshot, DurableEvent, CampaignCheckpoint>;
enum class WireError {
    none, too_large, truncated, bad_magic, unsupported_protocol,
    unknown_type, bad_length, invalid_value, invalid_session, wrong_session
};
struct EncodeResult {
    WireError error{WireError::none};
    std::vector<std::uint8_t> bytes;
    [[nodiscard]] explicit operator bool() const noexcept { return error == WireError::none; }
};
struct DecodeResult {
    WireError error{WireError::none};
    std::optional<WireMessage> message;
    SessionId session{};
    [[nodiscard]] explicit operator bool() const noexcept {
        return error == WireError::none && message.has_value();
    }
    // Revalidate when consuming a queued result: a host load/restart may have
    // rotated the active incarnation after this packet was decoded.
    [[nodiscard]] bool matches_session(const SessionId& active) const noexcept {
        return static_cast<bool>(*this) && valid_session_id(active) && session == active;
    }
};

// Scalars use little-endian byte order, including IEEE-754 binary32 bit patterns.
// No native struct memory, pointers or implicit padding cross the wire.
// Every message kind has a session-bound envelope. There is deliberately no
// context-free decoder: expected_session comes from the caller's active session,
// never from the packet being decoded. Raw state APIs are trusted internal APIs.
[[nodiscard]] EncodeResult encode(const WireMessage&, const SessionId& session);
[[nodiscard]] DecodeResult decode(std::span<const std::uint8_t>, const SessionId& expected_session);

} // namespace skcoop::core
