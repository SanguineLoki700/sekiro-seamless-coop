#pragma once
#include "skcoop/actors/native_input_latch.hpp"
#include <mutex>

namespace skcoop::actors {
enum class NativeMailboxFault {
    none,invalid_clock,clock_regressed,epoch_exhausted,overlapping_update,
    // Historical values 7/8 do not identify which movement stage failed.
    latch_failure,invalid_source,invalid_movement,movement_mismatch,
    invalid_held,held_mismatch,abandoned_update,
    invalid_turn_intent=12,turn_intent_mismatch=13,
    invalid_resolver_movement=14,resolver_movement_mismatch=15
};
struct NativeMailboxFailure {
    NativeMailboxFault kind{};
    NativeInputFault latch_fault{};
    std::uint64_t epoch{};
    PlayerInput expected,observed;
    CommandButton button{};
    float held_duration{};
};
struct NativeInputFrame {
    std::uint64_t epoch{};
    PlayerInput input;
    std::optional<NativeInputSource> source;
    bool accepted{}; // A valid original-update token, including neutral quarantine.
};
struct NativeTurnIntent {
    MovementIntent movement;
    bool native_turn_observed{};
};
struct NativeConsumedInput {
    NativeInputSource source;
    std::uint64_t epoch{};
    PlayerInput input;
    std::uint64_t observed_ms{};
    bool held_state_observed{};
};
enum class NativeMovementOnset {
    strict,
    // Exact native binding only: its previous-stick filter may suppress the
    // first neutral-to-moving update. That zero result earns no consumption;
    // the next native update must match normally. Not a time-based grace.
    defer_first_zero,
    // Exact native binding only: with a matching native steering observation
    // in the same completed epoch, a valid divergent physical direction may
    // remain uncredited for the first three moving updates and at most 250ms.
    // Correct physical movement is still required for consumption. Association
    // changes never rearm this window; only a real neutral begin_update does.
    defer_verified_steering,
    // Live input may change while native turning/filtering is still active.
    // Observe valid divergence without credit; never substitute requested input
    // for the consumer's actual vector. The strict trial never uses this mode.
    observe_continuous
};
// Serializes receiver ingress and one actor's native update/observation threads.
// Engine calls must execute outside these methods. The supplied clock is a
// non-reentrant monotonic read, sampled only while the short mutex is held.
// Observers are trusted only after the platform adapter verifies native owner,
// call site and epoch. This portable component establishes no native binding.
class NativeCommandMailbox {
public:
    using Clock=std::uint64_t(*)()noexcept;
    static constexpr std::uint64_t observation_expiry_ms=100;
    // Cumulative native steering onset, aligned with the scenario's consumer
    // timeout. This never extends the 100ms freshness of an individual epoch.
    static constexpr std::uint64_t steering_onset_timeout_ms=250;
    NativeCommandMailbox(std::uint64_t input_generation,Clock,
                         NativeMovementOnset=NativeMovementOnset::strict);
    NativeInputOffer offer(const CommandDelivery&);
    NativeInputFrame begin_update();
    // Matching completion retires the original Pad slot. This is not by itself
    // consumption evidence. An invalidated or expired completion returns false.
    // A verified native turn, when supplied, is validated and published under
    // the same mutex before completion becomes visible to observer threads.
    bool finish_update(std::uint64_t epoch,std::optional<NativeTurnIntent> turn_intent={});
    void abandon_update(std::uint64_t epoch);
    std::uint64_t current_epoch();
    // Only a completed epoch accepts observations. All flags must describe the
    // same actual downstream resolver call, including at neutral input; they
    // are not requested stage values and partial calls cannot be combined.
    bool observe_resolver(std::uint64_t epoch,MovementIntent resolved,
                          bool gate,bool speedWrite,bool angleWrite);
    // Native steering is corroboration for a bounded onset deferral, never
    // physical consumption evidence. An unobserved turn supplies no proof;
    // an observed turn requires valid nonzero input matching this epoch.
    bool observe_turn_intent(std::uint64_t epoch,MovementIntent native_intent,bool native_turn_observed);
    bool observe_held(std::uint64_t epoch,CommandButton,float duration);
    std::optional<NativeConsumedInput> consumed();
    void suspend();
    // Strictly newer controller lifetime only. Refuses an in-flight original;
    // old completion must retire it before any reset can become effective.
    bool reset_generation(std::uint64_t new_generation);
    bool configure_movement_onset(NativeMovementOnset);
    bool failed()const;
    // First failure for this mailbox object, preserved across suspend/reset.
    // Values are captured before neutralization and are diagnostics only.
    NativeMailboxFailure failure()const;
private:
    mutable std::mutex mutex_;
    NativeInputLatch latch_;
    Clock clock_{};
    NativeMovementOnset movement_onset_{};
    std::uint64_t generation_{},epoch_counter_{},clock_at_{},issued_at_{},completed_at_{},flight_epoch_{};
    NativeInputFrame frame_;
    NativeMailboxFailure failure_;
    bool clock_seen_{},failed_{},in_flight_{},association_{},ready_{};
    bool previous_moving_{},onset_frame_{};
    bool steering_window_open_{},turn_intent_verified_{};
    std::uint64_t steering_onset_at_{},steering_updates_{};
    bool resolver_gate_{},resolver_speed_{},resolver_angle_{},attack_seen_{},guard_seen_{};
    MovementIntent resolved_movement_;
    ButtonMask resolved_held_{};
    std::uint64_t resolver_at_{},attack_at_{},guard_at_{};
    bool now_locked(std::uint64_t&);
    bool advance_epoch_locked();
    void invalidate_locked();
    void record_failure_locked(NativeMailboxFault,std::optional<PlayerInput> observed={},CommandButton button={},float held_duration=0);
    void fail_locked(NativeMailboxFault,std::optional<PlayerInput> observed={},CommandButton button={},float held_duration=0);
    bool matches_locked(std::uint64_t)const;
    bool fresh_locked(std::uint64_t)const;
    bool observe_turn_intent_locked(MovementIntent,bool);
};
}
