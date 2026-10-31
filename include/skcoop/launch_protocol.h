#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace skcoop::launch {
inline constexpr GUID payload_guid{0x1a6ed496,0xf4ab,0x4f85,{0x84,0x8c,0x2a,0x9c,0x83,0xd9,0xb0,0x6a}};
inline constexpr std::uint32_t protocol_version=17;
enum class ActorTest : std::uint32_t { off=0, follow=1, locomotion=2, pad_reference=3, graph_locomotion=4, lan=5, native_commands=6,lan_native_commands=7 };
inline constexpr std::size_t path_units=1024;
inline constexpr std::size_t account_units=32;

enum class Phase : LONG { empty=0, payload_prepared=1, bootstrapping=2, ready=3, failed=4, shutdown=5 };

// Fixed-size Detours payload; the two handle values belong to the created child,
// obtained by DuplicateHandle before ResumeThread. They are not host handles.
struct Payload {
    std::uint32_t struct_size;
    std::uint32_t version;
    std::uint32_t host_pid;
    std::uint32_t child_pid;
    std::uint8_t nonce[16];
    std::uint8_t executable_sha256[32];
    wchar_t exe_path[path_units];
    wchar_t solo_roaming[path_units];
    wchar_t coop_roaming[path_units];
    wchar_t account[account_units];
    HANDLE mapping_handle;
    HANDLE ready_event;
    ActorTest actor_test;
    std::uint32_t actor_padding;
    std::uint32_t lan_role; // 0 disabled, 1 host, 2 guest; never a Steam identity.
    std::uint32_t lan_profile; // 0 legacy fixture, 1 own profile, 2 local combat preview.
    char lan_ticket[160]; // Private credential, omitted from receipts.
    std::uint8_t scenario_sha256[32]; // Declared controlled scenario; never a map ID.
    std::uint64_t reserved[4];
};

// Shared status is only fixed-width data. Use InterlockedExchange/Increment and
// InterlockedCompareExchange for fields modified/read across processes; 64-bit
// counters are naturally aligned. Runtime never owns/deletes the host's files.
inline constexpr std::size_t native_trace_capacity=128;
inline constexpr LONG native_trace_position_valid=1,native_trace_consumption_valid=2,native_trace_tail_complete=4;
inline constexpr LONG native_trace_action_bound=8,native_trace_attacking=16,native_trace_guarding=32,native_trace_action_idle=64;
struct alignas(8) NativeControllerTrace {
    volatile LONG phase,state_code;
    volatile LONG64 input_generation,frame_sequence,elapsed_ms;
    // Position is relative to the trial origin, never an absolute coordinate.
    // resolved_milli is [world direction X, world direction Z, intensity].
    volatile LONG position_mm[3],resolved_milli[3],held_mask,observation_flags;
    // Flags: position_valid, consumption_valid, tail_complete (position only),
    // action_bound, actual attacking, actual held guard, actual action idle.
    // The last three are meaningful only with action_bound. state_code is the
    // canonical action category in protocol 16, not a stale branch variable.
    // A tail marker supplies no input evidence without consumption_valid.
};
struct alignas(8) NativeMailboxDiagnostic {
    volatile LONG kind,latch_fault,button,expected_held,observed_held,held_ms;
    volatile LONG expected_move_micro[3],observed_move_micro[3]; // 1e-6 units; direction X/Z, intensity.
    volatile LONG64 epoch;
};
struct alignas(8) NativeMovementDiagnostic {
    volatile LONG query_mask,yaw_milli,axes_milli[2];
    volatile LONG after_pad_milli[4],at_consumer_milli[4],after_resolver_milli[4];
    volatile LONG speed_milli,angle_milli;
    volatile LONG64 pad_epoch,consumer_epoch,resolver_epoch;
};
// A retained sample from the failed mailbox epoch. Flags describe availability
// and native turn observations only; this diagnostic supplies no acceptance.
inline constexpr LONG native_failed_input_pad_valid=1,native_failed_input_original_turn_observed=2,
    native_failed_input_turn_matched=4,native_failed_input_consumer_valid=8;
inline constexpr LONG native_failed_input_known_flags=native_failed_input_pad_valid|
    native_failed_input_original_turn_observed|native_failed_input_turn_matched|native_failed_input_consumer_valid;
