#include "skcoop/core/core.hpp"
#include "skcoop/core/wire.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace skcoop::core;

namespace {
void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
CampaignId campaign() { CampaignId value{}; value[0] = 0x11; return value; }
BuildIdentity build() {
    BuildIdentity value;
    value.game_sha256.fill(0x42);
    value.profile_sha256.fill(0x77);
    return value;
}
CampaignCheckpoint checkpoint(std::uint64_t revision = 0) {
    CampaignCheckpoint value{campaign(), revision, {}};
    value.state_sha256.fill(static_cast<std::uint8_t>(revision + 1));
    return value;
}
Handshake hello(SteamId id, std::uint64_t revision = 0) {
    return {protocol_version, id, build(), checkpoint(revision)};
}
Snapshot snapshot(std::uint32_t generation = 1, std::uint64_t sequence = 1) {
    Snapshot value{100, generation, sequence, {}};
    value.entities.push_back({{100, generation, 10}, EntityKind::enemy, 1001,
                              {1.25f, -2.0f, 3.5f}, 0.5f, 100, 20, 300});
    return value;
}
DurableEvent reward(std::uint64_t revision = 1, std::uint32_t amount = 10) {
    return {campaign(), revision, {1001, revision}, EventKind::grant_reward, 1002, amount};
}
void put16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value);
    bytes.at(at + 1) = static_cast<std::uint8_t>(value >> 8);
}
void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<std::uint8_t>(value >> (i * 8));
}
SessionId incarnation(std::uint8_t first = 0x51) {
    SessionId value{};
    value[0] = first;
    value[15] = 0xa7;
    return value;
}
// The original non-session-specific cases share one declared test incarnation.
// These conveniences are test-local; the public codec has no one-argument API.
EncodeResult encode(const WireMessage& message) {
    return skcoop::core::encode(message, incarnation());
}
DecodeResult decode(std::span<const std::uint8_t> bytes) {
    return skcoop::core::decode(bytes, incarnation());
}
void roundtrip(const WireMessage& message) {
    const auto packet = encode(message);
    require(static_cast<bool>(packet), "valid message must encode");
    const auto decoded = decode(packet.bytes);
    require(static_cast<bool>(decoded), "valid packet must decode");
    require(*decoded.message == message, "roundtrip changed a field");
    for (std::size_t length = 0; length < packet.bytes.size(); ++length)
        require(!decode(std::span(packet.bytes).first(length)), "truncated packet accepted");
}

void handshake_identity() {
    SessionRoster session(1001, build(), checkpoint());
    std::array<BuildIdentity, 3> unresolved{BuildIdentity{}, build(), build()};
    unresolved[1].game_sha256 = {};
    unresolved[2].profile_sha256 = {};
    for (std::size_t i = 0; i < unresolved.size(); ++i) {
        bool rejected{};
        try { (void)SessionRoster(1001, unresolved[i], checkpoint()); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "unresolved host build identity accepted");
        auto zero_request = hello(1002);
        zero_request.build = unresolved[i];
        require(!encode(zero_request), "unresolved handshake build identity encoded");
        require(!session.admit(zero_request).accepted(), "unresolved guest identity admitted");

        auto packet = encode(hello(1002)).bytes;
        constexpr std::size_t game_hash_offset = wire_header_size + 8;
        const auto offset = game_hash_offset + (i == 2 ? 32 : 0);
        const auto count = i == 0 ? 64 : 32;
        std::fill_n(packet.begin() + static_cast<std::ptrdiff_t>(offset), count, std::uint8_t{0});
        require(decode(packet).error == WireError::invalid_value, "unresolved wire identity accepted");
    }
    auto request = hello(1002);
    request.protocol++;
    require(session.admit(request).error == JoinError::protocol_mismatch, "protocol mismatch accepted");
    request = hello(1002); request.build.game_sha256[31] ^= 1;
    require(session.admit(request).error == JoinError::game_mismatch, "last game digest byte ignored");
    request = hello(1002); request.build.profile_sha256[0] ^= 1;
    require(session.admit(request).error == JoinError::profile_mismatch, "profile mismatch accepted");
    request = hello(1002); request.checkpoint.campaign[15] ^= 1;
    require(session.admit(request).error == JoinError::wrong_campaign, "wrong campaign accepted");
    require(session.admit(hello(0)).error == JoinError::invalid_player, "zero identity accepted");
    require(session.admit(hello(1001)).error == JoinError::invalid_player, "host duplicate accepted");
    require(session.connected_count() == 1, "rejected handshakes consumed slots");
    require(session.admit(hello(1002)).accepted(), "exact identity rejected");
    auto fresh = hello(1003);
    fresh.has_checkpoint = false;
    fresh.checkpoint.state_sha256 = {};
    const auto fresh_result = session.admit(fresh);
    require(fresh_result.accepted() && fresh_result.needs_snapshot, "fresh guest at revision zero skipped snapshot");
    roundtrip(fresh);
}

