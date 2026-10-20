#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace skcoop::core {

inline constexpr std::uint16_t protocol_version = 1;
inline constexpr std::size_t max_players = 4;
inline constexpr std::size_t max_snapshot_entities = 128;
inline constexpr std::size_t max_durable_events = 4096;
using SteamId = std::uint64_t; // A declared stable identifier, never proof of authentication.
using Sha256 = std::array<std::uint8_t, 32>;
using CampaignId = std::array<std::uint8_t, 16>;

struct BuildIdentity {
    Sha256 game_sha256{};
    Sha256 profile_sha256{};
    bool operator==(const BuildIdentity&) const = default;
};

struct CampaignCheckpoint {
    CampaignId campaign{};
    std::uint64_t revision{};
    Sha256 state_sha256{}; // Computed by the save adapter; this core compares bytes only.
    bool operator==(const CampaignCheckpoint&) const = default;
};

struct Handshake {
    std::uint16_t protocol{protocol_version};
    SteamId player{};
    BuildIdentity build{};
    CampaignCheckpoint checkpoint{};
    // A fresh guest advertises the host campaign ID, zero revision/digest, and
    // requests a full snapshot even if the host is also at revision zero.
    bool has_checkpoint{true};
    bool operator==(const Handshake&) const = default;
};

enum class ReconnectResult { current, needs_snapshot, wrong_campaign, peer_ahead, conflict };
[[nodiscard]] bool valid_build_identity(const BuildIdentity&) noexcept;
[[nodiscard]] bool valid_campaign(const CampaignId&) noexcept;
[[nodiscard]] ReconnectResult validate_reconnect(const CampaignCheckpoint& host,
                                                const CampaignCheckpoint& peer) noexcept;

enum class JoinStatus { accepted, reconnected, already_connected, rejected };
enum class JoinError {
    none, invalid_player, protocol_mismatch, game_mismatch, profile_mismatch,
    wrong_campaign, peer_ahead, checkpoint_conflict, full
};
struct JoinResult {
    JoinStatus status{JoinStatus::rejected};
    JoinError error{JoinError::none};
    bool needs_snapshot{};
    [[nodiscard]] bool accepted() const noexcept { return status != JoinStatus::rejected; }
};
struct Member {
    SteamId player{};
    bool connected{};
    bool synchronized{};
};

class SessionRoster {
public:
    SessionRoster(SteamId host, BuildIdentity build, CampaignCheckpoint checkpoint);
    [[nodiscard]] JoinResult admit(const Handshake&);
    [[nodiscard]] bool disconnect(SteamId) noexcept;
    // A disconnected guest reserves its slot until the host explicitly releases it.
    [[nodiscard]] bool release_disconnected(SteamId) noexcept;
    [[nodiscard]] bool confirm_checkpoint(SteamId, const CampaignCheckpoint&) noexcept;
    // Advancing a checkpoint requires peers to acknowledge the new shared state.
    [[nodiscard]] bool advance_checkpoint(const CampaignCheckpoint&) noexcept;
    [[nodiscard]] const std::array<Member, max_players>& members() const noexcept { return members_; }
    [[nodiscard]] const CampaignCheckpoint& checkpoint() const noexcept { return checkpoint_; }
    [[nodiscard]] std::size_t connected_count() const noexcept;
private:
    SteamId host_;
    BuildIdentity build_;
    CampaignCheckpoint checkpoint_;
    std::array<Member, max_players> members_{};
};

struct EntityId {
    std::uint32_t map{};
    std::uint32_t generation{}; // Session-wide increasing loading epoch, never a pointer.
    std::uint64_t object{};
    bool operator==(const EntityId&) const = default;
};
enum class EntityKind : std::uint8_t { enemy = 1, player = 2 };
[[nodiscard]] bool can_simulate(EntityKind kind, SteamId player_owner,
                              SteamId sender, SteamId host) noexcept;

struct EntityState {
    EntityId id{};
    EntityKind kind{EntityKind::enemy};
    SteamId owner{};
    std::array<float, 3> position{};
    float yaw{};
    std::uint32_t hit_points{};
    std::uint32_t posture{};
    std::uint32_t animation{};
    bool operator==(const EntityState&) const = default;
};
struct Snapshot {
    std::uint32_t map{};
    std::uint32_t generation{};
    std::uint64_t sequence{};
    std::vector<EntityState> entities;
    bool operator==(const Snapshot&) const = default;
};
[[nodiscard]] bool valid_snapshot(const Snapshot&) noexcept;
enum class SnapshotResult { applied, not_host, wrong_area, stale_sequence, invalid };

class SnapshotStore {
public:
    explicit SnapshotStore(SteamId host);
    [[nodiscard]] bool begin_area(std::uint32_t map, std::uint32_t generation) noexcept;
    [[nodiscard]] SnapshotResult apply(const Snapshot&, SteamId sender);
    [[nodiscard]] std::span<const EntityState> entities() const noexcept { return entities_; }
    [[nodiscard]] std::uint64_t sequence() const noexcept { return sequence_; }
private:
    SteamId host_;
    std::uint32_t map_{};
    std::uint32_t generation_{};
    std::uint64_t sequence_{};
    std::vector<EntityState> entities_;
};

struct EventId {
    SteamId issuer{};
    std::uint64_t sequence{};
    bool operator==(const EventId&) const = default;
};
enum class EventKind : std::uint8_t { set_flag = 1, grant_reward = 2 };
struct DurableEvent {
    CampaignId campaign{};
    std::uint64_t revision{};
    EventId id{};
    EventKind kind{EventKind::set_flag};
    // Flag identifier for set_flag; receiving SteamId for grant_reward.
    std::uint64_t subject{};
    std::uint32_t value{};
    bool operator==(const DurableEvent&) const = default;
};
[[nodiscard]] bool valid_event(const DurableEvent&) noexcept;
enum class EventResult {
    applied, duplicate, not_host, wrong_campaign, invalid, conflicting_id,
    out_of_order, journal_full, reward_overflow
};

class DurableLedger {
public:
    DurableLedger(CampaignId campaign, SteamId host);
    [[nodiscard]] EventResult apply(const DurableEvent&, SteamId sender);
    // Atomic restore: replay a complete journal from revision 1; leave this ledger
    // untouched if any receipt is missing, duplicated, conflicting or out of order.
    // An existing confirmed prefix is immutable, including at the same revision.
    [[nodiscard]] bool restore(const CampaignCheckpoint&, std::span<const DurableEvent>);
    [[nodiscard]] std::uint64_t reward_balance(SteamId) const noexcept;
    [[nodiscard]] bool flag(std::uint64_t) const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::span<const DurableEvent> events() const noexcept { return events_; }
private:
    CampaignId campaign_;
    SteamId host_;
    std::uint64_t revision_{};
    std::vector<DurableEvent> events_;
};

} // namespace skcoop::core
