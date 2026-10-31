#pragma once
#include "skcoop/actors/player_command.hpp"
#include <array>
#include <optional>
#include <string_view>

namespace skcoop::actors {
// Offline policy and oracle only. A passing supplied trace does not establish
// that the native binding exists or that two real clients were tested.
enum class ControllerScenarioPhase {
    waiting_ready,neutral,moving,movement_release,attack_press,attack_release,
    guard_hold,guard_release,final_neutral,complete,stopped,failed,inconclusive
};
enum class ControllerScenarioFault {
    none,invalid_generation,clock_regressed,tick_gap,preparation_timeout,total_timeout,
    invalid_observation,stale_observation,readiness_lost,scope_changed,unsafe_driver,
    primary_changed,command_failure,invalid_consumption,consumer_timeout,
    not_neutral,movement_missing,movement_exceeded,movement_not_released,
    attack_missing,attack_not_released,guard_missing,guard_not_held,guard_not_released,
    action_result_unbound
};
// Diagnostic collection is an explicit opt-in and never grants acceptance.
enum class ControllerScenarioMode {strict,diagnostic_unbound};
std::string_view controller_scenario_phase_name(ControllerScenarioPhase)noexcept;
std::string_view controller_scenario_fault_name(ControllerScenarioFault)noexcept;
bool terminal(ControllerScenarioPhase)noexcept;

struct ControllerScenarioObservation {
    NativePlayerState guest;
    // Monotonic native observation, bound by the adapter to one verified world,
    // primary and owned guest lifetime. These IDs are not native pointers.
    std::uint64_t sample_sequence{},observed_ms{},lifetime{};
    bool ownership_verified{};
    // Actual attack/guard outcome classification is separate from native input
    // consumption. Unknown state-code families must leave this false.
    bool action_output_bound{};
    std::array<float,3> primary_position{};
    PlayerInput primary_input;
    // Read from the downstream native resolver, independently of provider
    // submission/writeback. The consumed identifiers in guest refer to this
    // resolved value. Merely storing the submitted command must leave false.
    bool consumer_observed{};
    // Guard hold/release must be resolved as a native held level. A pulse-only
    // button resolver or the provider's requested mask cannot attest this.
    bool held_state_observed{};
    PlayerInput resolved_input;
    // Project-issued operations only. Normal internal engine animation/event
    // processing is not a manual HKB enqueue. Exactly one initial placement is
    // allowed; no later project pose/graph driver can make this oracle pass.
    std::uint64_t pose_setter_calls{},manual_hkb_enqueues{},direct_graph_writes{},
        other_driver_commands{},primary_write_calls{};
};
struct ControllerScenarioReport {
    ControllerScenarioPhase phase{ControllerScenarioPhase::waiting_ready};
    ControllerScenarioFault fault{};
    std::uint64_t commands{},consumed_commands{},observations{},guard_held_ms{};
    float displacement{}; // Maximum forward X/Z progress along the walk command.
    bool attack_observed{},guard_released{};
    bool action_result_unbound{}; // Latched: a later binding cannot validate prior samples.
    bool assertions_satisfied()const noexcept{return phase==ControllerScenarioPhase::complete&&fault==ControllerScenarioFault::none&&!action_result_unbound;}
};
struct ControllerScenarioStep {
    std::optional<PlayerCommand> command;
    // The adapter must neutralize only a still-verified owned binding. This is
    // a request, never authorization to write through a changed/dead lifetime.
    bool neutralize{true};
};
class ControllerScenario {
public:
    static constexpr std::uint64_t preparation_timeout_ms=30000,max_run_ms=12000,
        sample_expiry_ms=100,max_tick_gap_ms=250,consumer_timeout_ms=250,
        action_start_timeout_ms=1000,movement_settle_timeout_ms=2000,movement_stable_ms=250;
    // Neutral input can precede the native stop transition. Bound that entire
    // coast, then require actual idle and a separate small 3D stable envelope.
    static constexpr float movement_coast_limit=.4f,movement_idle_drift_limit=.05f;
    static constexpr float movement_input_tolerance=1e-4f;
    static constexpr float max_displacement=4.f;
    explicit ControllerScenario(std::uint64_t input_generation,std::uint64_t start_ms,
        ControllerScenarioMode mode=ControllerScenarioMode::strict)noexcept;
    ControllerScenarioStep tick(std::uint64_t now_ms,const ControllerScenarioObservation&)noexcept;
    void request_stop()noexcept{stop_requested_=true;}
    const ControllerScenarioReport& report()const noexcept{return report_;}
private:
    struct IssuedRange {std::uint64_t first{},last{};PlayerInput input;};
    PlayerCommandCapture capture_;
    ControllerScenarioMode mode_;
    ControllerScenarioReport report_;
    std::array<IssuedRange,9> issued_{};
    std::uint64_t generation_{},start_{},checked_{},active_at_{},phase_at_{},phase_first_{},
        sample_{},sample_at_{},lifetime_{},last_issued_{},last_consumed_{},progress_at_{},guard_last_{},stop_idle_at_{};
    std::array<float,3> primary_origin_{},guest_origin_{},movement_origin_{},movement_stop_origin_{},stop_idle_origin_{};
    bool stop_requested_{},phase_consumed_{},guard_seen_{},guard_consecutive_{},stop_origin_set_{},stop_idle_set_{};
    float stop_drift_{};
    ControllerScenarioStep fail(ControllerScenarioFault)noexcept;
    ControllerScenarioStep emit(std::uint64_t)noexcept;
    void enter(ControllerScenarioPhase,std::uint64_t)noexcept;
    bool check_invariants(std::uint64_t,const ControllerScenarioObservation&)noexcept;
    bool observe_consumer(std::uint64_t,const ControllerScenarioObservation&,bool fresh)noexcept;
    bool observe_output_binding(const ControllerScenarioObservation&)noexcept;
    bool output_assertions_bound(const ControllerScenarioObservation&)const noexcept;
};
}
