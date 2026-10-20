#include "skcoop/core/core.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace skcoop::core {

bool valid_build_identity(const BuildIdentity& build) noexcept {
    const auto nonzero = [](const Sha256& digest) {
        return std::any_of(digest.begin(), digest.end(), [](auto value) { return value != 0; });
    };
    return nonzero(build.game_sha256) && nonzero(build.profile_sha256);
}

bool valid_campaign(const CampaignId& id) noexcept {
    return std::any_of(id.begin(), id.end(), [](auto v) { return v != 0; });
}

ReconnectResult validate_reconnect(const CampaignCheckpoint& host,
                                  const CampaignCheckpoint& peer) noexcept {
    if (!valid_campaign(host.campaign) || host.campaign != peer.campaign)
        return ReconnectResult::wrong_campaign;
    if (peer.revision > host.revision) return ReconnectResult::peer_ahead;
    if (peer.revision < host.revision) return ReconnectResult::needs_snapshot;
    return peer.state_sha256 == host.state_sha256 ? ReconnectResult::current : ReconnectResult::conflict;
}

SessionRoster::SessionRoster(SteamId host, BuildIdentity build, CampaignCheckpoint checkpoint)
    : host_(host), build_(build), checkpoint_(checkpoint) {
    if (host == 0 || !valid_campaign(checkpoint.campaign) || !valid_build_identity(build))
        throw std::invalid_argument("host, campaign and both build digests must be nonzero");
    members_[0] = {host, true, true};
}

JoinResult SessionRoster::admit(const Handshake& request) {
    const auto reject = [](JoinError error) { return JoinResult{JoinStatus::rejected, error, false}; };
    if (request.player == 0 || request.player == host_) return reject(JoinError::invalid_player);
    if (request.protocol != protocol_version) return reject(JoinError::protocol_mismatch);
    if (request.build.game_sha256 != build_.game_sha256) return reject(JoinError::game_mismatch);
    if (request.build.profile_sha256 != build_.profile_sha256) return reject(JoinError::profile_mismatch);
    if (request.checkpoint.campaign != checkpoint_.campaign) return reject(JoinError::wrong_campaign);
    if (!request.has_checkpoint &&
        (request.checkpoint.revision != 0 || request.checkpoint.state_sha256 != Sha256{}))
        return reject(JoinError::checkpoint_conflict);
    const auto resume = request.has_checkpoint ? validate_reconnect(checkpoint_, request.checkpoint)
                                               : ReconnectResult::needs_snapshot;
    if (resume == ReconnectResult::wrong_campaign) return reject(JoinError::wrong_campaign);
    if (resume == ReconnectResult::peer_ahead) return reject(JoinError::peer_ahead);
    if (resume == ReconnectResult::conflict) return reject(JoinError::checkpoint_conflict);
    const bool needs_snapshot = resume == ReconnectResult::needs_snapshot;
    auto member = std::find_if(members_.begin(), members_.end(), [&](const auto& m) {
        return m.player == request.player;
    });
    if (member != members_.end()) {
        const auto status = member->connected ? JoinStatus::already_connected : JoinStatus::reconnected;
        member->connected = true;
        member->synchronized = !needs_snapshot;
        return {status, JoinError::none, needs_snapshot};
    }
    member = std::find_if(members_.begin(), members_.end(), [](const auto& m) { return m.player == 0; });
    if (member == members_.end()) return reject(JoinError::full);
    *member = {request.player, true, !needs_snapshot};
    return {JoinStatus::accepted, JoinError::none, needs_snapshot};
}

bool SessionRoster::disconnect(SteamId id) noexcept {
    if (id == 0 || id == host_) return false;
    for (auto& member : members_) {
        if (member.player == id && member.connected) {
            member.connected = false;
            member.synchronized = false;
            return true;
        }
    }
    return false;
}

bool SessionRoster::release_disconnected(SteamId id) noexcept {
    if (id == 0 || id == host_) return false;
    for (auto& member : members_) {
        if (member.player == id && !member.connected) { member = {}; return true; }
    }
    return false;
}

bool SessionRoster::confirm_checkpoint(SteamId id, const CampaignCheckpoint& checkpoint) noexcept {
    if (checkpoint != checkpoint_) return false;
    for (auto& member : members_) {
        if (member.player == id && member.connected) { member.synchronized = true; return true; }
    }
    return false;
}

bool SessionRoster::advance_checkpoint(const CampaignCheckpoint& checkpoint) noexcept {
    if (checkpoint.campaign != checkpoint_.campaign || checkpoint.revision < checkpoint_.revision)
        return false;
    if (checkpoint.revision == checkpoint_.revision) return checkpoint == checkpoint_;
    checkpoint_ = checkpoint;
    for (auto& member : members_) member.synchronized = member.player == host_;
    return true;
}

std::size_t SessionRoster::connected_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(members_.begin(), members_.end(), [](const auto& m) {
        return m.player != 0 && m.connected;
    }));
}