struct alignas(8) NativeFailedInputDiagnostic {
    volatile LONG64 epoch;
    volatile LONG flags,padding;
    // Actual raw float4 lanes [x,y,z,w] in 1e-6 units, never normalized input.
    // pad_valid supplies intent and after_pad together; consumer_valid is
    // independent. Epoch is published last, before the mailbox failure kind.
    volatile LONG intent_micro[4],after_pad_micro[4],at_consumer_micro[4];
};
struct alignas(8) SharedStatus {
    std::uint32_t struct_size;
    std::uint32_t version;
    std::uint32_t host_pid;
    std::uint32_t child_pid;
    std::uint8_t nonce[16];
    volatile LONG phase;
    volatile LONG fatal_error;
    volatile LONG64 hooks_mask;
    volatile LONG64 routed_opens;
    volatile LONG64 routed_metadata;
    volatile LONG64 routed_copies;
    volatile LONG64 routed_deletes;
    volatile LONG64 denied_operations;
    volatile LONG64 primary_write_opens;
    volatile LONG64 backup_write_opens;
    volatile LONG64 unsupported_operations;
    volatile LONG64 successful_primary_reads;
    volatile LONG64 successful_backup_reads;
    volatile LONG64 process_detach_count;
    volatile LONG64 native_redirected;
    volatile LONG64 native_denied;
    volatile LONG64 native_passthrough;
    volatile LONG64 native_internal_bypass;
    volatile LONG64 native_appdata_redirects;
    volatile LONG64 native_directory_queries;
    volatile LONG64 native_copy_failures;
    volatile LONG64 native_destination_may_be_modified;
    volatile LONG actor_phase;
    volatile LONG actor_error;
    volatile LONG64 actor_creation_calls;
    volatile LONG64 actor_ticks;
    volatile LONG64 actor_pose_writes;
    volatile LONG actor_motion_paused;
    volatile LONG actor_metrics_padding;
    volatile LONG64 actor_creation_attempts;
    volatile LONG64 actor_world_callbacks;
    volatile LONG64 actor_callback_total_us;
    volatile LONG64 actor_callback_max_us;
    volatile LONG64 actor_factory_us;
    volatile LONG64 actor_max_frame_interval_us;
    volatile LONG64 actor_locomotion_writes;
    volatile LONG64 actor_zero_commands;
    volatile LONG64 actor_controller_callbacks;
    volatile LONG64 actor_controller_context_errors;
    volatile LONG64 actor_source_captures;
    volatile LONG64 actor_source_nonzero;
    volatile LONG64 actor_action_finalizes;
    volatile LONG64 actor_invalid_deltas;
    volatile LONG64 actor_observed_displacement_mm;
    volatile LONG64 actor_graph_writes;
    volatile LONG64 actor_graph_nonzero;
    volatile LONG lan_state;
    volatile LONG lan_error;
    volatile LONG lan_wait;
    volatile LONG lan_system_error;
    volatile LONG64 lan_frames_sent;
    volatile LONG64 lan_frames_received;
    volatile LONG64 lan_rejected;
    volatile LONG64 lan_stale_frames;
    volatile LONG action_error;
    volatile LONG action_sprinting;
    volatile LONG64 action_captured;
    volatile LONG64 action_replayed;
    volatile LONG64 action_discarded;
    volatile LONG64 action_released;
    volatile LONG64 action_ignored_sources;
    volatile LONG64 action_unsupported_source;
    volatile LONG64 action_jump_captured,action_step_captured,action_sprint_captured;
    volatile LONG64 action_jump_replayed,action_step_replayed,action_sprint_replayed;
    volatile LONG action_guarding,action_guard_rearm_required;
    volatile LONG64 action_attack_captured,action_attack_replayed;
    volatile LONG64 action_guard_enter_captured,action_guard_exit_captured;
    volatile LONG64 action_guard_enter_replayed,action_guard_exit_replayed;
    volatile LONG native_controller_phase,native_controller_fault;
    volatile LONG native_controller_state_code,native_controller_output_bound;
    volatile LONG native_controller_negative_passed,native_controller_trace_count;
    volatile LONG native_controller_trace_overflow,native_controller_reason;
    volatile LONG64 native_controller_commands,native_controller_consumed,
        native_controller_guard_ms,native_controller_displacement_mm,
        native_controller_manual_events,native_controller_primary_writes;
    volatile LONG64 native_controller_input_queries,native_controller_resolvers,
        native_controller_held_queries,native_controller_pressed_queries;
    NativeControllerTrace native_controller_trace[native_trace_capacity];
    NativeMailboxDiagnostic native_controller_mailbox;
    NativeMovementDiagnostic native_controller_movement;
    NativeFailedInputDiagnostic native_controller_failed_input;
    std::uint64_t reserved[8];
};
static_assert(sizeof(void*)==8 && sizeof(wchar_t)==2);
static_assert(std::is_standard_layout_v<Payload> && std::is_trivially_copyable_v<Payload>);
static_assert(std::is_standard_layout_v<SharedStatus> && std::is_trivially_copyable_v<SharedStatus>);
static_assert(offsetof(SharedStatus,hooks_mask)%8==0);
static_assert(offsetof(SharedStatus,action_captured)%8==0 && offsetof(SharedStatus,action_unsupported_source)%8==0);
static_assert(offsetof(SharedStatus,action_attack_captured)%8==0);
static_assert(sizeof(NativeControllerTrace)==64 && alignof(NativeControllerTrace)==8);
static_assert(sizeof(NativeFailedInputDiagnostic)==64 && alignof(NativeFailedInputDiagnostic)==8);
static_assert(std::is_standard_layout_v<NativeFailedInputDiagnostic> && std::is_trivially_copyable_v<NativeFailedInputDiagnostic>);
static_assert(offsetof(NativeFailedInputDiagnostic,epoch)==0 && offsetof(NativeFailedInputDiagnostic,flags)==8 &&
    offsetof(NativeFailedInputDiagnostic,padding)==12 && offsetof(NativeFailedInputDiagnostic,intent_micro)==16 &&
    offsetof(NativeFailedInputDiagnostic,after_pad_micro)==32 && offsetof(NativeFailedInputDiagnostic,at_consumer_micro)==48);
static_assert(offsetof(SharedStatus,native_controller_failed_input)%8==0);
static_assert(sizeof(Payload)<65536);
} // namespace skcoop::launch
