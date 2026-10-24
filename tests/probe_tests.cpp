#include "policy.h"
#include "process.h"
#include <Windows.h>
#include <array>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void word(std::span<unsigned char> bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) bytes[offset + index] = static_cast<unsigned char>(value >> (index * 8));
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        using namespace skcoop::probe;
        require(argc == 2, "Pass the built diagnostic probe executable.");
        skcoop::launcher::ExecutableIdentity identity{{}, IMAGE_FILE_MACHINE_AMD64, expected_file_size, std::string(expected_sha256)};
        require(approved_research_identity(identity), "The exact research identity was not admitted.");
        identity.sha256[0] = '0';
        require(!approved_research_identity(identity), "An altered digest was admitted.");
        identity.sha256 = std::string(expected_sha256);
        identity.file_size -= 1;
        require(!approved_research_identity(identity), "An altered file size was admitted.");
        identity.file_size = expected_file_size;
        identity.machine = IMAGE_FILE_MACHINE_I386;
        require(!approved_research_identity(identity), "A non-AMD64 identity was admitted.");
        constexpr std::uintptr_t base = 0x140000000;
        require(bounded_slot_address(base, expected_image_size, candidate_rva) == base + candidate_rva, "Expected research slot did not fit.");
        require(bounded_slot_address(base, 16, 8).has_value(), "An exact eight-byte tail slot did not fit.");
        require(!bounded_slot_address(base, 16, 9), "A slot crossing the image end was admitted.");
        require(!bounded_slot_address(base, 7, 0), "An image smaller than the slot was admitted.");
        require(!bounded_slot_address(0, expected_image_size, candidate_rva), "A null module was admitted.");
        require(!bounded_slot_address(std::numeric_limits<std::uintptr_t>::max() - 3, 16, 0), "Module range overflow was admitted.");
        require(!bounded_slot_address(base, 16, std::numeric_limits<std::uint64_t>::max()), "RVA arithmetic overflow was admitted.");
        require(region_read_permitted(MEM_COMMIT, PAGE_READONLY), "Readable committed region rejected.");
        require(!region_read_permitted(MEM_RESERVE, PAGE_READWRITE), "Reserved region reported readable.");
        require(!region_read_permitted(MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD), "Guard region reported readable.");
        require(!region_read_permitted(MEM_COMMIT, PAGE_NOACCESS), "No-access region reported readable.");
        require(!region_read_permitted(MEM_COMMIT, PAGE_EXECUTE), "Execute-only region reported readable.");
        require(region_contains_readable(base, 16, base + 8, 8, MEM_COMMIT, PAGE_READONLY), "Eight-byte object field rejected.");
        require(!region_contains_readable(base, 16, base + 9, 8, MEM_COMMIT, PAGE_READONLY), "Object field crossed its queried region.");
        require(!image_contains(base, 16, base - 1, 8), "Address below image base admitted.");
        require(!image_contains(base, 16, base + 16, 1), "Address at image end admitted.");
        require(!image_contains(std::numeric_limits<std::uintptr_t>::max() - 3, 16,
            std::numeric_limits<std::uintptr_t>::max() - 2, 1), "Overflowing image range admitted.");
        require(!actor_sample_plan(-1) && !actor_sample_plan(65), "Invalid actor count admitted.");
        const auto empty_plan = actor_sample_plan(0);
        require(empty_plan && empty_plan->sampled == 0 && !empty_plan->partial, "Empty actor sample plan rejected.");
        const auto full_plan = actor_sample_plan(6);
        require(full_plan && full_plan->sampled == 6 && !full_plan->partial, "Observed six-slot sample plan rejected.");
        const auto exact_plan = actor_sample_plan(8);
        require(exact_plan && exact_plan->sampled == 8 && !exact_plan->partial, "Exact actor sampling limit changed.");
        const auto partial_plan = actor_sample_plan(64);
        require(partial_plan && partial_plan->sampled == 8 && partial_plan->partial, "Truncated actor collection was not marked partial.");
        require(bounded_actor_field_address(base, actor_primary_offset, 8) == base + actor_primary_offset,
            "Observed primary field range rejected.");
        require(!bounded_actor_field_address(0, 0, 8) && !bounded_actor_field_address(0xffff, 0, 8),
            "Null/low actor address admitted.");
        require(!bounded_actor_field_address(base, 0, 0) && !bounded_actor_field_address(base, 0, 17),
            "Empty/oversized actor field read admitted.");
        require(!bounded_actor_field_address(base, std::numeric_limits<std::uint64_t>::max(), 8),
            "Overflowing actor field offset admitted.");
        require(!bounded_actor_field_address(0x00007fffffffffff, 0, 8) &&
            !bounded_actor_field_address(0xffff800000000000, 0, 8), "Non-user actor range admitted.");
        require(bounded_actor_record_address(base, 7) == base + 7 * actor_record_stride,
            "Last sampled actor record address changed.");
        require(!bounded_actor_record_address(base, 8) &&
            !bounded_actor_record_address(base, std::numeric_limits<std::size_t>::max()), "Unbounded actor index admitted.");
        require(!bounded_actor_record_address(0x00007ffffffffffe, 0), "Actor record crossed canonical user bound.");
        std::array<unsigned char, rtti_locator_size> locator{};
        word(locator, 0, 1);
        word(locator, 12, 0x400);
        word(locator, 20, 0x200);
        require(rtti_descriptor_address(base, 0x1000, base + 0x200, locator) == base + 0x400, "Valid x64 COL metadata rejected.");
        word(locator, 0, 0);
        require(!rtti_descriptor_address(base, 0x1000, base + 0x200, locator), "Unsupported COL signature admitted.");
        word(locator, 0, 1);
        word(locator, 20, 0x201);
        require(!rtti_descriptor_address(base, 0x1000, base + 0x200, locator), "COL self RVA mismatch admitted.");
        word(locator, 20, 0x200);
        word(locator, 12, 0xff0);
        require(!rtti_descriptor_address(base, 0x1000, base + 0x200, locator), "Type descriptor with unbounded name range admitted.");
        word(locator, 12, 0xffffffff);
        require(!rtti_descriptor_address(base, 0x1000, base + 0x200, locator), "Out-of-image type RVA admitted.");
        require(!rtti_descriptor_address(base, 0x1000, base + 0x200, std::span(locator).first(20)), "Truncated COL admitted.");
        std::array<unsigned char, rtti_name_limit> name{};
        const std::string class_name = ".?AVExampleSoftwareType@@";
        std::copy(class_name.begin(), class_name.end(), name.begin());
        require(rtti_vendor_type_name(name) == class_name, "Bounded vendor class name rejected.");
        name[3] = 'U';
        require(rtti_vendor_type_name(name).has_value(), "Bounded vendor struct name rejected.");
        name[0] = 'X';
        require(!rtti_vendor_type_name(name), "Non-vendor name prefix admitted.");
        name[0] = '.';
        name[5] = '\n';
        require(!rtti_vendor_type_name(name), "Control character in type name admitted.");
        name.fill('A');
        name[0] = '.'; name[1] = '?'; name[2] = 'A'; name[3] = 'V';
        require(!rtti_vendor_type_name(name), "Unterminated 128-byte type name admitted.");
        name[4] = 0;
        require(!rtti_vendor_type_name(name), "Empty vendor type name admitted.");
        // The probe inspects its own built EXE as the deliberately wrong target.
        // SHA preflight rejects before any process enumeration or memory access.
        // This is a refusal test, not a simulated confirmation of game bindings.
        const auto probe_path = std::filesystem::absolute(argv[1]);
        require(skcoop::launcher::run_controlled_child(probe_path, {L"--exe", probe_path.wstring()}) == 10,
            "Wrong executable was not refused before process access.");
        require(skcoop::launcher::run_controlled_child(probe_path, {L"--exe", probe_path.wstring(), L"--rtti"}) == 10,
            "RTTI option bypassed exact-build preflight.");
        require(skcoop::launcher::run_controlled_child(probe_path, {L"--exe", probe_path.wstring(), L"--actors"}) == 10,
            "Actor option bypassed exact-build preflight.");
        std::cout << "Probe policy passed: exact build, bounded slot, region flags and wrong-executable refusal. No Sekiro process was queried.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Probe policy failed: " << error.what() << '\n';
        return 1;
    }
}