bool can_simulate(EntityKind kind, SteamId player_owner, SteamId sender, SteamId host) noexcept {
    if (sender == 0 || host == 0) return false;
    switch (kind) {
    case EntityKind::enemy: return player_owner == host && sender == host;
    case EntityKind::player: return player_owner != 0 && sender == player_owner;
    default: return false;
    }
}

bool valid_snapshot(const Snapshot& snapshot) noexcept {
    if (snapshot.generation == 0 || snapshot.sequence == 0 || snapshot.entities.size() > max_snapshot_entities)
        return false;
    for (std::size_t i = 0; i < snapshot.entities.size(); ++i) {
        const auto& entity = snapshot.entities[i];
        if (entity.id.map != snapshot.map || entity.id.generation != snapshot.generation ||
            entity.id.object == 0 || entity.owner == 0 ||
            (entity.kind != EntityKind::enemy && entity.kind != EntityKind::player) ||
            !std::isfinite(entity.yaw)) return false;
        for (auto value : entity.position) if (!std::isfinite(value)) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (snapshot.entities[j].id == entity.id) return false;
    }
    return true;
}

SnapshotStore::SnapshotStore(SteamId host) : host_(host) {
    if (host == 0) throw std::invalid_argument("host must be nonzero");
}

bool SnapshotStore::begin_area(std::uint32_t map, std::uint32_t generation) noexcept {
    if (generation == 0 || generation <= generation_) return false;
    map_ = map;
    generation_ = generation;
    sequence_ = 0;
    entities_.clear();
    return true;
}

SnapshotResult SnapshotStore::apply(const Snapshot& snapshot, SteamId sender) {
    if (sender != host_) return SnapshotResult::not_host;
    if (!valid_snapshot(snapshot)) return SnapshotResult::invalid;
    if (snapshot.map != map_ || snapshot.generation != generation_) return SnapshotResult::wrong_area;
    if (snapshot.sequence <= sequence_) return SnapshotResult::stale_sequence;
    for (const auto& entity : snapshot.entities)
        if (entity.kind == EntityKind::enemy && entity.owner != host_) return SnapshotResult::invalid;
    // Validate the complete batch before replacing live state.
    entities_ = snapshot.entities;
    sequence_ = snapshot.sequence;
    return SnapshotResult::applied;
}

bool valid_event(const DurableEvent& event) noexcept {
    if (!valid_campaign(event.campaign) || event.revision == 0 ||
        event.id.issuer == 0 || event.id.sequence == 0) return false;
    switch (event.kind) {
    case EventKind::set_flag: return event.value <= 1;
    case EventKind::grant_reward: return event.subject != 0 && event.value != 0;
    default: return false;
    }
}

DurableLedger::DurableLedger(CampaignId campaign, SteamId host) : campaign_(campaign), host_(host) {
    if (!valid_campaign(campaign) || host == 0)
        throw std::invalid_argument("host and campaign must be nonzero");
}

EventResult DurableLedger::apply(const DurableEvent& event, SteamId sender) {
    if (sender != host_ || event.id.issuer != host_) return EventResult::not_host;
    if (event.campaign != campaign_) return EventResult::wrong_campaign;
    if (!valid_event(event)) return EventResult::invalid;
    const auto receipt = std::find_if(events_.begin(), events_.end(), [&](const auto& prior) {
        return prior.id == event.id;
    });
    if (receipt != events_.end()) return *receipt == event ? EventResult::duplicate : EventResult::conflicting_id;
    if (revision_ == std::numeric_limits<std::uint64_t>::max() || event.revision != revision_ + 1)
        return EventResult::out_of_order;
    if (events_.size() == max_durable_events) return EventResult::journal_full;
    const auto balance = reward_balance(event.subject);
    if (event.kind == EventKind::grant_reward &&
        balance > std::numeric_limits<std::uint64_t>::max() - event.value)
        return EventResult::reward_overflow;

    events_.push_back(event);
    revision_ = event.revision;
    return EventResult::applied;
}

bool DurableLedger::restore(const CampaignCheckpoint& checkpoint, std::span<const DurableEvent> journal) {
    if (checkpoint.campaign != campaign_ || checkpoint.revision < revision_ ||
        journal.size() > max_durable_events) return false;
    if (journal.size() < events_.size() ||
        !std::equal(events_.begin(), events_.end(), journal.begin())) return false;
    DurableLedger candidate(campaign_, host_);
    for (const auto& event : journal)
        if (candidate.apply(event, host_) != EventResult::applied) return false;
    if (candidate.revision_ != checkpoint.revision) return false;
    // state_sha256 is verified over canonical save bytes by the caller before restore.
    *this = std::move(candidate);
    return true;
}

std::uint64_t DurableLedger::reward_balance(SteamId player) const noexcept {
    std::uint64_t balance{};
    for (const auto& event : events_)
        if (event.kind == EventKind::grant_reward && event.subject == player) balance += event.value;
    return balance;
}

bool DurableLedger::flag(std::uint64_t id) const noexcept {
    for (auto event = events_.rbegin(); event != events_.rend(); ++event)
        if (event->kind == EventKind::set_flag && event->subject == id) return event->value != 0;
    return false;
}

} // namespace skcoop::core