void roster_capacity_and_rejoin() {
    SessionRoster session(1001, build(), checkpoint());
    for (SteamId id = 1002; id <= 1004; ++id)
        require(session.admit(hello(id)).status == JoinStatus::accepted, "guest rejected");
    require(session.connected_count() == 4, "must include host in four-player limit");
    require(session.admit(hello(1005)).error == JoinError::full, "fifth player accepted");
    require(session.admit(hello(1003)).status == JoinStatus::already_connected, "duplicate consumed a slot");
    require(session.disconnect(1003), "guest failed to disconnect");
    require(session.admit(hello(1005)).error == JoinError::full, "disconnect silently released identity");
    require(session.admit(hello(1003)).status == JoinStatus::reconnected, "stable identity not reconnected");
    require(session.members()[2].player == 1003, "reconnection changed reserved slot");
    require(!session.disconnect(1001), "host migration was silently enabled");
    require(!session.release_disconnected(1003), "connected guest released");
    require(session.disconnect(1003) && session.release_disconnected(1003), "explicit guest release failed");
    require(session.admit(hello(1005)).accepted(), "released slot not reusable");
}

void checkpoint_reconnection() {
    const auto host = checkpoint(3);
    require(validate_reconnect(host, checkpoint(2)) == ReconnectResult::needs_snapshot, "stale checkpoint accepted as current");
    require(validate_reconnect(host, checkpoint(4)) == ReconnectResult::peer_ahead, "ahead checkpoint silently rolled back");
    auto conflicting = host; conflicting.state_sha256[0] ^= 1;
    require(validate_reconnect(host, conflicting) == ReconnectResult::conflict, "equal revision with different state accepted");
    SessionRoster session(1001, build(), host);
    const auto accepted = session.admit(hello(1002, 2));
    require(accepted.accepted() && accepted.needs_snapshot, "late join should require snapshot");
    require(!session.members()[1].synchronized, "stale guest marked ready");
    require(!session.confirm_checkpoint(1002, checkpoint(2)), "stale ack accepted");
    require(session.confirm_checkpoint(1002, host), "current ack rejected");
    require(session.members()[1].synchronized, "current ack did not unlock guest");
    require(!session.advance_checkpoint(checkpoint(2)), "checkpoint revision regressed");
    require(!session.advance_checkpoint(conflicting), "same-revision rewrite accepted");
    require(session.advance_checkpoint(checkpoint(4)), "new checkpoint rejected");
    require(!session.members()[1].synchronized, "new checkpoint did not require ack");
}

void ownership_rules() {
    require(can_simulate(EntityKind::enemy, 1001, 1001, 1001), "host cannot simulate enemy");
    require(!can_simulate(EntityKind::enemy, 1001, 1002, 1001), "guest owns enemy");
    require(!can_simulate(EntityKind::enemy, 1002, 1001, 1001), "enemy owner mismatch ignored");
    require(can_simulate(EntityKind::player, 1002, 1002, 1001), "local player cannot simulate self");
    require(!can_simulate(EntityKind::player, 1002, 1003, 1001), "peer simulates another player");
    require(!can_simulate(static_cast<EntityKind>(99), 1002, 1002, 1001), "unknown kind accepted");
}

