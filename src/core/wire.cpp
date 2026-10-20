#include "skcoop/core/wire.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <type_traits>

namespace skcoop::core {
namespace {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
constexpr std::array<std::uint8_t, 4> magic{'S', 'K', 'C', '1'};

struct Writer {
    std::vector<std::uint8_t> bytes;
    template <typename T> void number(T value) {
        static_assert(std::is_unsigned_v<T>);
        for (std::size_t i = 0; i < sizeof(T); ++i)
            bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    }
    template <std::size_t N> void fixed(const std::array<std::uint8_t, N>& value) {
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    void real(float value) { number(std::bit_cast<std::uint32_t>(value)); }
};

struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t offset{};
    bool good{true};
    bool take(std::size_t size) {
        if (!good || size > bytes.size() - offset) { good = false; return false; }
        return true;
    }
    template <typename T> T number() {
        static_assert(std::is_unsigned_v<T>);
        if (!take(sizeof(T))) return 0;
        std::uint64_t value{};
        for (std::size_t i = 0; i < sizeof(T); ++i)
            value |= std::uint64_t{bytes[offset++]} << (i * 8);
        return static_cast<T>(value);
    }
    template <std::size_t N> std::array<std::uint8_t, N> fixed() {
        std::array<std::uint8_t, N> value{};
        if (take(N)) {
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), N, value.begin());
            offset += N;
        }
        return value;
    }
    float real() { return std::bit_cast<float>(number<std::uint32_t>()); }
};

void write_checkpoint(Writer& out, const CampaignCheckpoint& checkpoint) {
    out.fixed(checkpoint.campaign);
    out.number(checkpoint.revision);
    out.fixed(checkpoint.state_sha256);
}
CampaignCheckpoint read_checkpoint(Reader& in) {
    CampaignCheckpoint result;
    result.campaign = in.fixed<16>();
    result.revision = in.number<std::uint64_t>();
    result.state_sha256 = in.fixed<32>();
    return result;
}

void write_entity(Writer& out, const EntityState& entity) {
    out.number(entity.id.map);
    out.number(entity.id.generation);
    out.number(entity.id.object);
    out.number(static_cast<std::uint8_t>(entity.kind));
    out.number(entity.owner);
    for (auto value : entity.position) out.real(value);
    out.real(entity.yaw);
    out.number(entity.hit_points);
    out.number(entity.posture);
    out.number(entity.animation);
}
EntityState read_entity(Reader& in) {
    EntityState entity;
    entity.id.map = in.number<std::uint32_t>();
    entity.id.generation = in.number<std::uint32_t>();
    entity.id.object = in.number<std::uint64_t>();
    entity.kind = static_cast<EntityKind>(in.number<std::uint8_t>());
    entity.owner = in.number<std::uint64_t>();
    for (auto& value : entity.position) value = in.real();
    entity.yaw = in.real();
    entity.hit_points = in.number<std::uint32_t>();
    entity.posture = in.number<std::uint32_t>();
    entity.animation = in.number<std::uint32_t>();
    return entity;
}

DecodeResult fail(WireError error) { return {error, std::nullopt}; }
DecodeResult finish(Reader& reader, WireMessage value, bool valid) {
    if (!reader.good) return fail(WireError::truncated);
    if (reader.offset != reader.bytes.size()) return fail(WireError::bad_length);
    if (!valid) return fail(WireError::invalid_value);
    return {WireError::none, std::move(value)};
}
} // namespace

