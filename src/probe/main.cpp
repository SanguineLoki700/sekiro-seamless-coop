#include "policy.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <array>
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
std::optional<std::string> observe_rtti(HANDLE process, std::uintptr_t module_base, std::uint64_t image_size,
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
    return rtti_vendor_type_name(std::span<const unsigned char>(type).subspan(rtti_type_header_size));
}
int probe(const skcoop::launcher::ExecutableIdentity& identity, bool rtti_requested) {
    using namespace skcoop::probe;
    const auto selected = find_process(identity.path);
    // This is the only handle with memory-read permission. It has no write,
    // operation, thread, debug, process-launch, or termination authority.
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, selected.pid));
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
    if (rtti_requested) {
        const auto type_name = observe_rtti(process.get(), reinterpret_cast<std::uintptr_t>(module.lpBaseOfDll),
            module.SizeOfImage, value, queried, region);
        if (type_name) std::cout << "rtti_type_name: " << *type_name << '\n';
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
            std::cout << "skcoop_probe --exe <path-to-the-approved-Sekiro-executable> [--rtti]\n"
                << "Default: read one eight-byte candidate slot. Optional RTTI: at most five bounded reads total.\n"
                << "No launch, injection, hooks, writes, save access, or elevation.\n";
            return 0;
        }
        if ((argc != 3 && argc != 4) || std::wstring_view(argv[1]) != L"--exe" ||
            (argc == 4 && std::wstring_view(argv[3]) != L"--rtti")) {
            std::cerr << "Usage: skcoop_probe --exe <path> [--rtti]\n";
            return 2;
        }
        skcoop::launcher::ExecutableIdentity identity;
        try { identity = skcoop::launcher::inspect_executable(argv[2]); }
        catch (const std::exception&) { throw ProbeError(10, "Selected executable failed read-only PE/hash preflight; no process queried."); }
        if (!skcoop::probe::approved_research_identity(identity))
            throw ProbeError(10, "Selected executable is not the exact approved research build; no process queried.");
        return probe(identity, argc == 4);
    } catch (const ProbeError& error) {
        std::cerr << "Probe refused: " << error.what() << '\n';
        return error.code;
    } catch (const std::exception&) {
        std::cerr << "Probe failed: local observation unavailable. No write or fallback attempted.\n";
        return 1;
    }
}
