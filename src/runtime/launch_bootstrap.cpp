#include "launch_bootstrap.h"
#include "actor_native.h"
#include "skcoop/launch_protocol.h"
#include "../saves/native_io_hooks.hpp"
#include <detours.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace {
using namespace skcoop::launch;
using namespace skcoop::saves;

Payload config{};
SharedStatus* shared{};
// The installed hook state copies its binding. Both that state and this store
// deliberately survive until process termination, including all native handles.
CoOpSaveStore* controller{};
bool active{};

constexpr char expected_hash[] =
    "637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856";
constexpr DWORD expected_image_size = 0x42d2000;

template<std::size_t N>
bool bounded_text(const wchar_t (&value)[N]) noexcept {
    const auto end = std::find(std::begin(value), std::end(value), L'\0');
    return end != std::begin(value) && end != std::end(value) &&
        std::all_of(end, std::end(value), [](wchar_t c) { return c == L'\0'; });
}

bool same_path(const wchar_t* left, const wchar_t* right) {
    std::wstring a(left), b(right);
    std::replace(a.begin(), a.end(), L'/', L'\\');
    std::replace(b.begin(), b.end(), L'/', L'\\');
    return a.size() == b.size() && CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
        b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool game_basename(const wchar_t* path) noexcept {
    const wchar_t* name = path;
    for (const wchar_t* cursor = path; *cursor; ++cursor)
        if (*cursor == L'\\' || *cursor == L'/') name = cursor + 1;
    constexpr wchar_t expected[] = L"sekiro.exe";
    return CompareStringOrdinal(name, -1, expected, -1, TRUE) == CSTR_EQUAL;
}

bool current_game_basename() noexcept {
    wchar_t path[32768]{};
    const DWORD count = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    // Failure to establish a diagnostic process's identity is also a refusal.
    return !count || count >= std::size(path) || game_basename(path);
}

ExecutableSha256 expected_digest() noexcept {
    ExecutableSha256 digest{};
    const auto digit = [](char c) { return static_cast<unsigned char>(c <= '9' ? c - '0' : c - 'a' + 10); };
    for (std::size_t i = 0; i < digest.size(); ++i)
        digest[i] = static_cast<unsigned char>((digit(expected_hash[2 * i]) << 4) | digit(expected_hash[2 * i + 1]));
    return digest;
}

bool identity() {
    if (!bounded_text(config.exe_path) || !game_basename(config.exe_path)) return false;
    wchar_t actual[32768]{};
    const DWORD count = GetModuleFileNameW(nullptr, actual, static_cast<DWORD>(std::size(actual)));
    if (!count || count >= std::size(actual) || !same_path(actual, config.exe_path)) return false;
    const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 64 || dos->e_lfanew > 4096) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        (nt->FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) == 0 ||
        (nt->FileHeader.Characteristics & IMAGE_FILE_DLL) != 0 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfImage != expected_image_size) return false;
    // The parent independently hashes and pins the executable before creating
    // the child. Do not lazy-load a hashing provider under the DLL loader lock.
    const auto digest = expected_digest();
    return std::memcmp(config.executable_sha256, digest.data(), digest.size()) == 0;
}

LONG load(const volatile LONG& value) noexcept {
    return InterlockedCompareExchange(const_cast<volatile LONG*>(&value), 0, 0);
}

