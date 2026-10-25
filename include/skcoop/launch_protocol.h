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
inline constexpr std::uint32_t protocol_version=6;
enum class ActorTest : std::uint32_t { off=0, follow=1, locomotion=2 };
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
    std::uint64_t reserved[4];
};

// Shared status is only fixed-width data. Use InterlockedExchange/Increment and
// InterlockedCompareExchange for fields modified/read across processes; 64-bit
// counters are naturally aligned. Runtime never owns/deletes the host's files.
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
    std::uint64_t reserved[8];
};
static_assert(sizeof(void*)==8 && sizeof(wchar_t)==2);
static_assert(std::is_standard_layout_v<Payload> && std::is_trivially_copyable_v<Payload>);
static_assert(std::is_standard_layout_v<SharedStatus> && std::is_trivially_copyable_v<SharedStatus>);
static_assert(offsetof(SharedStatus,hooks_mask)%8==0);
static_assert(sizeof(Payload)<65536);
} // namespace skcoop::launch
