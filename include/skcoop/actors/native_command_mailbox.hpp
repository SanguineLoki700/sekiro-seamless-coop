#pragma once
#include "skcoop/actors/native_input_latch.hpp"
#include <mutex>

namespace skcoop::actors {
struct NativeInputFrame {
    std::uint64_t epoch{};
    PlayerInput input;
    std::optional<NativeInputSource> source;
    bool accepted{}; // A valid original-update token, including neutral quarantine.
};
struct NativeConsumedInput {
    NativeInputSource source;
    std::uint64_t epoch{};
    PlayerInput input;
    std::uint64_t observed_ms{};
    bool held_state_observed{};
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
    NativeCommandMailbox(std::uint64_t input_generation,Clock);
    NativeInputOffer offer(const CommandDelivery&);
    NativeInputFrame begin_update();
    // Matching completion retires the original Pad slot. This is not by itself
    // consumption evidence. An invalidated or expired completion returns false.
    bool finish_update(std::uint64_t epoch);
    void abandon_update(std::uint64_t epoch);
    std::uint64_t current_epoch();
    // Only a completed epoch accepts observations. All flags must describe the
    // same actual downstream resolver call, including at neutral input; they
    // are not requested stage values and partial calls cannot be combined.
    bool observe_resolver(std::uint64_t epoch,MovementIntent resolved,
                          bool gate,bool speedWrite,bool angleWrite);
    bool observe_held(std::uint64_t epoch,CommandButton,float duration);
    std::optional<NativeConsumedInput> consumed();
    void suspend();
    // Strictly newer controller lifetime only. Refuses an in-flight original;
    // old completion must retire it before any reset can become effective.
    bool reset_generation(std::uint64_t new_generation);
    bool failed()const;
private:
    mutable std::mutex mutex_;
    NativeInputLatch latch_;
    Clock clock_{};
    std::uint64_t generation_{},epoch_counter_{},clock_at_{},issued_at_{},completed_at_{},flight_epoch_{};
    NativeInputFrame frame_;
    bool clock_seen_{},failed_{},in_flight_{},association_{},ready_{};
    bool resolver_gate_{},resolver_speed_{},resolver_angle_{},attack_seen_{},guard_seen_{};
    MovementIntent resolved_movement_;
    ButtonMask resolved_held_{};
    std::uint64_t resolver_at_{},attack_at_{},guard_at_{};
    bool now_locked(std::uint64_t&);
    bool advance_epoch_locked();
    void invalidate_locked();
    void fail_locked();
    bool matches_locked(std::uint64_t)const;
    bool fresh_locked(std::uint64_t)const;
};
}
