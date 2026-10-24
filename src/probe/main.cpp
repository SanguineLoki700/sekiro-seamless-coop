#include "policy.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
class ProbeError : public std::runtime_error {
public:
    ProbeError(int code, const char* message) : std::runtime_error(message), code(code) {}
    int code;
};
class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { close(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { close(); value_ = std::exchange(other.value_, nullptr); }
        return *this;
    }
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
private:
    void close() { if (*this) CloseHandle(value_); value_ = nullptr; }
    HANDLE value_;
};
bool equal_path(const fs::path& first, const fs::path& second) {
    std::error_code left_error, right_error;
    const auto left = fs::weakly_canonical(first, left_error).wstring();
    const auto right = fs::weakly_canonical(second, right_error).wstring();
    if (left_error || right_error) throw ProbeError(13, "Cannot resolve process image identity; probe stopped.");
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}
fs::path process_path(HANDLE process) {
    std::wstring text(32768, L'\0');
    DWORD count = static_cast<DWORD>(text.size());
    if (!QueryFullProcessImageNameW(process, 0, text.data(), &count))
        throw ProbeError(13, "Cannot query selected process image; no elevation or fallback attempted.");
    text.resize(count);
    return fs::path(text);
}
FILETIME creation_time(HANDLE process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user))
        throw ProbeError(13, "Cannot establish process lifetime identity.");
    return creation;
}
struct SelectedProcess {
    DWORD pid;
    Handle query_handle;
    FILETIME created;
};
SelectedProcess find_process(const fs::path& expected) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) throw ProbeError(13, "Cannot enumerate processes; no elevation attempted.");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) throw ProbeError(13, "Cannot inspect process inventory.");
    std::vector<SelectedProcess> matches;
    const auto wanted_name = expected.filename().wstring();
    do {
        if (CompareStringOrdinal(entry.szExeFile, -1, wanted_name.c_str(), -1, TRUE) != CSTR_EQUAL) continue;
        Handle query(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
        if (!query) {
            // A matching name with inaccessible identity is not silently skipped.
            throw ProbeError(13, "A candidate process cannot be queried; probe stopped without elevation.");
        }
        if (equal_path(process_path(query.get()), expected)) {
            const auto created = creation_time(query.get());
            matches.push_back({entry.th32ProcessID, std::move(query), created});
        }
    } while (Process32NextW(snapshot.get(), &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) throw ProbeError(13, "Process inventory was incomplete.");
    if (matches.empty()) throw ProbeError(11, "No running process exactly matches the selected executable.");
    if (matches.size() != 1) throw ProbeError(12, "Multiple matching processes; selection is ambiguous.");
    return std::move(matches.front());
}
MODULEINFO main_module(HANDLE process, const fs::path& expected) {
    std::array<HMODULE, 4096> modules{};
    DWORD required = 0;
    if (!EnumProcessModulesEx(process, modules.data(), static_cast<DWORD>(sizeof(modules)), &required, LIST_MODULES_64BIT) ||
        required == 0 || required > sizeof(modules) || required % sizeof(HMODULE) != 0)
        throw ProbeError(14, "Cannot establish a complete bounded module inventory.");
    std::size_t matches = 0;
    MODULEINFO selected{};
    for (std::size_t index = 0; index < required / sizeof(HMODULE); ++index) {
        std::wstring name(32768, L'\0');
        const auto length = GetModuleFileNameExW(process, modules[index], name.data(), static_cast<DWORD>(name.size()));
        if (length == 0 || length >= name.size()) throw ProbeError(14, "Module identity changed or is unavailable.");
        name.resize(length);
        if (!equal_path(fs::path(name), expected)) continue;
        if (!GetModuleInformation(process, modules[index], &selected, sizeof(selected)))
            throw ProbeError(14, "Cannot query main module metadata.");
        ++matches;
    }
    if (matches != 1) throw ProbeError(14, "Main module identity is absent or ambiguous.");
    return selected;
}
void boolean(const char* key, bool value) { std::cout << key << ": " << (value ? "true" : "false") << '\n'; }
template<typename Value> bool read_exact(HANDLE process, std::uintptr_t address, Value& output) {
    SIZE_T received = 0;
    return ReadProcessMemory(process, reinterpret_cast<const void*>(address), &output, sizeof(output), &received) != FALSE &&
        received == sizeof(output);
}
struct RttiObservation {
    std::string type_name;
    std::uintptr_t vtable, locator, descriptor;
    std::uint32_t complete_object_offset;
    bool operator==(const RttiObservation&) const = default;
};
std::optional<RttiObservation> observe_rtti(HANDLE process, std::uintptr_t module_base, std::uint64_t image_size,
    std::uint64_t object, bool region_queried, const MEMORY_BASIC_INFORMATION& region) {
    using namespace skcoop::probe;
    if (!region_queried || !region_contains_readable(reinterpret_cast<std::uintptr_t>(region.BaseAddress),
        region.RegionSize, static_cast<std::uintptr_t>(object), 8, region.State, region.Protect)) return std::nullopt;
    std::uintptr_t vtable = 0;
    if (!read_exact(process, static_cast<std::uintptr_t>(object), vtable) || vtable < 8 ||
        !image_contains(module_base, image_size, vtable - 8, 16)) return std::nullopt;
    std::uintptr_t locator_address = 0;
    if (!read_exact(process, vtable - 8, locator_address) ||
        !image_contains(module_base, image_size, locator_address, rtti_locator_size)) return std::nullopt;
    std::array<unsigned char, rtti_locator_size> locator{};
    if (!read_exact(process, locator_address, locator)) return std::nullopt;
    const auto descriptor = rtti_descriptor_address(module_base, image_size, locator_address, locator);
    if (!descriptor) return std::nullopt;
    std::array<unsigned char, rtti_type_header_size + rtti_name_limit> type{};
    if (!read_exact(process, *descriptor, type)) return std::nullopt;
    const auto name = rtti_vendor_type_name(std::span<const unsigned char>(type).subspan(rtti_type_header_size));
    if (!name) return std::nullopt;
    std::uint32_t adjustment{};
    std::memcpy(&adjustment, locator.data() + 4, sizeof(adjustment));
    return RttiObservation{*name, vtable, locator_address, *descriptor, adjustment};
}

// No heap scan: every request is one selected field or one bounded RTTI object.
class ActorReader {
public:
    ActorReader(HANDLE process, std::uintptr_t base, std::uint64_t size) : process_(process), base_(base), size_(size) {}
    template<typename Value> Value heap(std::uintptr_t object, std::uint64_t offset = 0) {
        const auto address = skcoop::probe::bounded_actor_field_address(object, offset, sizeof(Value));
        if (!address) throw ProbeError(16, "Actor field range is invalid; observation stopped.");
        Value value{};
        read(*address, &value, sizeof(value), false);
        return value;
    }
    std::uint64_t record(std::uintptr_t array, std::size_t index) {
        const auto address = skcoop::probe::bounded_actor_record_address(array, index);
        if (!address) throw ProbeError(16, "Actor record range is invalid; observation stopped.");
        return heap<std::uint64_t>(*address);
    }
    template<typename Value> Value module(std::uintptr_t address) {
        if (!skcoop::probe::image_contains(base_, size_, address, sizeof(Value)))
            throw ProbeError(16, "RTTI/slot range is outside the selected image.");
        Value value{};
        read(address, &value, sizeof(value), true);
        return value;
    }
    void require_module_page(std::uintptr_t address, bool executable) const {
        using namespace skcoop::probe;
        MEMORY_BASIC_INFORMATION region{};
        if (!image_contains(base_, size_, address, 1) ||
            VirtualQueryEx(process_, reinterpret_cast<const void*>(address), &region, sizeof(region)) != sizeof(region) ||
            !region_contains_readable(reinterpret_cast<std::uintptr_t>(region.BaseAddress),
                region.RegionSize, address, 1, region.State, region.Protect) ||
            (executable && !region_execute_read_permitted(region.State, region.Protect)))
            throw ProbeError(16, "Selected creator target is outside the image or lacks required page access.");
    }
    RttiObservation type(std::uintptr_t object) {
        using namespace skcoop::probe;
        const auto vtable = heap<std::uintptr_t>(object);
        if (vtable < 8 || !image_contains(base_, size_, vtable - 8, 16))
            throw ProbeError(16, "Actor vtable is not inside the selected image.");
        const auto locator = module<std::uintptr_t>(vtable - 8);
        const auto bytes = module<std::array<unsigned char, rtti_locator_size>>(locator);
        const auto descriptor = rtti_descriptor_address(base_, size_, locator, bytes);
        if (!descriptor) throw ProbeError(16, "Actor RTTI locator is invalid.");
        const auto type_bytes = module<std::array<unsigned char, rtti_type_header_size + rtti_name_limit>>(*descriptor);
        const auto name = rtti_vendor_type_name(std::span<const unsigned char>(type_bytes).subspan(rtti_type_header_size));
        if (!name) throw ProbeError(16, "Actor RTTI name is unavailable; no type inferred.");
        std::uint32_t adjustment{};
        std::memcpy(&adjustment, bytes.data() + 4, sizeof(adjustment));
        return {*name, vtable, locator, *descriptor, adjustment};
    }
    std::size_t heap_bytes() const noexcept { return heap_bytes_; }
    std::size_t module_bytes() const noexcept { return module_bytes_; }
private:
    void read(std::uintptr_t address, void* output, std::size_t bytes, bool in_module) {
        auto& consumed = in_module ? module_bytes_ : heap_bytes_;
        const auto budget = in_module ? std::size_t{4096} : std::size_t{512};
        if (!bytes || bytes > 256 || consumed > budget || bytes > budget - consumed)
            throw ProbeError(16, "Actor observation read budget exceeded.");
        std::size_t at = 0;
        while (at < bytes) {
            MEMORY_BASIC_INFORMATION region{};
            const auto current = address + at;
            if (VirtualQueryEx(process_, reinterpret_cast<const void*>(current), &region, sizeof(region)) != sizeof(region) ||
                !skcoop::probe::region_contains_readable(reinterpret_cast<std::uintptr_t>(region.BaseAddress),
                    region.RegionSize, current, 1, region.State, region.Protect))
                throw ProbeError(16, "Selected actor/RTTI region is not committed and readable.");
            const auto inside = current - reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            const auto chunk = std::min<std::size_t>(bytes - at, region.RegionSize - inside);
            SIZE_T received = 0;
            if (!ReadProcessMemory(process_, reinterpret_cast<const void*>(current),
                static_cast<unsigned char*>(output) + at, chunk, &received) || received != chunk)
                throw ProbeError(16, "Actor observation was incomplete; no partial result accepted.");
            at += chunk;
            consumed += chunk;
        }
    }
    HANDLE process_;
    std::uintptr_t base_;
    std::uint64_t size_;
    std::size_t heap_bytes_{}, module_bytes_{};
};

void observe_actors(HANDLE process, const FILETIME& expected_creation, std::uintptr_t module_base,
    std::uint64_t image_size, std::uintptr_t slot) {
    using namespace skcoop::probe;
    if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT)
        throw ProbeError(16, "Selected actor process is no longer alive.");
    ActorReader reader(process, module_base, image_size);
    const auto manager = reader.module<std::uintptr_t>(slot);
    const auto manager_type = reader.type(manager);
    if (manager_type.type_name != ".?AVWorldChrManImp@NS_SPRJ@@" || manager_type.complete_object_offset != 0)
        throw ProbeError(16, "Selected manager is not the observed WorldChrManImp type.");
    const auto primary = reader.heap<std::uintptr_t>(manager, actor_primary_offset);
    const auto primary_type = reader.type(primary);
    if (primary_type.type_name != ".?AVPlayerIns@NS_SPRJ@@" || primary_type.complete_object_offset != 0)
        throw ProbeError(16, "Primary candidate is not the observed PlayerIns type.");
    const auto header = reader.heap<std::array<unsigned char, 16>>(manager, actor_collection_offset);
    std::int32_t count{};
    std::uintptr_t array{};
    std::memcpy(&count, header.data(), sizeof(count));
    std::memcpy(&array, header.data() + 8, sizeof(array));
    const auto plan = actor_sample_plan(count);
    if (!plan) throw ProbeError(16, "Selected actor collection count is outside the observation limit.");
    struct Entry { std::uintptr_t pointer; std::optional<RttiObservation> type; };
    std::vector<Entry> entries;
    bool primary_present = false;
    for (std::size_t i = 0; i < plan->sampled; ++i) {
        const auto pointer = reader.record(array, i);
        std::optional<RttiObservation> type;
        if (pointer) type = reader.type(pointer);
        if (pointer == primary) primary_present = true;
        entries.push_back({pointer, std::move(type)});
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (reader.record(array, i) != entry.pointer ||
            (entry.type && reader.type(entry.pointer) != *entry.type))
            throw ProbeError(16, "Selected actor entry changed during observation.");
    }
    if (reader.heap<std::array<unsigned char, 16>>(manager, actor_collection_offset) != header ||
        reader.heap<std::uintptr_t>(manager, actor_primary_offset) != primary ||
        reader.module<std::uintptr_t>(slot) != manager || reader.type(manager) != manager_type ||
        reader.type(primary) != primary_type)
        throw ProbeError(16, "Selected actor links or RTTI changed during observation.");
    const auto after = creation_time(process);
    if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT || CompareFileTime(&after, &expected_creation) != 0)
        throw ProbeError(16, "Selected actor process exited or its lifetime identity changed.");

    std::cout << "actor_observation: read-only selected fields\nactor_manager_type: " << manager_type.type_name
        << "\nactor_primary_type: " << primary_type.type_name << "\nactor_collection_count: " << count
        << "\nactor_sampled_slots: " << entries.size() << '\n';
    boolean("actor_sample_partial", plan->partial);
    boolean("actor_primary_present_in_sample", primary_present);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        std::cout << "actor_slot_" << i << "_type: " << (entry.type ? entry.type->type_name : "null") << '\n';
        std::cout << "actor_slot_" << i << "_same_as_primary: " << (entry.pointer == primary ? "true" : "false") << '\n';
    }
    std::cout << "actor_heap_bytes_read: " << reader.heap_bytes() << "\nactor_module_bytes_read: " << reader.module_bytes() << '\n';
    boolean("actor_selected_links_unchanged", true);
    std::cout << "actor_limitation: selected slots only; count includes null slots, not player count. Partial means only the prefix was sampled."
        " No atomic snapshot, allocation lifetime, complete layout, creation/control API or multiplayer is established.\n";
}

