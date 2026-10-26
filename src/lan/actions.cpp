#include "skcoop/lan/actions.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace skcoop::lan {
namespace {
bool direction_valid(float value) noexcept {
    return std::isfinite(value) && std::abs(value) <= 360.f;
}
bool event_valid(const ActionEvent& event) noexcept {
    return event.serial && valid(event.kind) && direction_valid(event.direction);
}
bool overlaps_match(const ActionSnapshot& old, const ActionSnapshot& next) noexcept {
    for (std::size_t i = 0; i < old.count; ++i)
        for (std::size_t j = 0; j < next.count; ++j)
            if (old.events[i].serial == next.events[j].serial && old.events[i] != next.events[j])
                return false;
    return true;
}
}

bool valid(ActionKind kind) noexcept {
    return kind >= ActionKind::stand_move_start && kind <= ActionKind::land_ground_jump;
}
bool valid(const ActionScope& scope) noexcept {
    const auto nonzero = [](const auto& bytes) {
        return std::any_of(bytes.begin(), bytes.end(), [](auto value) { return value != 0; });
    };
    return nonzero(scope.room) && nonzero(scope.connection) && scope.participant && scope.epoch;
}
bool valid(const ActionSnapshot& snapshot) noexcept {
    if (!snapshot.epoch || snapshot.count != std::min<std::uint64_t>(snapshot.latest, action_history_capacity))
        return false;
    std::uint32_t previous_ms{};
    for (std::size_t i = 0; i < snapshot.events.size(); ++i) {
        const auto& event = snapshot.events[i];
        if (i >= snapshot.count) {
            if (event != ActionEvent{}) return false;
            continue;
        }
        if (!event_valid(event) || event.serial != snapshot.latest - snapshot.count + 1 + i ||
            event.source_ms > snapshot.source_ms || (i && event.source_ms < previous_ms))
            return false;
        previous_ms = event.source_ms;
    }
    // Logical held-state policy: sprint-start sets it, the other allowed
    // movement/step/stop/jump/landing transitions clear it. The retained last
    // event determines the declared state after older history rolls out; a
    // no-event initial snapshot cannot claim sprint.
    return snapshot.sprinting == (snapshot.count &&
        snapshot.events[snapshot.count - 1].kind == ActionKind::sprint_start_from_step);
}

ActionCapture::ActionCapture(std::uint64_t epoch, std::uint32_t source_ms) noexcept {
    history_.epoch = epoch;
    history_.source_ms = source_ms;
    if (!epoch) fault_ = ActionFault::invalid;
}
bool ActionCapture::fail(ActionFault fault) noexcept { fault_ = fault; return false; }
bool ActionCapture::capture(ActionKind kind, std::uint32_t source_ms, float direction) noexcept {
    if (fault_ != ActionFault::none) return false;
    if (!valid(kind) || !direction_valid(direction)) return fail(ActionFault::invalid);
    if (source_ms < history_.source_ms) return fail(ActionFault::clock_regressed);
    if (history_.latest == std::numeric_limits<std::uint64_t>::max()) return fail(ActionFault::serial_exhausted);
    if (history_.count == action_history_capacity) {
        if (history_.events[0].serial > offered_) return fail(ActionFault::overflow);
        std::move(history_.events.begin() + 1, history_.events.end(), history_.events.begin());
        --history_.count;
    }
    history_.source_ms = source_ms;
    history_.events[history_.count++] = {++history_.latest, source_ms, kind, direction};
    history_.sprinting = kind == ActionKind::sprint_start_from_step;
    return true;
}
std::optional<ActionSnapshot> ActionCapture::snapshot(std::uint32_t source_ms) noexcept {
    if (fault_ != ActionFault::none) return {};
    if (source_ms < history_.source_ms) { fail(ActionFault::clock_regressed); return {}; }
    history_.source_ms = source_ms;
    return history_;
}
bool ActionCapture::offered(std::uint64_t latest) noexcept {
    if (fault_ != ActionFault::none) return false;
    if (latest < offered_ || latest > history_.latest) return fail(ActionFault::invalid);
    offered_ = latest;
    return true;
}

