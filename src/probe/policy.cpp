#include "policy.h"
#include <Windows.h>
#include <algorithm>
#include <limits>

namespace skcoop::probe {
std::optional<ActorSamplePlan> actor_sample_plan(std::int32_t count) noexcept {
    if (count < 0 || count > actor_count_limit) return std::nullopt;
    const auto total = static_cast<std::size_t>(count);
    return ActorSamplePlan{std::min(total, actor_sample_limit), total > actor_sample_limit};
}

std::optional<std::uintptr_t> bounded_actor_field_address(std::uintptr_t object,
    std::uint64_t offset, std::uint64_t bytes) noexcept {
    constexpr std::uint64_t highest_user_address = 0x00007fffffffffff;
    if (object < 0x10000 || object > highest_user_address || bytes == 0 || bytes > actor_field_limit ||
        offset > highest_user_address - object) return std::nullopt;
    const auto address = static_cast<std::uint64_t>(object) + offset;
    if (bytes - 1 > highest_user_address - address) return std::nullopt;
    return static_cast<std::uintptr_t>(address);
}

std::optional<std::uintptr_t> bounded_actor_record_address(std::uintptr_t array,
    std::size_t index) noexcept {
    if (index >= actor_sample_limit) return std::nullopt;
    return bounded_actor_field_address(array, static_cast<std::uint64_t>(index) * actor_record_stride, 8);
}

bool approved_research_identity(const launcher::ExecutableIdentity& identity) noexcept {
    return identity.machine == IMAGE_FILE_MACHINE_AMD64 && identity.file_size == expected_file_size &&
        identity.sha256 == expected_sha256;
}

std::optional<std::uintptr_t> bounded_slot_address(std::uintptr_t module_base,
    std::uint64_t image_size, std::uint64_t rva) noexcept {
    if (module_base == 0 || image_size < candidate_slot_size || rva > image_size - candidate_slot_size)
        return std::nullopt;
    constexpr auto maximum = std::numeric_limits<std::uintptr_t>::max();
    if (image_size > maximum || module_base > maximum - image_size || rva > maximum - module_base)
        return std::nullopt;
    return module_base + static_cast<std::uintptr_t>(rva);
}

bool region_read_permitted(std::uint32_t state, std::uint32_t protection) noexcept {
    if (state != MEM_COMMIT || (protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
    switch (protection & 0xffu) {
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

bool image_contains(std::uintptr_t module_base, std::uint64_t image_size,
    std::uintptr_t address, std::uint64_t count) noexcept {
    constexpr auto maximum = std::numeric_limits<std::uintptr_t>::max();
    if (module_base == 0 || image_size == 0 || count == 0 || image_size > maximum ||
        module_base > maximum - image_size || address < module_base) return false;
    const auto offset = address - module_base;
    return offset < image_size && count <= image_size - offset;
}

bool region_contains_readable(std::uintptr_t region_base, std::uint64_t region_size,
    std::uintptr_t address, std::uint64_t count, std::uint32_t state, std::uint32_t protection) noexcept {
    return region_read_permitted(state, protection) && image_contains(region_base, region_size, address, count);
}

std::optional<std::uintptr_t> rtti_descriptor_address(std::uintptr_t module_base,
    std::uint64_t image_size, std::uintptr_t locator_address, std::span<const unsigned char> locator) noexcept {
    if (locator.size() != rtti_locator_size || !image_contains(module_base, image_size, locator_address, locator.size()))
        return std::nullopt;
    const auto word = [&](std::size_t at) {
        return static_cast<std::uint32_t>(locator[at]) | (static_cast<std::uint32_t>(locator[at + 1]) << 8) |
            (static_cast<std::uint32_t>(locator[at + 2]) << 16) | (static_cast<std::uint32_t>(locator[at + 3]) << 24);
    };
    if (word(0) != 1) return std::nullopt; // MSVC x64 image-relative COL revision.
    const auto self_rva = word(20);
    const auto type_rva = word(12);
    // locator_address is already inside the image; compare differences to avoid
    // adding an untrusted self RVA to the module base.
    if (locator_address - module_base != self_rva || type_rva >= image_size) return std::nullopt;
    const auto descriptor = module_base + static_cast<std::uintptr_t>(type_rva);
    if (!image_contains(module_base, image_size, descriptor, rtti_type_header_size + rtti_name_limit))
        return std::nullopt;
    return descriptor;
}

std::optional<std::string> rtti_vendor_type_name(std::span<const unsigned char> bytes) {
    if (bytes.size() != rtti_name_limit) return std::nullopt;
    const auto end = std::find(bytes.begin(), bytes.end(), static_cast<unsigned char>(0));
    if (end == bytes.end() || end - bytes.begin() <= 4 || bytes[0] != '.' || bytes[1] != '?' ||
        bytes[2] != 'A' || (bytes[3] != 'V' && bytes[3] != 'U')) return std::nullopt;
    if (!std::all_of(bytes.begin(), end, [](unsigned char character) { return character >= 0x20 && character <= 0x7e; }))
        return std::nullopt;
    return std::string(bytes.begin(), end);
}
}