void observe_creator(HANDLE process, const FILETIME& expected_creation, std::uintptr_t module_base,
    std::uint64_t image_size, std::uintptr_t slot) {
    using namespace skcoop::probe;
    if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT)
        throw ProbeError(16, "Selected creator process is no longer alive.");
    ActorReader reader(process, module_base, image_size);
    const auto image_address = [&](std::uint64_t rva) {
        const auto address = bounded_slot_address(module_base, image_size, rva);
        if (!address) throw ProbeError(16, "Selected creator RVA is outside the image.");
        return *address;
    };
    const auto expected_vtable = image_address(creator_vtable_rva);
    const auto setter_slot = image_address(creator_vtable_rva + creator_setter_slot);
    const auto consumer_slot = image_address(creator_vtable_rva + creator_consumer_slot);
    const auto manager = reader.module<std::uintptr_t>(slot);
    const auto manager_type = reader.type(manager);
    if (manager_type.type_name != ".?AVWorldChrManImp@NS_SPRJ@@" || manager_type.complete_object_offset != 0)
        throw ProbeError(16, "Selected manager is not the observed WorldChrManImp type.");
    const auto creator = reader.heap<std::uintptr_t>(manager, creator_manager_offset);
    const auto creator_type = reader.type(creator);
    if (creator_type.type_name != ".?AVCSDebugChrCreator@NS_SPRJ@@" ||
        creator_type.complete_object_offset != 0 || creator_type.vtable != expected_vtable)
        throw ProbeError(16, "Selected creator type, object adjustment or vtable differs from the observed target.");
    const auto setter = reader.module<std::uintptr_t>(setter_slot);
    const auto consumer = reader.module<std::uintptr_t>(consumer_slot);
    if (setter != image_address(creator_setter_rva) || consumer != image_address(creator_consumer_rva))
        throw ProbeError(16, "Selected creator virtual targets differ from the observed target.");
    reader.require_module_page(setter, true);
    reader.require_module_page(consumer, true);
    const auto table = reader.heap<std::uintptr_t>(creator, creator_table_offset);
    // The retained consumer reads DWORDs at +0x40/+0x44. Values are observed
    // without assigning action semantics or invoking that consumer/setter.
    const auto states = reader.heap<std::array<std::uint32_t, 2>>(creator, creator_state_offset);
    if (!bounded_creator_descriptor_address(table, creator_descriptor_prefix_count - 1))
        throw ProbeError(16, "Selected creator table prefix crosses the bounded user range.");
    const bool table_in_module = creator_table_in_image(module_base, image_size, table);
    const auto table_last_byte = table + creator_table_prefix_bytes - 1;
    if (!table_in_module && (image_contains(module_base, image_size, table, 1) ||
        image_contains(module_base, image_size, table_last_byte, 1)))
        throw ProbeError(16, "Selected creator table prefix partially overlaps the image; classification refused.");
    using Descriptor = std::array<std::uintptr_t, 2>; // callback, label; no string read.
    using Prefix = std::array<Descriptor, creator_descriptor_prefix_count>;
    static_assert(sizeof(Prefix) == creator_table_prefix_bytes);
    const auto read_prefix = [&]() {
        if (table_in_module) return reader.module<Prefix>(table);
        Prefix prefix{};
        for (std::size_t i = 0; i < prefix.size(); ++i) {
            const auto address = bounded_creator_descriptor_address(table, i);
            if (!address) throw ProbeError(16, "Selected creator descriptor is out of bounds.");
            prefix[i] = reader.heap<Descriptor>(*address);
        }
        return prefix;
    };
    const auto validate_targets = [&](const Prefix& prefix) {
        for (const auto& descriptor : prefix) {
            if (descriptor[0]) reader.require_module_page(descriptor[0], true);
            if (descriptor[1]) reader.require_module_page(descriptor[1], false);
        }
    };
    const auto descriptors = read_prefix();
    validate_targets(descriptors);
    if (reader.heap<std::uintptr_t>(creator, creator_table_offset) != table ||
        reader.heap<std::array<std::uint32_t, 2>>(creator, creator_state_offset) != states ||
        read_prefix() != descriptors || reader.module<std::uintptr_t>(setter_slot) != setter ||
        reader.module<std::uintptr_t>(consumer_slot) != consumer ||
        reader.heap<std::uintptr_t>(manager, creator_manager_offset) != creator ||
        reader.module<std::uintptr_t>(slot) != manager || reader.type(manager) != manager_type ||
        reader.type(creator) != creator_type)
        throw ProbeError(16, "Selected creator links, RTTI, states or table changed; retry manually while idle.");
    validate_targets(descriptors);
    reader.require_module_page(setter, true);
    reader.require_module_page(consumer, true);
    const auto after = creation_time(process);
    if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT || CompareFileTime(&after, &expected_creation) != 0)
        throw ProbeError(16, "Selected creator process exited or its lifetime identity changed.");

    const auto rva = [&](const std::string& key, std::uintptr_t address) {
        std::cout << key << ": ";
        if (address) std::cout << "0x" << std::hex << address - module_base << std::dec;
        else std::cout << "null";
        std::cout << '\n';
    };
    std::cout << "creator_observation: read-only selected fields\ncreator_manager_type: " << manager_type.type_name
        << "\ncreator_type: " << creator_type.type_name << "\ncreator_state_value: " << states[0]
        << "\ncreator_request_value: " << states[1] << "\ncreator_table_storage: " << (table_in_module ? "module" : "heap")
        << "\ncreator_descriptor_prefix_sampled: " << descriptors.size() << "\ncreator_total_descriptor_count: unknown\n";
    if (table_in_module) rva("creator_table_rva", table);
    rva("creator_vtable_rva", creator_type.vtable);
    rva("creator_virtual_0x20_rva", setter);
    rva("creator_virtual_0x48_rva", consumer);
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        rva("creator_descriptor_" + std::to_string(i) + "_callback_rva", descriptors[i][0]);
        rva("creator_descriptor_" + std::to_string(i) + "_label_rva", descriptors[i][1]);
    }
    std::cout << "creator_heap_bytes_read: " << reader.heap_bytes()
        << "\ncreator_module_bytes_read: " << reader.module_bytes() << '\n';
    boolean("creator_selected_links_unchanged", true);
    std::cout << "creator_limitation: four descriptors are only a prefix, not a total state count. Heap means outside the selected image;"
        " allocation ownership is unknown. No label strings were read. State/request semantics, atomic snapshot, allocation lifetime,"
        " complete layout, native call contract, actor creation/control and multiplayer remain unverified.\n";
}