EncodeResult encode(const WireMessage& message) {
    Writer payload;
    auto kind = MessageKind::handshake;
    WireError error = WireError::none;
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Handshake>) {
            kind = MessageKind::handshake;
            if (value.protocol != protocol_version) { error = WireError::unsupported_protocol; return; }
            if (value.player == 0 || !valid_build_identity(value.build) ||
                !valid_campaign(value.checkpoint.campaign) ||
                (!value.has_checkpoint && (value.checkpoint.revision != 0 ||
                                          value.checkpoint.state_sha256 != Sha256{}))) {
                error = WireError::invalid_value; return;
            }
            payload.number(value.player);
            payload.fixed(value.build.game_sha256);
            payload.fixed(value.build.profile_sha256);
            write_checkpoint(payload, value.checkpoint);
            payload.number(static_cast<std::uint8_t>(value.has_checkpoint));
        } else if constexpr (std::is_same_v<T, Snapshot>) {
            kind = MessageKind::snapshot;
            if (!valid_snapshot(value)) { error = WireError::invalid_value; return; }
            payload.number(value.map);
            payload.number(value.generation);
            payload.number(value.sequence);
            payload.number(static_cast<std::uint16_t>(value.entities.size()));
            for (const auto& entity : value.entities) write_entity(payload, entity);
        } else if constexpr (std::is_same_v<T, DurableEvent>) {
            kind = MessageKind::durable_event;
            if (!valid_event(value)) { error = WireError::invalid_value; return; }
            payload.fixed(value.campaign);
            payload.number(value.revision);
            payload.number(value.id.issuer);
            payload.number(value.id.sequence);
            payload.number(static_cast<std::uint8_t>(value.kind));
            payload.number(value.subject);
            payload.number(value.value);
        } else if constexpr (std::is_same_v<T, CampaignCheckpoint>) {
            kind = MessageKind::checkpoint;
            if (!valid_campaign(value.campaign)) { error = WireError::invalid_value; return; }
            write_checkpoint(payload, value);
        }
    }, message);
    if (error != WireError::none) return {error, {}};
    if (payload.bytes.size() > max_packet_bytes - wire_header_size) return {WireError::too_large, {}};
    Writer packet;
    packet.bytes.reserve(wire_header_size + payload.bytes.size());
    packet.fixed(magic);
    packet.number(protocol_version);
    packet.number(static_cast<std::uint8_t>(kind));
    packet.number(std::uint8_t{0});
    packet.number(static_cast<std::uint32_t>(payload.bytes.size()));
    packet.bytes.insert(packet.bytes.end(), payload.bytes.begin(), payload.bytes.end());
    return {WireError::none, std::move(packet.bytes)};
}

DecodeResult decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > max_packet_bytes) return fail(WireError::too_large);
    if (bytes.size() < wire_header_size) return fail(WireError::truncated);
    Reader header{bytes.first(wire_header_size)};
    if (header.fixed<4>() != magic) return fail(WireError::bad_magic);
    if (header.number<std::uint16_t>() != protocol_version) return fail(WireError::unsupported_protocol);
    const auto kind = static_cast<MessageKind>(header.number<std::uint8_t>());
    if (header.number<std::uint8_t>() != 0) return fail(WireError::invalid_value);
    const auto payload_size = header.number<std::uint32_t>();
    if (payload_size != bytes.size() - wire_header_size) return fail(WireError::bad_length);
    Reader in{bytes.subspan(wire_header_size)};
    switch (kind) {
    case MessageKind::handshake: {
        Handshake value;
        value.player = in.number<std::uint64_t>();
        value.build.game_sha256 = in.fixed<32>();
        value.build.profile_sha256 = in.fixed<32>();
        value.checkpoint = read_checkpoint(in);
        const auto has_checkpoint = in.number<std::uint8_t>();
        value.has_checkpoint = has_checkpoint == 1;
        const bool valid = has_checkpoint <= 1 && value.player != 0 && valid_build_identity(value.build) &&
            valid_campaign(value.checkpoint.campaign) &&
            (value.has_checkpoint || (value.checkpoint.revision == 0 && value.checkpoint.state_sha256 == Sha256{}));
        return finish(in, value, valid);
    }
    case MessageKind::snapshot: {
        Snapshot value;
        value.map = in.number<std::uint32_t>();
        value.generation = in.number<std::uint32_t>();
        value.sequence = in.number<std::uint64_t>();
        const auto count = in.number<std::uint16_t>();
        if (!in.good) return fail(WireError::truncated);
        if (count > max_snapshot_entities) return fail(WireError::too_large);
        constexpr std::size_t entity_wire_size = 53;
        if (in.bytes.size() - in.offset != static_cast<std::size_t>(count) * entity_wire_size)
            return fail(WireError::bad_length);
        value.entities.reserve(count);
        for (std::uint16_t i = 0; i < count; ++i) value.entities.push_back(read_entity(in));
        const bool valid = valid_snapshot(value);
        return finish(in, std::move(value), valid);
    }
    case MessageKind::durable_event: {
        DurableEvent value;
        value.campaign = in.fixed<16>();
        value.revision = in.number<std::uint64_t>();
        value.id.issuer = in.number<std::uint64_t>();
        value.id.sequence = in.number<std::uint64_t>();
        value.kind = static_cast<EventKind>(in.number<std::uint8_t>());
        value.subject = in.number<std::uint64_t>();
        value.value = in.number<std::uint32_t>();
        return finish(in, value, valid_event(value));
    }
    case MessageKind::checkpoint: {
        const auto value = read_checkpoint(in);
        return finish(in, value, valid_campaign(value.campaign));
    }
    default: return fail(WireError::unknown_type);
    }
}

} // namespace skcoop::core