void snapshots_reject_stale_and_invalid() {
    SnapshotStore store(1001);
    require(store.begin_area(100, 1), "initial generation rejected");
    auto state = snapshot();
    require(store.apply(state, 1002) == SnapshotResult::not_host, "guest snapshot accepted");
    require(store.apply(state, 1001) == SnapshotResult::applied, "host snapshot rejected");
    require(store.apply(state, 1001) == SnapshotResult::stale_sequence, "duplicate sequence applied");
    state.sequence = 2;
    state.entities[0].position[0] = std::numeric_limits<float>::quiet_NaN();
    require(store.apply(state, 1001) == SnapshotResult::invalid, "NaN accepted");
    require(store.sequence() == 1 && store.entities()[0].position[0] == 1.25f, "invalid snapshot partially applied");
    state = snapshot(1, 2); state.entities.push_back(state.entities[0]);
    require(store.apply(state, 1001) == SnapshotResult::invalid, "duplicate entity key accepted");
    state = snapshot(1, 2); state.entities[0].owner = 1002;
    require(store.apply(state, 1001) == SnapshotResult::invalid, "enemy ownership mismatch accepted");
    require(store.begin_area(200, 2), "new area generation rejected");
    require(store.entities().empty(), "old area's entities survived transition");
    require(store.apply(snapshot(1, 99), 1001) == SnapshotResult::wrong_area, "delayed old-generation snapshot accepted");
    require(!store.begin_area(100, 1) && !store.begin_area(200, 2), "generation reused");
    state = snapshot(2); state.map = 200; state.entities[0].id.map = 200;
    require(store.apply(state, 1001) == SnapshotResult::applied, "new area did not accept sequence reset");
}

void durable_idempotence_and_authority() {
    DurableLedger ledger(campaign(), 1001);
    auto event = reward();
    require(ledger.apply(event, 1002) == EventResult::not_host, "guest granted reward");
    require(ledger.apply(event, 1001) == EventResult::applied, "host reward rejected");
    require(ledger.apply(event, 1001) == EventResult::duplicate, "replay not identified");
    require(ledger.reward_balance(1002) == 10 && ledger.revision() == 1, "replayed reward duplicated");
    event.value = 11;
    require(ledger.apply(event, 1001) == EventResult::conflicting_id, "conflicting receipt accepted");
    event = reward(3);
    require(ledger.apply(event, 1001) == EventResult::out_of_order, "missing revision ignored");
    event = reward(2); event.campaign[1] = 1;
    require(ledger.apply(event, 1001) == EventResult::wrong_campaign, "cross-campaign reward applied");
    event = reward(2); event.kind = EventKind::set_flag; event.subject = 500; event.value = 1;
    require(ledger.apply(event, 1001) == EventResult::applied && ledger.flag(500), "flag not applied");
    event.revision = 3; event.id.sequence = 3; event.value = 0;
    require(ledger.apply(event, 1001) == EventResult::applied && !ledger.flag(500), "flag clear not applied");
}

void durable_restore_is_atomic() {
    DurableLedger ledger(campaign(), 1001);
    require(ledger.apply(reward(), 1001) == EventResult::applied, "seed failed");
    const std::vector<DurableEvent> good{reward(), reward(2, 30)};
    require(ledger.restore(checkpoint(2), good), "valid journal restore failed");
    require(ledger.reward_balance(1002) == 40, "restore lost reward ledger");
    require(ledger.apply(reward(), 1001) == EventResult::duplicate, "restore lost dedup receipts");
    require(ledger.restore(checkpoint(2), good), "identity restore at the same revision rejected");
    auto rewritten = good;
    rewritten[1].value = 31;
    require(!ledger.restore(checkpoint(2), rewritten), "same-revision restore rewrote confirmed payload");
    rewritten = good;
    rewritten[1].id.sequence = 22;
    require(!ledger.restore(checkpoint(2), rewritten), "same-revision restore replaced confirmed event ID");
    rewritten = good;
    rewritten[0].value = 11;
    rewritten.push_back(reward(3, 50));
    require(!ledger.restore(checkpoint(3), rewritten), "forward restore rewrote confirmed history prefix");
    require(ledger.revision() == 2 && ledger.reward_balance(1002) == 40 && ledger.events()[0] == good[0] &&
            ledger.events()[1] == good[1], "conflicting restore altered confirmed state");
    auto bad = good; bad[1].revision = 3;
    require(!ledger.restore(checkpoint(3), bad), "journal gap accepted");
    require(ledger.revision() == 2 && ledger.reward_balance(1002) == 40, "failed restore mutated state");
    require(!ledger.restore(checkpoint(1), std::span(good).first(1)), "restore silently rolled state back");
    bad = good; bad.push_back(good[1]);
    require(!ledger.restore(checkpoint(3), bad), "duplicate journal receipt accepted");
    auto extended = good;
    extended.push_back(reward(3, 50));
    require(ledger.restore(checkpoint(3), extended), "valid prefix-preserving forward restore rejected");
    require(ledger.revision() == 3 && ledger.reward_balance(1002) == 90, "valid forward restore lost effects");
}