int probe(const skcoop::launcher::ExecutableIdentity& identity, bool rtti_requested, bool actors_requested,
    bool creator_requested) {
    using namespace skcoop::probe;
    const auto selected = find_process(identity.path);
    // This is the only handle with memory-read permission. It has no write,
    // operation, thread, debug, process-launch, or termination authority.
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
        (actors_requested || creator_requested ? SYNCHRONIZE : 0), FALSE, selected.pid));
    if (!process) throw ProbeError(13, "Read-only process access denied; no elevation or fallback attempted.");
    const auto now = creation_time(process.get());
    if (CompareFileTime(&now, &selected.created) != 0 || !equal_path(process_path(process.get()), identity.path))
        throw ProbeError(13, "Process identity changed before observation.");
    USHORT machine = 0, native_machine = 0;
    if (!IsWow64Process2(process.get(), &machine, &native_machine) || machine != IMAGE_FILE_MACHINE_UNKNOWN ||
        native_machine != IMAGE_FILE_MACHINE_AMD64)
        throw ProbeError(14, "Only a native AMD64 process is admitted.");
    const auto module = main_module(process.get(), identity.path);
    if (module.SizeOfImage != expected_image_size) throw ProbeError(14, "Main module image size differs from the research build.");
    const auto slot = bounded_slot_address(reinterpret_cast<std::uintptr_t>(module.lpBaseOfDll), module.SizeOfImage, candidate_rva);
    if (!slot) throw ProbeError(14, "Research candidate does not fit inside the selected module.");
    if (actors_requested || creator_requested) {
        std::cout << "research_probe: read-only\nprocess_id: " << selected.pid << '\n';
        boolean("exact_disk_hash_match", true);
        boolean("native_amd64_process", true);
        boolean("main_module_identity_match", true);
        boolean("module_image_size_match", true);
        boolean("native_hooks_implemented", false);
        boolean("save_isolation_implemented", false);
        boolean("multiplayer_implemented", false);
        std::cout << "capability_flags_scope: this read-only probe only, not the injected runtime.\n";
        if (creator_requested)
            observe_creator(process.get(), selected.created, reinterpret_cast<std::uintptr_t>(module.lpBaseOfDll),
                module.SizeOfImage, *slot);
        else
            observe_actors(process.get(), selected.created, reinterpret_cast<std::uintptr_t>(module.lpBaseOfDll),
                module.SizeOfImage, *slot);
        return 0;
    }

    std::uint64_t value = 0;
    SIZE_T received = 0;
    // Default mode performs only this eight-byte read. RTTI is opt-in below.
    const bool read = ReadProcessMemory(process.get(), reinterpret_cast<const void*>(*slot),
        &value, static_cast<SIZE_T>(candidate_slot_size), &received) != FALSE && received == candidate_slot_size;
    MEMORY_BASIC_INFORMATION region{};
    const bool nonzero = read && value != 0;
    const bool queried = nonzero && VirtualQueryEx(process.get(), reinterpret_cast<const void*>(value),
        &region, sizeof(region)) == sizeof(region);
    std::cout << "research_probe: read-only\nprocess_id: " << selected.pid << '\n';
    boolean("exact_disk_hash_match", true);
    boolean("native_amd64_process", true);
    boolean("main_module_identity_match", true);
    boolean("module_image_size_match", true);
    boolean("candidate_slot_in_bounds", true);
    boolean("slot_read_complete", read);
    boolean("candidate_nonzero", nonzero);
    boolean("candidate_region_query_ok", queried);
    boolean("candidate_region_committed", queried && region.State == MEM_COMMIT);
    boolean("candidate_region_read_permitted", queried && region_read_permitted(region.State, region.Protect));
    boolean("native_hooks_implemented", false);
    boolean("save_isolation_implemented", false);
    boolean("multiplayer_implemented", false);
    std::cout << "capability_flags_scope: this read-only probe only, not the injected runtime.\n";
    if (rtti_requested) {
        const auto type_name = observe_rtti(process.get(), reinterpret_cast<std::uintptr_t>(module.lpBaseOfDll),
            module.SizeOfImage, value, queried, region);
        if (type_name) std::cout << "rtti_type_name: " << type_name->type_name << '\n';
        else boolean("rtti_unavailable", true);
        std::cout << "rtti_limitation: bounded software type metadata only; no object layout, lifetime, actor, or hook was validated.\n";
    }
    std::cout << "limitation: disk identity is not a hash of loaded memory; no pointer chain or remote actor was validated.\n";
    return read ? 0 : 15;
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2 && std::wstring_view(argv[1]) == L"--help") {
            std::cout << "skcoop_probe --exe <path-to-the-approved-Sekiro-executable> [--rtti | --actors | --creator]\n"
                << "Default: read one eight-byte candidate slot. Optional RTTI: at most five bounded reads total.\n"
                << "--actors: selected manager/player/collection fields; max 8 slots, 512 heap bytes and 4096 module bytes.\n"
                << "--creator: selected creator state/request and 4 descriptor pairs; 512 heap bytes and 4096 module bytes.\n"
                << "No launch, injection, hooks, writes, save access, or elevation.\n";
            return 0;
        }
        if ((argc != 3 && argc != 4) || std::wstring_view(argv[1]) != L"--exe" ||
            (argc == 4 && std::wstring_view(argv[3]) != L"--rtti" && std::wstring_view(argv[3]) != L"--actors" &&
                std::wstring_view(argv[3]) != L"--creator")) {
            std::cerr << "Usage: skcoop_probe --exe <path> [--rtti | --actors | --creator]\n";
            return 2;
        }
        skcoop::launcher::ExecutableIdentity identity;
        try { identity = skcoop::launcher::inspect_executable(argv[2]); }
        catch (const std::exception&) { throw ProbeError(10, "Selected executable failed read-only PE/hash preflight; no process queried."); }
        if (!skcoop::probe::approved_research_identity(identity))
            throw ProbeError(10, "Selected executable is not the exact approved research build; no process queried.");
        const bool actors = argc == 4 && std::wstring_view(argv[3]) == L"--actors";
        const bool creator = argc == 4 && std::wstring_view(argv[3]) == L"--creator";
        return probe(identity, argc == 4 && !actors && !creator, actors, creator);
    } catch (const ProbeError& error) {
        std::cerr << "Probe refused: " << error.what() << '\n';
        return error.code;
    } catch (const std::exception&) {
        std::cerr << "Probe failed: local observation unavailable. No write or fallback attempted.\n";
        return 1;
    }
}