bool valid_initial_status() noexcept {
    if (shared->struct_size != sizeof(*shared) || shared->version != protocol_version ||
        shared->host_pid != config.host_pid || shared->child_pid != config.child_pid ||
        std::memcmp(shared->nonce, config.nonce, sizeof(config.nonce)) != 0 ||
        load(shared->phase) != static_cast<LONG>(Phase::payload_prepared) || load(shared->fatal_error) != 0 ||
        load(shared->actor_phase) || load(shared->actor_error) ||
        load(shared->actor_motion_paused) || load(shared->actor_metrics_padding))
        return false;
    for (const auto value : shared->reserved) if (value != 0) return false;
    const volatile LONG64* const counters[]{
        &shared->hooks_mask, &shared->routed_opens, &shared->routed_metadata, &shared->routed_copies,
        &shared->routed_deletes, &shared->denied_operations, &shared->primary_write_opens,
        &shared->backup_write_opens, &shared->unsupported_operations, &shared->successful_primary_reads,
        &shared->successful_backup_reads, &shared->process_detach_count, &shared->native_redirected,
        &shared->native_denied, &shared->native_passthrough, &shared->native_internal_bypass,
        &shared->native_appdata_redirects, &shared->native_directory_queries, &shared->native_copy_failures,
        &shared->native_destination_may_be_modified, &shared->actor_creation_calls,
        &shared->actor_ticks, &shared->actor_pose_writes, &shared->actor_creation_attempts,
        &shared->actor_world_callbacks, &shared->actor_callback_total_us,
        &shared->actor_callback_max_us, &shared->actor_factory_us, &shared->actor_max_frame_interval_us};
    for (const auto* value : counters)
        if (InterlockedCompareExchange64(const_cast<volatile LONG64*>(value), 0, 0) != 0) return false;
    return true;
}

void fail(DWORD error) noexcept {
    if (!error) error = ERROR_ACCESS_DENIED;
    if (shared) {
        InterlockedExchange(&shared->fatal_error, static_cast<LONG>(error));
        InterlockedExchange(&shared->phase, static_cast<LONG>(Phase::failed));
        SetEvent(config.ready_event);
    }
}

bool initialize(const void* payload, DWORD bytes) {
    if (bytes != sizeof(config)) return false;
    std::memcpy(&config, payload, sizeof(config));
    if (config.struct_size != sizeof(config) || config.version != protocol_version ||
        config.child_pid != GetCurrentProcessId() || !config.host_pid || config.host_pid == config.child_pid ||
        !bounded_text(config.exe_path) || !bounded_text(config.solo_roaming) ||
        !bounded_text(config.coop_roaming) || !bounded_text(config.account) ||
        !config.mapping_handle || config.mapping_handle == INVALID_HANDLE_VALUE ||
        !config.ready_event || config.ready_event == INVALID_HANDLE_VALUE ||
        config.mapping_handle == config.ready_event) return false;
    if (config.actor_padding || (config.actor_test != ActorTest::off && config.actor_test != ActorTest::follow)) return false;
    if (std::all_of(std::begin(config.nonce), std::end(config.nonce), [](std::uint8_t c) { return c == 0; }))
        return false;
    for (const auto value : config.reserved) if (value != 0) return false;
    shared = static_cast<SharedStatus*>(MapViewOfFile(config.mapping_handle,
        FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SharedStatus)));
    if (!shared) return false;
    if (!valid_initial_status()) {
        UnmapViewOfFile(shared);
        shared = nullptr;
        return false;
    }
    InterlockedExchange(&shared->phase, static_cast<LONG>(Phase::bootstrapping));
    if (!DetourRestoreAfterWith()) { fail(ERROR_DLL_INIT_FAILED); return false; }
    if (!identity()) { fail(ERROR_INVALID_IMAGE_HASH); return false; }

    NativeIoBinding binding;
    binding.policy.appdata_root = config.solo_roaming;
    binding.policy.coop_profile_root = config.coop_roaming;
    binding.policy.extension = L"co2";
    binding.policy.destination_mode = DestinationMode::separate_profile;
    binding.policy.expected_executable_sha256 = expected_digest();
    binding.policy.selected_game_process_id = GetCurrentProcessId();
    binding.process = {true, true, false, GetCurrentProcessId(), expected_digest()};
    binding.account = config.account;
    const auto decision = decide_native_save_path(std::wstring(config.solo_roaming) + L"\\Sekiro\\" +
        binding.account + L"\\S0000.sl2", binding.policy, binding.process, binding.account);
    if (decision.action != NativePathAction::redirect) { fail(ERROR_INVALID_PARAMETER); return false; }
    const auto slash = decision.destination.find_last_of(L'\\');
    auto opened = CoOpSaveStore::open({decision.destination.substr(0, slash),
        std::wstring(config.solo_roaming) + L"\\Sekiro", L"co2"});
    if (!opened) { fail(opened.status.system_error); return false; }
    for (const auto kind : {SaveFileKind::primary, SaveFileKind::backup}) {
        const auto existing = opened.store->query_native(kind);
        if (!existing && existing.status.error != SaveStoreError::missing_file) {
            fail(existing.status.system_error);
            return false;
        }
    }
    binding.store = opened.store.get();
    // Callback addresses and the bound controller must remain valid for every
    // application thread/handle until termination; never permit live unload.
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&skcoop::runtime::launch_bootstrap_attach), &pinned)) {
        fail(GetLastError());
        return false;
    }
    const auto installed = install_native_io_hooks(binding);
    if (!installed || !installed.installed) { fail(installed.system_error); return false; }
    controller = opened.store.release();
    active = true;
    // This bit means only that the native save namespace hook transaction
    // committed. It does not claim gameplay hooks, actors, or multiplayer.
    InterlockedExchange64(&shared->hooks_mask, 1);
    InterlockedExchange(&shared->phase, static_cast<LONG>(Phase::ready));
    if (!SetEvent(config.ready_event)) {
        const DWORD error = GetLastError();
        fail(error);
        // Application TLS/startup has not run. Revert before a failed DLL load
        // could unload our callback addresses; inability to detach is fatal.
        const auto removed = uninstall_native_io_hooks();
        if (!removed) TerminateProcess(GetCurrentProcess(), ERROR_DLL_INIT_FAILED);
        else { delete controller; controller = nullptr; active = false; }
        return false;
    }
    if (config.actor_test == ActorTest::follow && !skcoop::actors::start_native_demo(shared)) {
        // Save protection remains valid if the optional actor worker cannot start.
        InterlockedExchange(&shared->actor_error,1);
        InterlockedExchange(&shared->actor_phase,6);
    }
    return true;
}