void durable_capacity_preserves_receipts() {
    DurableLedger ledger(campaign(), 1001);
    for (std::uint64_t i = 1; i <= max_durable_events; ++i)
        require(ledger.apply(reward(i, 1), 1001) == EventResult::applied, "journal filled too early");
    require(ledger.apply(reward(max_durable_events + 1), 1001) == EventResult::journal_full, "journal bound ignored");
    require(ledger.apply(reward(1, 1), 1001) == EventResult::duplicate, "full journal dropped old dedup receipts");
    require(ledger.reward_balance(1002) == max_durable_events, "capacity changed balances");
}

void wire_golden_little_endian() {
    auto value = checkpoint(0x0102030405060708ULL);
    value.state_sha256.fill(0xa5);
    std::vector<std::uint8_t> golden{
        'S', 'K', 'C', '1', 2, 0, 4, 0, 56, 0, 0, 0,
        0x51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xa7,
        0x11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        8, 7, 6, 5, 4, 3, 2, 1
    };
    golden.insert(golden.end(), 32, 0xa5);
    const auto encoded = encode(value);
    require(encoded.bytes == golden, "wire format is not canonical little-endian");
    const auto decoded = decode(golden);
    require(decoded && std::get<CampaignCheckpoint>(*decoded.message) == value, "golden packet rejected");
    roundtrip(value);
    roundtrip(hello(1002));
    roundtrip(snapshot());
    roundtrip(reward());
}

void wire_rejects_malformed() {
    const auto valid = encode(snapshot()).bytes;
    auto bytes = valid; bytes[0] ^= 1;
    require(decode(bytes).error == WireError::bad_magic, "bad magic accepted");
    bytes = valid; put16(bytes, 4, protocol_version + 1);
    require(decode(bytes).error == WireError::unsupported_protocol, "unknown protocol accepted");
    bytes = valid; bytes[6] = 99;
    require(decode(bytes).error == WireError::unknown_type, "unknown message type accepted");
    bytes = valid; bytes[7] = 1;
    require(decode(bytes).error == WireError::invalid_value, "reserved bits accepted");
    bytes = valid; put32(bytes, 8, 0xffffffffU);
    require(decode(bytes).error == WireError::bad_length, "oversized declared payload accepted");
    bytes = valid; bytes.push_back(0);
    require(decode(bytes).error == WireError::bad_length, "trailing bytes accepted");
    bytes = valid; put16(bytes, wire_header_size + 16, 129);
    require(decode(bytes).error == WireError::too_large, "unbounded entity count accepted");
    bytes = valid; put32(bytes, wire_header_size + 18 + 25, 0x7f800000U);
    require(decode(bytes).error == WireError::invalid_value, "wire infinity accepted");
    bytes = valid; bytes[wire_header_size + 18 + 16] = 99;
    require(decode(bytes).error == WireError::invalid_value, "unknown entity kind accepted");
    bytes.assign(max_packet_bytes + 1, 0);
    require(decode(bytes).error == WireError::too_large, "oversized packet accepted");
    auto too_many = snapshot(); too_many.entities.resize(max_snapshot_entities + 1);
    require(!encode(too_many), "oversized snapshot encoded");
    auto invalid = reward(); invalid.kind = static_cast<EventKind>(99);
    require(!encode(invalid), "unknown durable event encoded");
}

