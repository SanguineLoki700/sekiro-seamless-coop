#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace skcoop::lan {
// Logical event labels observed on the supported source character. These
// ordinals are ours; they are not native graph event IDs or native addresses.
enum class ActionKind : std::uint8_t {
    none = 0,
    stand_move_start,
    stand_run_stop,
    stand_walk_stop,
    ground_step,
    sprint_start_from_step,
    sprint_stop_ready,
    sprint_stop_quick_turn_front,
    ground_jump_ready,
    ground_jump_land_ready,
    land_ground_jump,
};

inline constexpr std::size_t action_history_capacity = 4;
inline constexpr std::size_t action_queue_capacity = 16;
inline constexpr std::uint64_t action_delay_ms = 100;
inline constexpr std::uint64_t action_expiry_ms = 250;
inline constexpr std::uint64_t action_frame_expiry_ms = 500;

struct ActionEvent {
    std::uint64_t serial{};
    std::uint32_t source_ms{}; // Monotonic milliseconds relative to this run.
    ActionKind kind{};
    float direction{}; // Captured at the event, degrees; never interpolated.
    bool operator==(const ActionEvent&) const = default;
};

struct ActionSnapshot {
    std::uint64_t epoch{};
    std::uint64_t latest{};
    std::uint32_t source_ms{};
    std::uint8_t count{};
    bool sprinting{}; // Held state; survives baseline without replaying a start event.
    std::array<ActionEvent, action_history_capacity> events{};
    bool operator==(const ActionSnapshot&) const = default;
};

// Authentication remains the transport's responsibility. The caller supplies
// the active, authenticated scope, not scope values asserted by an event.
struct ActionScope {
    std::array<std::uint8_t, 16> room{}, connection{};
    std::uint64_t participant{}, epoch{};
    bool operator==(const ActionScope&) const = default;
};

enum class ActionFault {
    none,
    invalid,
    scope_changed,
    clock_regressed,
    gap,
    overflow,
    serial_exhausted,
};

bool valid(ActionKind) noexcept;
bool valid(const ActionSnapshot&) noexcept;
bool valid(const ActionScope&) noexcept;

// Single source-thread producer. Call capture for every observed event, before
// throttling offers. The four records persist across snapshots. A record may
// be evicted only after a successful offer of a snapshot containing it; if five
// events arrive without such an offer, capture fails without replacing data.
// Offered is a local hand-off, not a network acknowledgement. Missing more than
// the retained history is detected as a gap by ActionTimeline.
class ActionCapture {
public:
    explicit ActionCapture(std::uint64_t epoch, std::uint32_t source_ms = 0) noexcept;
    bool capture(ActionKind, std::uint32_t source_ms, float direction = 0) noexcept;
    std::optional<ActionSnapshot> snapshot(std::uint32_t source_ms) noexcept;
    bool offered(std::uint64_t latest) noexcept;
    ActionFault fault() const noexcept { return fault_; }
    std::uint64_t offered_serial() const noexcept { return offered_; }

private:
    ActionSnapshot history_{};
    std::uint64_t offered_{};
    ActionFault fault_{};
    bool fail(ActionFault) noexcept;
};

enum class ActionPush { accepted, stale, unarmed, failed };

// Separate from pose interpolation. baseline explicitly discards history and
// pending work and arms from a fresh snapshot. Call suspend on pause/stale/loss,
// then baseline a fresh current snapshot on resume: old actions are not replayed.
// Faults latch; use a new object for a new authenticated session/lifetime.
// Arrival-based expiry bounds our queues, not unmeasured network transit time.
class ActionTimeline {
public:
    bool baseline(const ActionScope&, const ActionSnapshot&, std::uint64_t packet_sequence,
                  std::uint64_t arrival_ms) noexcept;
    ActionPush push(const ActionScope&, const ActionSnapshot&, std::uint64_t packet_sequence,
                    std::uint64_t arrival_ms) noexcept;
    void suspend() noexcept;
    std::optional<ActionEvent> take(std::uint64_t now_ms) noexcept;

    bool armed() const noexcept { return armed_; }
    // Latest received held state, gated by explicit suspend/fault/freshness.
    // This is metadata for native state restoration, not an action request.
    bool held_sprint() const noexcept { return armed_ && fault_ == ActionFault::none && known_.sprinting; }
    ActionFault fault() const noexcept { return fault_; }
    std::size_t pending() const noexcept { return count_; }
    std::uint64_t seen_serial() const noexcept { return known_.latest; }
    std::uint64_t delivered() const noexcept { return delivered_; }
    std::uint64_t discarded() const noexcept { return discarded_; }

private:
    struct Pending { ActionEvent event; std::uint64_t arrival{}, due{}; };
    ActionScope scope_{};
    ActionSnapshot known_{};
    std::array<Pending, action_queue_capacity> queue_{};
    std::size_t count_{};
    std::uint64_t sequence_{}, arrival_{}, checked_at_{}, delivered_{}, discarded_{};
    bool bound_{}, armed_{};
    ActionFault fault_{};
    bool fail(ActionFault) noexcept;
    void pop() noexcept;
};
}