bool ActionTimeline::fail(ActionFault fault) noexcept {
    fault_ = fault;
    suspend();
    return false;
}
void ActionTimeline::suspend() noexcept {
    discarded_ += count_;
    count_ = 0;
    armed_ = false;
}
bool ActionTimeline::baseline(const ActionScope& scope, const ActionSnapshot& snapshot,
                              std::uint64_t sequence, std::uint64_t arrival) noexcept {
    if (fault_ != ActionFault::none) return false;
    if (!valid(scope) || !valid(snapshot) || snapshot.epoch != scope.epoch || !sequence ||
        arrival > std::numeric_limits<std::uint64_t>::max() - action_delay_ms)
        return fail(ActionFault::invalid);
    if (bound_) {
        if (scope != scope_) return fail(ActionFault::scope_changed);
        if (sequence < sequence_ || arrival < arrival_ || snapshot.source_ms < known_.source_ms)
            return fail(ActionFault::clock_regressed);
        if (snapshot.latest < known_.latest || !overlaps_match(known_, snapshot))
            return fail(ActionFault::invalid);
        if (sequence == sequence_ && snapshot != known_) return fail(ActionFault::invalid);
    }
    suspend();
    scope_ = scope;
    known_ = snapshot;
    sequence_ = sequence;
    arrival_ = arrival;
    checked_at_ = std::max(checked_at_, arrival);
    bound_ = armed_ = true;
    return true;
}
ActionPush ActionTimeline::push(const ActionScope& scope, const ActionSnapshot& snapshot,
                                std::uint64_t sequence, std::uint64_t arrival) noexcept {
    if (fault_ != ActionFault::none) return ActionPush::failed;
    if (!armed_) return ActionPush::unarmed;
    const auto reject = [&](ActionFault fault) {
        fail(fault);
        return ActionPush::failed;
    };
    if (scope != scope_) return reject(ActionFault::scope_changed);
    if (!sequence || !valid(snapshot) || snapshot.epoch != scope.epoch)
        return reject(ActionFault::invalid);
    if (sequence <= sequence_) return ActionPush::stale;
    if (arrival < arrival_ || snapshot.source_ms < known_.source_ms ||
        arrival > std::numeric_limits<std::uint64_t>::max() - action_delay_ms)
        return reject(ActionFault::clock_regressed);
    if (snapshot.latest < known_.latest || !overlaps_match(known_, snapshot))
        return reject(ActionFault::invalid);
    if (snapshot.latest > known_.latest &&
        snapshot.events[0].serial > known_.latest + 1)
        return reject(ActionFault::gap);

    auto next = queue_;
    auto next_count = count_;
    std::uint64_t expired{};
    for (std::size_t i = 0; i < snapshot.count; ++i) {
        const auto& event = snapshot.events[i];
        if (event.serial <= known_.latest) continue;
        const auto source_age = snapshot.source_ms - event.source_ms;
        if (source_age > action_expiry_ms) { ++expired; continue; }
        if (next_count == next.size()) return reject(ActionFault::overflow);
        // Preserve source spacing within a coalesced offer; across offers,
        // clamp to serial order rather than interpolate an event identity.
        auto due = arrival + action_delay_ms - std::min<std::uint64_t>(source_age, action_delay_ms);
        if (next_count) due = std::max(due, next[next_count - 1].due);
        next[next_count++] = {event, arrival, due};
    }
    queue_ = next;
    count_ = next_count;
    discarded_ += expired;
    known_ = snapshot;
    sequence_ = sequence;
    arrival_ = arrival;
    return ActionPush::accepted;
}
void ActionTimeline::pop() noexcept {
    std::move(queue_.begin() + 1, queue_.begin() + static_cast<std::ptrdiff_t>(count_), queue_.begin());
    --count_;
    queue_[count_] = {};
}
std::optional<ActionEvent> ActionTimeline::take(std::uint64_t now) noexcept {
    if (!armed_ || fault_ != ActionFault::none) return {};
    if (now < checked_at_ || now < arrival_) { fail(ActionFault::clock_regressed); return {}; }
    checked_at_ = now;
    if (now - arrival_ > action_frame_expiry_ms) { suspend(); return {}; }
    while (count_) {
        const auto pending = queue_[0];
        if (now - pending.arrival > action_expiry_ms) { pop(); ++discarded_; continue; }
        if (now < pending.due) return {};
        pop();
        ++delivered_;
        return pending.event;
    }
    return {};
}
}