void snapshot() noexcept {
    if (!shared || !controller || !active) return;
    const auto counters = native_io_hook_status().counters;
    InterlockedExchange64(&shared->native_redirected, static_cast<LONG64>(counters.redirected));
    InterlockedExchange64(&shared->native_denied, static_cast<LONG64>(counters.denied));
    InterlockedExchange64(&shared->native_passthrough, static_cast<LONG64>(counters.passthrough));
    InterlockedExchange64(&shared->native_internal_bypass, static_cast<LONG64>(counters.internal_bypass));
    InterlockedExchange64(&shared->native_appdata_redirects, static_cast<LONG64>(counters.appdata_redirects));
    InterlockedExchange64(&shared->native_directory_queries, static_cast<LONG64>(counters.directory_queries));
    InterlockedExchange64(&shared->native_copy_failures, static_cast<LONG64>(counters.copy_failures));
    InterlockedExchange64(&shared->native_destination_may_be_modified,
        static_cast<LONG64>(counters.destination_may_be_modified));
    InterlockedIncrement64(&shared->process_detach_count);
    if (load(shared->phase) == static_cast<LONG>(Phase::ready))
        InterlockedExchange(&shared->phase, static_cast<LONG>(Phase::shutdown));
}
} // namespace

namespace skcoop::runtime {
bool launch_bootstrap_attach() noexcept {
    if (DetourIsHelperProcess()) return true;
    try {
        DWORD bytes{};
        const void* payload = DetourFindPayloadEx(payload_guid, &bytes);
        if (!payload) return !current_game_basename();
        return initialize(payload, bytes);
    } catch (...) { fail(ERROR_INVALID_DATA); return false; }
}

void launch_bootstrap_detach() noexcept { snapshot(); }
bool launch_bootstrap_active() noexcept { return active; }
} // namespace skcoop::runtime

// DetourCreateProcessWithDllEx requires an exported ordinal 1. The marker has
// no callable game behavior and is distinct from the runtime's diagnostic ABI.
extern "C" void skcoop_runtime_launch_marker() {}
