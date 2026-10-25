#include "actor_demo.h"
#include <algorithm>
#include <cmath>

namespace skcoop::actors {
bool valid_position(const Vector& v) noexcept {
    for (const auto x : v) if (!std::isfinite(x)) return false;
    return std::abs(v[0]) < 1e7f && std::abs(v[1]) < 1e7f &&
        std::abs(v[2]) < 1e7f && std::abs(v[3]) <= 2;
}
namespace {
float distance(const Vector& a, const Vector& b) {
    return std::hypot(a[0]-b[0], a[1]-b[1], a[2]-b[2]);
}
}
void Demo::tick(Game& game) {
    if (phase == Phase::stopped || phase == Phase::failed) return;
    if (stop_requested && !creation_calls) { phase=Phase::stopped; return; }
    ++ticks;
    Frame f{};
    if (!game.sample(f) || !f.primary_ready) {
        if (creation_calls) { phase = Phase::stopped; error = Error::changed_world; }
        return;
    }
    if (!valid_position(f.position)) { fail(Error::invalid_pose); return; }
    if (!creation_calls) {
        if (f.guest || !f.world || !f.primary || !f.registry) { fail(Error::binding); return; }
        const float length = std::hypot(f.forward[0], f.forward[1], f.forward[2]);
        if (!valid_position(f.forward) || std::abs(f.forward[3]) > .001f || length < .5f || length > 1.5f) {
            fail(Error::invalid_pose); return;
        }
        original_ = f;
        placement_ = f.position;
        for (unsigned i=0; i<3; ++i) {
            offset_[i] = f.forward[i] / length * 2.2f;
            placement_[i] += offset_[i];
        }
        ++creation_calls; // A failed/throwing native factory must never retry.
        actor_ = game.create();
        if (!actor_ || actor_ == f.primary) { fail(Error::factory); return; }
        created_tick_ = ticks;
        phase = Phase::placing;
        return;
    }
    if (f.world != original_.world || f.primary != original_.primary ||
        f.registry != original_.registry || f.field_handle != original_.field_handle || f.guest != actor_) {
        phase = Phase::stopped; error = Error::changed_world; return;
    }
    if (stop_requested) {
        if (native_locomotion) game.command_locomotion(actor_,false);
        phase=Phase::stopped; return;
    }
    if (phase == Phase::placing) {
        if (!game.actor_ready(actor_)) {
            if (ticks - created_tick_ >= 120) fail(Error::model_timeout);
            return;
        }
        game.place(actor_, placement_); ++pose_writes;
        placed_tick_ = ticks;
        phase = Phase::settling;
        return;
    }
    if (!game.actor_ready(actor_)) { phase = Phase::stopped; error = Error::changed_world; return; }
    if (phase == Phase::settling) {
        if (ticks - placed_tick_ < 60) return;
        observed_origin_=game.position(actor_);
        if (!valid_position(observed_origin_)) { fail(Error::invalid_pose); return; }
        last_ms_ = follow_start_ = f.milliseconds;
        phase = Phase::following;
        return;
    }
    if (f.milliseconds < last_ms_ || f.milliseconds - follow_start_ >= 120000) {
        if (native_locomotion) game.command_locomotion(actor_,false);
        phase = Phase::stopped; return;
    }
    if(f.milliseconds-last_ms_>250) {
        if(native_pad_reference) { last_ms_=f.milliseconds; return; }
        if(native_locomotion) { game.command_locomotion(actor_,false); last_ms_=f.milliseconds; return; }
        phase=Phase::stopped; return;
    }
    if(native_pad_reference) {
        // Native PadManipulator owns input. This reference test only observes;
        // it must not apply a pose setter or our mirrored locomotion commands.
        const auto current=game.position(actor_);
        if(!valid_position(current)) { fail(Error::invalid_pose); return; }
        observed_displacement_mm=std::max(observed_displacement_mm,
            static_cast<std::uint64_t>(std::llround(distance(current,observed_origin_)*1000.)));
        last_ms_=f.milliseconds; return;
    }
    if (native_locomotion) {
        last_ms_=f.milliseconds;
        game.command_locomotion(actor_,!motion_paused);
        return;
    }
    if (motion_paused) { last_ms_ = f.milliseconds; return; }
    Vector target = f.position;
    for (unsigned i=0; i<3; ++i) target[i] += offset_[i];
    Vector current = game.position(actor_);
    if (!valid_position(current) || !valid_position(target)) { fail(Error::invalid_pose); return; }
    const float gap = distance(current, target);
    if (gap > 15) { phase = Phase::stopped; error = Error::changed_world; return; }
    const auto elapsed = std::min<std::uint64_t>(f.milliseconds-last_ms_, 100);
    last_ms_ = f.milliseconds;
    if (gap <= .05f || !elapsed) return;
    const float fraction = std::min(1.f, static_cast<float>(elapsed) * .006f / gap);
    for (unsigned i=0; i<3; ++i) current[i] += (target[i]-current[i]) * fraction;
    current[3] = target[3];
    game.place(actor_, current); ++pose_writes;
}
} // namespace skcoop::actors
