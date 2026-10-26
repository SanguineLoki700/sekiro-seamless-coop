#pragma once
#include "skcoop/lan/peer.hpp"
#include <algorithm>

namespace skcoop::actors {
// World-thread owner. Consume authenticated action batches independently of
// interpolated pose; inactive phases discard history and explicitly rebaseline.
class ActionReceiver {
public:
    void observe(const lan::ActionFrameBuffer& frames, bool following,
                 std::uint64_t now) noexcept {
        restored_ = false;
        if (fault() != lan::ActionFault::none) return;
        if (frames.fault() != lan::ActionFault::none) { fail(frames.fault()); return; }
        const auto& latest = frames.latest();
        if (!latest) { suspend(); return; }
        if (now < latest->arrival_ms) { fail(lan::ActionFault::clock_regressed); return; }
        following = following && now - latest->arrival_ms <= lan::action_frame_expiry_ms;
        if (!following || !active_ || !timeline_.armed()) {
            const auto before = timeline_.seen_serial();
            if (!timeline_.baseline(latest->scope, latest->history,
                                    latest->packet_sequence, latest->arrival_ms)) return;
            baseline_discarded_ += latest->history.latest - before;
            ack_ = std::max(ack_, latest->packet_sequence);
            active_ = restored_ = following;
            if (!following) timeline_.suspend();
            return;
        }
        for (const auto& frame : frames.pending()) {
            timeline_.push(frame.scope, frame.history, frame.packet_sequence, frame.arrival_ms);
            if (timeline_.fault() != lan::ActionFault::none) return;
        }
        timeline_.push(latest->scope, latest->history, latest->packet_sequence, latest->arrival_ms);
        if (timeline_.fault() == lan::ActionFault::none)
            ack_ = std::max(ack_, latest->packet_sequence);
    }
    std::optional<lan::ActionEvent> take(std::uint64_t now) noexcept {
        if (fault() != lan::ActionFault::none || !active_) return {};
        auto result = timeline_.take(now);
        if (!timeline_.armed()) active_ = false;
        return result;
    }
    void suspend() noexcept { timeline_.suspend(); active_ = restored_ = false; }
    bool restored() const noexcept { return restored_; }
    bool active() const noexcept { return active_ && timeline_.armed(); }
    bool held_sprint() const noexcept { return active() && timeline_.held_sprint(); }
    std::size_t pending() const noexcept { return timeline_.pending(); }
    std::uint64_t acknowledgement() const noexcept { return ack_; }
    std::uint64_t discarded() const noexcept { return baseline_discarded_ + timeline_.discarded(); }
    lan::ActionFault fault() const noexcept {
        return fault_ != lan::ActionFault::none ? fault_ : timeline_.fault();
    }
private:
    lan::ActionTimeline timeline_;
    std::uint64_t ack_{}, baseline_discarded_{};
    lan::ActionFault fault_{};
    bool active_{}, restored_{};
    void fail(lan::ActionFault fault) noexcept { fault_ = fault; suspend(); }
};
}