void wire_mutation_boundaries() {
    const auto original = encode(snapshot()).bytes;
    for (std::size_t at = 0; at < original.size(); ++at) {
        auto bytes = original; bytes[at] ^= 0x80;
        const auto result = decode(bytes);
        if (result) {
            const auto reencoded = encode(*result.message);
            require(reencoded && decode(reencoded.bytes), "accepted mutation cannot be safely reencoded");
        }
    }
}

void cross_session_snapshot_replay() {
    // Session A emitted a high sequence number before the host restarted.
    auto stale = snapshot(1, 900);
    stale.entities[0].hit_points = 1;
    const auto previous_session = incarnation(0x51);
    const auto current_session = incarnation(0x52);
    const auto packet_from_previous_session = skcoop::core::encode(stale, previous_session);
    require(static_cast<bool>(packet_from_previous_session), "old-session fixture did not encode");

    // Session B restarts counters on the same host/map. This is legitimate new state.
    SnapshotStore current_world(1001);
    require(current_world.begin_area(100, 1), "new session could not initialize its area");
    require(current_world.apply(snapshot(1, 1), 1001) == SnapshotResult::applied, "new session seed failed");
    const auto replay = skcoop::core::decode(packet_from_previous_session.bytes, current_session);
    require(replay.error == WireError::wrong_session && !replay,
            "previous session packet crossed the envelope boundary");
    if (replay) {
        require(current_world.apply(std::get<Snapshot>(*replay.message), 1001) != SnapshotResult::applied,
                "old-session snapshot was accepted after counters restarted");
    }
    require(current_world.sequence() == 1 && current_world.entities()[0].hit_points == 100,
            "old-session replay changed the new world's state");
}

void session_envelopes_and_durable_resume() {
    const auto previous_session = incarnation(0x51);
    const auto current_session = incarnation(0x52);
    const std::array<WireMessage, 4> kinds{hello(1002), snapshot(), reward(), checkpoint(1)};
    for (const auto& message : kinds) {
        const auto packet = skcoop::core::encode(message, previous_session);
        const auto queued = skcoop::core::decode(packet.bytes, previous_session);
        require(packet && queued && queued.matches_session(previous_session), "active session rejected its own envelope");
        require(!queued.matches_session(current_session) && !queued.matches_session(SessionId{}),
                "decoded queued message lost its original session stamp");
        require(skcoop::core::decode(packet.bytes, current_session).error == WireError::wrong_session,
                "a message kind bypassed cross-session rejection");
        require(skcoop::core::encode(message, SessionId{}).error == WireError::invalid_session,
                "zero outbound session ID accepted");
        require(skcoop::core::decode(packet.bytes, SessionId{}).error == WireError::invalid_session,
                "unset expected session accepted");
    }
    auto bytes = skcoop::core::encode(snapshot(), previous_session).bytes;
    auto last_byte_changed = previous_session;
    last_byte_changed[15] ^= 1;
    require(skcoop::core::decode(bytes, last_byte_changed).error == WireError::wrong_session,
            "only part of the session ID was compared");
    std::fill_n(bytes.begin() + 12, 16, std::uint8_t{0});
    require(skcoop::core::decode(bytes, previous_session).error == WireError::invalid_session,
            "zero wire session ID accepted");
    bytes = skcoop::core::encode(snapshot(), previous_session).bytes;
    put16(bytes, 4, 1);
    require(skcoop::core::decode(bytes, previous_session).error == WireError::unsupported_protocol,
            "legacy protocol version accepted under the new layout");

    // Persistence belongs to the campaign, not to the transient network envelope.
    DurableLedger before(campaign(), 1001);
    require(before.apply(reward(), 1001) == EventResult::applied, "resume seed failed");
    DurableLedger resumed(campaign(), 1001);
    require(resumed.restore(checkpoint(1), before.events()), "new incarnation could not restore campaign receipts");
    const auto old_event = skcoop::core::encode(reward(), previous_session);
    require(skcoop::core::decode(old_event.bytes, current_session).error == WireError::wrong_session,
            "old durable packet accepted after resume");
    const auto fresh_envelope = skcoop::core::encode(reward(), current_session);
    const auto retried = skcoop::core::decode(fresh_envelope.bytes, current_session);
    require(retried && resumed.apply(std::get<DurableEvent>(*retried.message), 1001) == EventResult::duplicate,
            "new envelope lost campaign-level idempotence");
    require(resumed.reward_balance(1002) == 10 && resumed.revision() == 1, "resume duplicated reward");
}

