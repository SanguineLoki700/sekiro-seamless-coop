#include "skcoop/lan/actions.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace skcoop::lan;
namespace {
void check(bool yes, int line) {
    if (!yes) throw std::runtime_error("Action history invariant failed at line " + std::to_string(line));
}
#define require(value) check((value), __LINE__)
ActionScope scope() {
    ActionScope value;
    value.room[0] = 1;
    value.connection[0] = 2;
    value.participant = 3;
    value.epoch = 7;
    return value;
}
ActionSnapshot snap(ActionCapture& capture, std::uint32_t ms) {
    const auto value = capture.snapshot(ms);
    require(value.has_value());
    require(valid(*value));
    return *value;
}
void producer() {
    ActionCapture capture(7);
    require(!valid(ActionKind::none) && !valid(static_cast<ActionKind>(action_kind_count + 1)));
    require(valid(ActionKind::stand_move_start) && valid(ActionKind::land_ground_jump));
    static_assert(action_kind_count == 20 && static_cast<unsigned>(ActionKind::attack_combo1) == 11);
    for (std::size_t i = 1; i <= action_kind_count; ++i) require(valid(static_cast<ActionKind>(i)));
    require(is_attack(ActionKind::attack_combo1) && is_attack(ActionKind::attack_combo2_release) && is_attack(ActionKind::attack_combo4));
    require(is_guard_enter(ActionKind::guard_enter) && is_guard_enter(ActionKind::guard_enter2) && is_guard_enter(ActionKind::guard_enter3));
    require(is_guard_exit(ActionKind::guard_exit) && is_combat(ActionKind::guard_exit) && !is_combat(ActionKind::ground_step));
    require(!is_attack(ActionKind::guard_enter) && !is_guard_enter(ActionKind::guard_exit) && !is_combat(ActionKind::none));
    require(valid(snap(capture, 0)));
    // Four pulses all occur between the old 50 ms offer boundaries.
    for (std::uint32_t i = 1; i <= 4; ++i)
        require(capture.capture(ActionKind::ground_step, i, static_cast<float>(i)));
    const auto first = snap(capture, 49);
    require(first.count == 4 && first.latest == 4);
    for (std::size_t i = 0; i < first.count; ++i)
        require(first.events[i].serial == i + 1 && first.events[i].source_ms == i + 1);
    require(snap(capture, 50).events == first.events);
    require(capture.offered(first.latest));
    require(capture.capture(ActionKind::ground_jump_ready, 51, -90));
    const auto second = snap(capture, 52);
    require(second.count == 4 && second.latest == 5 && second.events[0].serial == 2);
    require(second.events[3].kind == ActionKind::ground_jump_ready && second.events[3].direction == -90);

    ActionCapture blocked(7);
    for (std::uint32_t i = 0; i < 4; ++i) require(blocked.capture(ActionKind::ground_step, i));
    require(!blocked.capture(ActionKind::ground_jump_ready, 5));
    require(blocked.fault() == ActionFault::overflow && !blocked.snapshot(6));
    require(!blocked.offered(4)); // Overflow is terminal; no silent recovery.
    ActionCapture wrong_offer(7);
    require(!wrong_offer.offered(1) && wrong_offer.fault() == ActionFault::invalid);
    ActionCapture clock(7, 20);
    require(!clock.capture(ActionKind::ground_step, 19) && clock.fault() == ActionFault::clock_regressed);
    ActionCapture snapshot_clock(7, 20);
    require(!snapshot_clock.snapshot(19) && snapshot_clock.fault() == ActionFault::clock_regressed);
    ActionCapture bad_kind(7);
    require(!bad_kind.capture(ActionKind::none, 0) && bad_kind.fault() == ActionFault::invalid);
    ActionCapture bad_angle(7);
    require(!bad_angle.capture(ActionKind::ground_step, 0, std::numeric_limits<float>::quiet_NaN()));
    require(bad_angle.fault() == ActionFault::invalid);
    ActionCapture invalid_epoch(0);
    require(invalid_epoch.fault() == ActionFault::invalid);
}
void metadata() {
    ActionCapture capture(7);
    require(capture.capture(ActionKind::ground_step, 10, -45));
    const auto good = snap(capture, 20);
    auto bad = good;
    bad.count = 5; require(!valid(bad));
    bad = good; bad.count = 0; require(!valid(bad));
    bad = good; bad.latest = 2; require(!valid(bad));
    bad = good; bad.epoch = 0; require(!valid(bad));
    bad = good; bad.events[0].kind = static_cast<ActionKind>(200); require(!valid(bad));
    bad = good; bad.events[0].direction = std::numeric_limits<float>::infinity(); require(!valid(bad));
    bad = good; bad.events[0].direction = 360.01f; require(!valid(bad));
    bad = good; bad.events[0].source_ms = 21; require(!valid(bad));
    bad = good; bad.events[1].direction = 1; require(!valid(bad));
    require(capture.capture(ActionKind::ground_jump_ready, 21));
    bad = snap(capture, 22); bad.events[1].source_ms = 9; require(!valid(bad));
    auto bad_scope = scope(); bad_scope.connection = {}; require(!valid(bad_scope));
    bad_scope = scope(); bad_scope.room = {}; require(!valid(bad_scope));
    bad_scope = scope(); bad_scope.participant = 0; require(!valid(bad_scope));
    bad_scope = scope(); bad_scope.epoch = 0; require(!valid(bad_scope));
}
void sprint_state() {
    ActionCapture capture(7);
    require(!snap(capture, 0).sprinting);
    const std::array reset_events{
        ActionKind::stand_move_start, ActionKind::stand_run_stop, ActionKind::stand_walk_stop,
        ActionKind::ground_step, ActionKind::sprint_stop_ready, ActionKind::sprint_stop_quick_turn_front,
        ActionKind::ground_jump_ready, ActionKind::ground_jump_land_ready, ActionKind::land_ground_jump};
    std::uint32_t ms{};
    for (const auto kind : reset_events) {
        require(capture.capture(ActionKind::sprint_start_from_step, ++ms));
        const auto started = snap(capture, ms);
        require(started.sprinting && capture.offered(started.latest));
        const auto repeated = snap(capture, ++ms);
        require(repeated.sprinting && repeated.latest == started.latest && repeated.events == started.events);
        require(capture.capture(kind, ++ms));
        const auto stopped = snap(capture, ms);
        require(!stopped.sprinting && capture.offered(stopped.latest));
    }
    ActionCapture running(7);
    require(running.capture(ActionKind::sprint_start_from_step, 1));
    const auto current = snap(running, 10);
    auto bad = current; bad.sprinting = false; require(!valid(bad));
    ActionCapture empty(7); bad = snap(empty, 0); bad.sprinting = true; require(!valid(bad));

    ActionTimeline timeline;
    // A fresh baseline restores the held state while dropping the old discrete
    // start event. Runtime should restore its owned guest state once here.
    require(timeline.baseline(scope(), current, 1, 1000));
    require(timeline.held_sprint() && timeline.pending() == 0 && !timeline.take(1100));
    timeline.suspend();
    require(!timeline.held_sprint() && !timeline.take(1110));
    const auto resumed = snap(running, 100);
    require(timeline.baseline(scope(), resumed, 2, 1110));
    require(timeline.held_sprint() && !timeline.take(1210) && timeline.delivered() == 0);
    require(!timeline.take(1611) && !timeline.armed() && !timeline.held_sprint());
    const auto fresh = snap(running, 700);
    require(timeline.baseline(scope(), fresh, 3, 1700));
    require(timeline.held_sprint() && !timeline.take(1800));
    require(running.capture(ActionKind::sprint_stop_ready, 701));
    require(timeline.push(scope(), snap(running, 710), 4, 1810) == ActionPush::accepted);
    require(!timeline.held_sprint());
    const auto stop = timeline.take(1901);
    require(stop && stop->kind == ActionKind::sprint_stop_ready && !timeline.held_sprint());
}
void guard_state() {
    ActionCapture source(7);
    const auto initial = snap(source, 0);
    auto invented = initial; invented.guarding = true; require(!valid(invented));
    require(source.capture(ActionKind::attack_combo1, 1));
    auto attack = snap(source, 1); attack.guarding = true; require(!valid(attack));
    require(source.offered(attack.latest));
    require(source.capture(ActionKind::guard_enter, 2));
    auto pressed = snap(source, 2); require(pressed.guarding && !pressed.sprinting);
    auto contradicted = pressed; contradicted.guarding = false; require(!valid(contradicted));
    require(source.offered(pressed.latest));
    for (std::uint32_t ms = 3; ms <= 9; ++ms) {
        require(source.capture(ms % 2 ? ActionKind::stand_move_start : ActionKind::attack_combo2, ms));
        const auto frame = snap(source, ms); require(frame.guarding && source.offered(frame.latest));
    }
    const auto rolled = snap(source, 10);
    require(rolled.latest == 9 && rolled.count == 4 && rolled.guarding);
    for (std::size_t i = 0; i < rolled.count; ++i) require(!is_guard_enter(rolled.events[i].kind));
    // This catches a defective latest-event-only held-state reducer.
    require(!is_guard_enter(rolled.events[rolled.count - 1].kind));
    auto flipped = rolled; flipped.guarding = false;
    require(valid(flipped) && !consistent_guard(rolled, flipped));
    ActionTimeline repeat;
    require(repeat.baseline(scope(), rolled, 1, 1000));
    require(repeat.held_guard() && !repeat.take(1100));
    require(repeat.push(scope(), flipped, 2, 1101) == ActionPush::failed);
    require(repeat.fault() == ActionFault::invalid && !repeat.held_guard());

    require(source.capture(ActionKind::attack_combo3, 11));
    const auto advanced = snap(source, 12);
    require(advanced.guarding && consistent_guard(rolled, advanced));
    flipped = advanced; flipped.guarding = false;
    require(valid(flipped) && !consistent_guard(rolled, flipped));
    ActionTimeline baseline;
    require(baseline.baseline(scope(), rolled, 1, 1000));
    require(!baseline.baseline(scope(), flipped, 2, 1100) && baseline.fault() == ActionFault::invalid);
    // Explicit rebaseline can skip an unknown old prefix, without replaying it.
    ActionTimeline resume;
    require(resume.baseline(scope(), initial, 1, 1000));
    require(resume.baseline(scope(), advanced, 2, 1100));
    require(resume.held_guard() && resume.pending() == 0 && !resume.take(1200));
    resume.suspend(); require(!resume.held_guard());
    require(resume.baseline(scope(), snap(source, 100), 3, 1250));
    require(resume.held_guard() && resume.delivered() == 0 && !resume.take(1350));
    require(!resume.take(1751) && !resume.held_guard() && !resume.armed());
    require(source.offered(advanced.latest));
    require(source.capture(ActionKind::guard_exit, 101));
    const auto released = snap(source, 110); require(!released.guarding);
    contradicted = released; contradicted.guarding = true; require(!valid(contradicted));
    require(consistent_guard(advanced, released));

    ActionCapture rapid(7); ActionTimeline pulses;
    require(pulses.baseline(scope(), snap(rapid, 0), 1, 1000));
    const std::array commands{ActionKind::guard_enter, ActionKind::guard_exit, ActionKind::guard_enter2, ActionKind::guard_exit};
    for (std::size_t i = 0; i < commands.size(); ++i) require(rapid.capture(commands[i], static_cast<std::uint32_t>(i + 1)));
    const auto taps = snap(rapid, 50); require(!taps.guarding);
    require(pulses.push(scope(), taps, 2, 1050) == ActionPush::accepted);
    for (const auto& event : taps.events) require(pulses.take(1104) == event);
    require(!pulses.take(1104) && pulses.delivered() == 4);
    require(pulses.push(scope(), taps, 2, 1050) == ActionPush::stale && !pulses.take(1200));

    ActionCapture expired(7); require(expired.capture(ActionKind::guard_enter3, 1));
    const auto held = snap(expired, 1); ActionTimeline latest_state;
    require(latest_state.baseline(scope(), held, 1, 1000) && latest_state.held_guard());
    require(expired.capture(ActionKind::guard_exit, 2));
    require(latest_state.push(scope(), snap(expired, 253), 2, 1100) == ActionPush::accepted);
    require(!latest_state.held_guard() && latest_state.pending() == 0 && latest_state.discarded() == 1);
    require(!latest_state.take(1200)); // No expired guard/deflect edge is invented.

    ActionCapture overflow(7);
    for (const auto kind : {ActionKind::attack_combo1, ActionKind::attack_combo1_release, ActionKind::attack_combo2, ActionKind::attack_combo2_release})
        require(overflow.capture(kind, 1));
    require(!overflow.capture(ActionKind::attack_combo3, 2) && overflow.fault() == ActionFault::overflow);
}
void timeline() {
    ActionCapture capture(7);
    ActionTimeline timeline;
    require(timeline.push(scope(), snap(capture, 0), 1, 1000) == ActionPush::unarmed);
    require(timeline.baseline(scope(), snap(capture, 0), 1, 1000));
    require(capture.capture(ActionKind::ground_step, 10, 45));
    require(capture.capture(ActionKind::ground_jump_ready, 30, -90));
    const auto update = snap(capture, 50);
    require(timeline.push(scope(), update, 2, 1050) == ActionPush::accepted);
    require(timeline.pending() == 2 && !timeline.take(1109));
    auto event = timeline.take(1110);
    require(event == update.events[0]);
    require(!timeline.take(1129));
    event = timeline.take(1130);
    require(event == update.events[1]);
    require(!timeline.take(1130) && timeline.delivered() == 2);
    require(timeline.push(scope(), update, 2, 1050) == ActionPush::stale);
    require(timeline.push(scope(), update, 1, 1000) == ActionPush::stale);
    require(timeline.push(scope(), snap(capture, 60), 3, 1060) == ActionPush::accepted);
    require(!timeline.take(1200) && timeline.delivered() == 2);
    // Freshness is a receiver-time bound, not a measurement of transit age.
    require(!timeline.take(1561) && !timeline.armed() && timeline.fault() == ActionFault::none);

    ActionTimeline delayed;
    ActionCapture other(7);
    require(delayed.baseline(scope(), snap(other, 0), 1, 0));
    require(other.capture(ActionKind::ground_step, 1));
    require(delayed.push(scope(), snap(other, 10), 2, 10) == ActionPush::accepted);
    require(!delayed.take(261) && delayed.discarded() == 1);
    ActionTimeline old_source;
    ActionCapture old(7);
    require(old_source.baseline(scope(), snap(old, 0), 1, 0));
    require(old.capture(ActionKind::ground_step, 1));
    require(old_source.push(scope(), snap(old, 252), 2, 252) == ActionPush::accepted);
    require(old_source.pending() == 0 && old_source.discarded() == 1);
}
void pause_and_lifetime() {
    ActionCapture capture(7);
    require(capture.capture(ActionKind::ground_step, 1));
    ActionTimeline timeline;
    // Arm from the current history, never replay the action before spawn.
    require(timeline.baseline(scope(), snap(capture, 10), 1, 10));
    require(!timeline.take(110) && timeline.seen_serial() == 1);
    require(capture.capture(ActionKind::ground_jump_ready, 11));
    require(timeline.push(scope(), snap(capture, 20), 2, 20) == ActionPush::accepted);
    require(timeline.pending() == 1);
    timeline.suspend();
    require(timeline.pending() == 0 && !timeline.take(120));
    require(capture.capture(ActionKind::ground_step, 21));
    const auto paused = snap(capture, 30);
    require(timeline.push(scope(), paused, 3, 30) == ActionPush::unarmed);
    require(timeline.baseline(scope(), paused, 3, 30));
    require(!timeline.take(130)); // Neither paused action reappears on resume.
    require(capture.capture(ActionKind::land_ground_jump, 31));
    const auto resumed = snap(capture, 40);
    require(timeline.push(scope(), resumed, 4, 40) == ActionPush::accepted);
    require(timeline.take(131) == resumed.events[3]);
    require(timeline.delivered() == 1 && timeline.discarded() == 1);

    for (unsigned field = 0; field < 4; ++field) {
        ActionTimeline changed;
        ActionCapture source(7);
        require(changed.baseline(scope(), snap(source, 0), 1, 0));
        auto foreign = scope();
        if (field == 0) ++foreign.room[0];
        if (field == 1) ++foreign.connection[0];
        if (field == 2) ++foreign.participant;
        if (field == 3) ++foreign.epoch;
        require(changed.push(foreign, snap(source, 1), 2, 1) == ActionPush::failed);
        require(changed.fault() == ActionFault::scope_changed && !changed.armed());
        require(!changed.baseline(scope(), snap(source, 2), 3, 2));
    }
}
void failures() {
    ActionCapture capture(7);
    ActionTimeline gap;
    require(gap.baseline(scope(), snap(capture, 0), 1, 0));
    for (std::uint32_t i = 1; i <= 4; ++i) require(capture.capture(ActionKind::ground_step, i));
    require(capture.offered(4));
    require(capture.capture(ActionKind::ground_jump_ready, 5));
    require(gap.push(scope(), snap(capture, 5), 2, 5) == ActionPush::failed);
    require(gap.fault() == ActionFault::gap && gap.delivered() == 0);

    ActionCapture producer(7);
    ActionTimeline altered;
    require(altered.baseline(scope(), snap(producer, 0), 1, 0));
    require(producer.capture(ActionKind::ground_step, 1));
    auto update = snap(producer, 2);
    require(altered.push(scope(), update, 2, 2) == ActionPush::accepted);
    update.events[0].kind = ActionKind::ground_jump_ready;
    require(altered.push(scope(), update, 3, 3) == ActionPush::failed);
    require(altered.fault() == ActionFault::invalid && altered.pending() == 0);

    ActionTimeline clock;
    ActionCapture clock_source(7);
    require(clock.baseline(scope(), snap(clock_source, 10), 1, 10));
    require(clock.push(scope(), snap(clock_source, 11), 2, 9) == ActionPush::failed);
    require(clock.fault() == ActionFault::clock_regressed);
    ActionTimeline local_clock;
    require(local_clock.baseline(scope(), snap(clock_source, 11), 1, 11));
    require(!local_clock.take(12));
    require(!local_clock.take(11) && local_clock.fault() == ActionFault::clock_regressed);
    ActionTimeline bad_snapshot;
    update = snap(producer, 3); update.events[0].kind = ActionKind::none;
    require(!bad_snapshot.baseline(scope(), update, 1, 3));
    require(bad_snapshot.fault() == ActionFault::invalid);

    ActionCapture flood(7);
    ActionTimeline full;
    require(full.baseline(scope(), snap(flood, 0), 1, 0));
    for (std::uint32_t i = 1; i <= action_queue_capacity + 1; ++i) {
        require(flood.capture(ActionKind::ground_step, i));
        const auto frame = snap(flood, i);
        const auto result = full.push(scope(), frame, i + 1, i);
        require(flood.offered(frame.latest));
        if (i <= action_queue_capacity) require(result == ActionPush::accepted);
        else require(result == ActionPush::failed);
    }
    require(full.fault() == ActionFault::overflow && full.delivered() == 0 && full.pending() == 0);
}

struct Delivery {
    ActionSnapshot snapshot;
    std::uint64_t sequence{}, arrival{};
};
bool expected_trace(const std::vector<ActionEvent>& expected, const std::vector<ActionEvent>& actual) {
    return expected == actual;
}
void seeded_transport() {
    for (unsigned seed = 0; seed < 64; ++seed) {
        std::mt19937 random(seed + 0x5e41u);
        ActionCapture capture(7);
        const auto initial = snap(capture, 0);
        std::vector<Delivery> deliveries;
        std::vector<ActionEvent> expected;
        std::uint32_t next_action = 1;
        std::uint64_t sequence = 1;
        bool dropped_previous = false;
        for (std::uint32_t now = 1; now <= 4000; ++now) {
            if (now == next_action && now <= 3600) {
                const auto kind = static_cast<ActionKind>(1 + random() % action_kind_count);
                const auto direction = static_cast<float>(static_cast<int>(random() % 721) - 360);
                require(capture.capture(kind, now, direction));
                const auto current = snap(capture, now);
                expected.push_back(current.events[current.count - 1]);
                next_action += 60 + random() % 41;
            }
            if (now % 50) continue;
            const auto frame = snap(capture, now);
            require(capture.offered(frame.latest));
            ++sequence;
            // Isolated loss is recoverable through persistent history.
            const bool dropped = !dropped_previous && random() % 5 == 0 && now < 3800;
            dropped_previous = dropped;
            if (dropped) continue;
            const auto arrival = now + random() % 91;
            deliveries.push_back({frame, sequence, arrival});
            if (random() % 3 == 0) deliveries.push_back({frame, sequence, arrival + 1});
            if (random() % 4 == 0) deliveries.push_back({frame, sequence, arrival + 120});
        }
        std::stable_sort(deliveries.begin(), deliveries.end(), [](const auto& a, const auto& b) {
            return a.arrival < b.arrival;
        });
        ActionTimeline timeline;
        require(timeline.baseline(scope(), initial, 1, 0));
        std::vector<ActionEvent> actual, repeat_defect, latest_only_defect;
        std::size_t next{};
        for (std::uint64_t now = 0; now <= 4400; ++now) {
            while (next < deliveries.size() && deliveries[next].arrival == now) {
                const auto& delivery = deliveries[next++];
                const auto result = timeline.push(scope(), delivery.snapshot, delivery.sequence, now);
                require(result == ActionPush::accepted || result == ActionPush::stale);
                if (result == ActionPush::accepted) require(timeline.held_guard() == delivery.snapshot.guarding);
                // Deliberately broken consumers of the same generated stream:
                // one replays the latest trigger for every frame; the other
                // coalesces all unseen events into the last trigger only.
                if (delivery.snapshot.count) {
                    const auto& last = delivery.snapshot.events[delivery.snapshot.count - 1];
                    repeat_defect.push_back(last);
                    if (latest_only_defect.empty() || last.serial > latest_only_defect.back().serial)
                        latest_only_defect.push_back(last);
                }
            }
            while (const auto event = timeline.take(now)) actual.push_back(*event);
            require(timeline.fault() == ActionFault::none);
        }
        require(next == deliveries.size());
        require(expected_trace(expected, actual));
        require(timeline.held_guard() == snap(capture, 4000).guarding);
        require(!expected_trace(expected, repeat_defect));
        // Network loss can coalesce more than one event in one history. At
        // least one seed must demonstrate that a last-trigger-only consumer
        // loses actions; the deterministic pulse case below always does.
        if (seed == 0) require(!expected_trace(expected, latest_only_defect));
    }
    ActionCapture capture(7);
    const auto initial = snap(capture, 0);
    require(capture.capture(ActionKind::ground_step, 1));
    require(capture.capture(ActionKind::ground_jump_ready, 2));
    const auto coalesced = snap(capture, 50);
    const std::vector<ActionEvent> expected{coalesced.events[0], coalesced.events[1]};
    const std::vector<ActionEvent> overwrite_defect{coalesced.events[1]};
    require(!expected_trace(expected, overwrite_defect));
    ActionTimeline correct;
    require(correct.baseline(scope(), initial, 1, 0));
    require(correct.push(scope(), coalesced, 2, 50) == ActionPush::accepted);
    std::vector<ActionEvent> actual;
    while (const auto event = correct.take(150)) actual.push_back(*event);
    require(expected_trace(expected, actual));
}
}
int main() {
    try {
        producer(); metadata(); sprint_state(); guard_state(); timeline(); pause_and_lifetime(); failures(); seeded_transport();
        std::cout << "PASS bounded sub-frame action capture, delayed at-most-once replay, scope/lifetime guards, "
                     "seeded loss/duplicate/reorder and defective-consumer detection\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
