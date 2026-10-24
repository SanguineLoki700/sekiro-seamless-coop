#pragma once
#include "preflight.h"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace skcoop::probe {
inline constexpr std::string_view expected_sha256 =
    "637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856";
inline constexpr std::uint64_t expected_file_size = 68005144;
inline constexpr std::uint64_t expected_image_size = 0x42d2000;
// Research candidate from the owner-provided Sekiro Online binary, not a
// validated game binding. REA evidence:
// ev_956dbc87d51c7a272331f8961a302c83985623127fc8710186c6b35f4fb23186
// WorldChrMan xref: observed global VA 0x143d7a1e0 - preferred base 0x140000000.
inline constexpr std::uint64_t candidate_rva = 0x3d7a1e0;
inline constexpr std::uint64_t candidate_slot_size = 8;
inline constexpr std::size_t rtti_locator_size = 24;
inline constexpr std::size_t rtti_type_header_size = 16;
inline constexpr std::size_t rtti_name_limit = 128;
// Read-only field candidates corroborated in the identified target's loaded
// code and RTTI on 2026-10-23. See docs/ACTOR_RESEARCH.md. These do not define
// a complete class layout, a callable API, or a maximum number of players.
inline constexpr std::uint64_t actor_primary_offset = 0x88;
inline constexpr std::uint64_t actor_collection_offset = 0x40;
inline constexpr std::uint64_t actor_record_stride = 0x38;
inline constexpr std::size_t actor_sample_limit = 8;
inline constexpr std::int32_t actor_count_limit = 64;
inline constexpr std::size_t actor_field_limit = 16;
struct ActorSamplePlan { std::size_t sampled; bool partial; };

std::optional<ActorSamplePlan> actor_sample_plan(std::int32_t count) noexcept;
std::optional<std::uintptr_t> bounded_actor_field_address(std::uintptr_t object,
    std::uint64_t offset, std::uint64_t bytes) noexcept;
std::optional<std::uintptr_t> bounded_actor_record_address(std::uintptr_t array,
    std::size_t index) noexcept;

bool approved_research_identity(const launcher::ExecutableIdentity& identity) noexcept;
std::optional<std::uintptr_t> bounded_slot_address(std::uintptr_t module_base,
    std::uint64_t image_size, std::uint64_t rva) noexcept;
bool region_read_permitted(std::uint32_t state, std::uint32_t protection) noexcept;
bool image_contains(std::uintptr_t module_base, std::uint64_t image_size,
    std::uintptr_t address, std::uint64_t count) noexcept;
bool region_contains_readable(std::uintptr_t region_base, std::uint64_t region_size,
    std::uintptr_t address, std::uint64_t count, std::uint32_t state, std::uint32_t protection) noexcept;
std::optional<std::uintptr_t> rtti_descriptor_address(std::uintptr_t module_base,
    std::uint64_t image_size, std::uintptr_t locator_address, std::span<const unsigned char> locator) noexcept;
std::optional<std::string> rtti_vendor_type_name(std::span<const unsigned char> bytes);
}