void four_peer_offline_scenario() {
    SessionRoster session(1001, build(), checkpoint());
    std::vector<SnapshotStore> worlds;
    std::vector<DurableLedger> ledgers;
    auto state = snapshot();
    for (SteamId id = 1001; id <= 1004; ++id) {
        if (id != 1001) {
            const auto decoded = decode(encode(hello(id)).bytes);
            require(decoded && session.admit(std::get<Handshake>(*decoded.message)).accepted(), "offline handshake failed");
        }
        worlds.emplace_back(1001);
        require(worlds.back().begin_area(100, 1), "offline area setup failed");
        ledgers.emplace_back(campaign(), 1001);
        state.entities.push_back({{100, 1, id}, EntityKind::player, id, {}, 0, 100, 0, 10});
    }
    require(session.connected_count() == 4, "scenario did not contain four declared peers");
    const auto packet = encode(state);
    for (auto& world : worlds) {
        const auto received = decode(packet.bytes);
        require(received && world.apply(std::get<Snapshot>(*received.message), 1001) == SnapshotResult::applied,
                "four-peer snapshot delivery failed");
        require(world.entities().size() == 5, "scenario lost enemy or player");
    }
    for (std::uint64_t revision = 1; revision <= 3; ++revision) {
        auto event = reward(revision, 7); event.subject = 1001 + revision;
        const auto event_packet = encode(event);
        for (auto& ledger : ledgers) {
            const auto received = decode(event_packet.bytes);
            const auto& decoded_event = std::get<DurableEvent>(*received.message);
            require(ledger.apply(decoded_event, 1001) == EventResult::applied, "durable delivery failed");
            require(ledger.apply(decoded_event, 1001) == EventResult::duplicate, "duplicate delivery changed state");
        }
    }
    for (const auto& ledger : ledgers)
        for (SteamId id = 1002; id <= 1004; ++id)
            require(ledger.revision() == 3 && ledger.reward_balance(id) == 7, "offline peers diverged");
    // This invokes no game process, Steam session, networking or engine hooks.
}
} // namespace

int main() {
    const std::array<std::pair<std::string_view, std::function<void()>>, 14> tests{{
        {"handshake_identity", handshake_identity},
        {"roster_capacity_and_rejoin", roster_capacity_and_rejoin},
        {"checkpoint_reconnection", checkpoint_reconnection},
        {"ownership_rules", ownership_rules},
        {"snapshots_reject_stale_and_invalid", snapshots_reject_stale_and_invalid},
        {"durable_idempotence_and_authority", durable_idempotence_and_authority},
        {"durable_restore_is_atomic", durable_restore_is_atomic},
        {"durable_capacity_preserves_receipts", durable_capacity_preserves_receipts},
        {"wire_golden_little_endian", wire_golden_little_endian},
        {"wire_rejects_malformed", wire_rejects_malformed},
        {"wire_mutation_boundaries", wire_mutation_boundaries},
        {"cross_session_snapshot_replay", cross_session_snapshot_replay},
        {"session_envelopes_and_durable_resume", session_envelopes_and_durable_resume},
        {"four_peer_offline_scenario", four_peer_offline_scenario}
    }};
    std::size_t failures{};
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << "CORE_RESULT " << (failures == 0 ? "PASS" : "FAIL")
              << " cases=" << tests.size() << " failed=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
