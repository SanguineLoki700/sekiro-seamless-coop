#define SKCOOP_BOOTSTRAP_BUILD
#include "skcoop/save_bootstrap.h"
#include "../saves/native_io_hooks.hpp"
#include <detours.h>
#include <algorithm>
#include <climits>
#include <cstring>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<skcoop_bootstrap_payload>);
static_assert(sizeof(skcoop_bootstrap_payload) < 64u * 1024u);

namespace {
skcoop_bootstrap_payload config{};
skcoop_bootstrap_status* status = nullptr;
decltype(&CreateFileW) original_create_file = CreateFileW;
bool installed = false;
skcoop::saves::CoOpSaveStore* native_store = nullptr;

WCHAR lower(WCHAR c) noexcept { return c >= L'A' && c <= L'Z' ? static_cast<WCHAR>(c + (L'a' - L'A')) : c; }
bool equal_text(const WCHAR* a, const WCHAR* b) noexcept {
    for (DWORD i = 0; i < 32768; ++i) {
        if (lower(a[i]) != lower(b[i])) return false;
        if (a[i] == L'\0') return true;
    }
    return false;
}
bool valid_root(const WCHAR* text) noexcept {
    if (!((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z')) ||
        text[1] != L':' || text[2] != L'\\') return false;
    for (DWORD i = 3; i < SKCOOP_BOOTSTRAP_ROOT_CHARS; ++i) {
        if (text[i] == L'\0') return i > 3 && text[i - 1] != L'\\';
        if (text[i] == L'/' || text[i] == L':' || text[i] < 32) return false;
        if (text[i] == L'.' && text[i - 1] == L'\\') {
            if (i + 1 >= SKCOOP_BOOTSTRAP_ROOT_CHARS || text[i + 1] == L'\\' ||
                text[i + 1] == L'.' || text[i + 1] == L'\0') return false;
        }
    }
    return false;
}
bool valid_account() noexcept {
    for (DWORD i = 0; i < SKCOOP_BOOTSTRAP_ACCOUNT_CHARS; ++i) {
        if (config.account[i] == L'\0') return i >= 1 && i <= 20;
        if (config.account[i] < L'0' || config.account[i] > L'9') return false;
    }
    return false;
}
bool fixture_identity() noexcept {
    WCHAR path[32768]{};
    const DWORD count = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    if (count == 0 || count >= sizeof(path) / sizeof(path[0])) return false;
    const WCHAR* name = path;
    for (DWORD i = 0; i < count; ++i) if (path[i] == L'\\' || path[i] == L'/') name = path + i + 1;
    if (!equal_text(name, SKCOOP_BOOTSTRAP_FIXTURE_NAME)) return false;
    const auto symbol = GetProcAddress(GetModuleHandleW(nullptr), "skcoop_bootstrap_fixture_marker");
    if (!symbol) return false;
    uint64_t marker = 0;
    std::memcpy(&marker, reinterpret_cast<const void*>(symbol), sizeof(marker));
    return marker == SKCOOP_BOOTSTRAP_FIXTURE_MAGIC;
}
bool candidate(const WCHAR* file) noexcept {
    if (!file) return false;
    const WCHAR* name = file;
    for (DWORD i = 0; i < 32768; ++i) {
        if (file[i] == L'\\' || file[i] == L'/') name = file + i + 1;
        if (file[i] == L'\0') return equal_text(name, L"S0000.sl2") || equal_text(name, L"S0000.sl2.bak");
    }
    return false;
}
HANDLE WINAPI guarded_create_file(LPCWSTR file, DWORD access, DWORD sharing,
    LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD attributes, HANDLE template_file) {
    if (candidate(file)) {
        if (status) InterlockedIncrement(&status->denied_opens);
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    return original_create_file(file, access, sharing, security, disposition, attributes, template_file);
}
void report_failure(DWORD error) noexcept {
    if (status) {
        InterlockedExchange(&status->system_error, static_cast<LONG>(error));
        InterlockedExchange(&status->phase, SKCOOP_BOOTSTRAP_FAILED);
        SetEvent(config.ready_event);
    }
}
void snapshot_native_counters() noexcept {
    if (!native_store || !status) return;
    const auto counters = skcoop::saves::native_io_hook_status().counters;
    const auto bounded = [](uint64_t value) { return static_cast<LONG>(std::min<uint64_t>(value, LONG_MAX)); };
    InterlockedExchange(&status->native_redirected, bounded(counters.redirected));
    InterlockedExchange(&status->native_denied, bounded(counters.denied));
    InterlockedExchange(&status->native_passthrough, bounded(counters.passthrough));
    InterlockedExchange(&status->native_internal_bypass, bounded(counters.internal_bypass));
    InterlockedExchange(&status->native_appdata_redirects, bounded(counters.appdata_redirects));
    InterlockedExchange(&status->native_directory_queries, bounded(counters.directory_queries));
    InterlockedExchange(&status->native_copy_failures, bounded(counters.copy_failures));
    InterlockedExchange(&status->native_destination_may_be_modified, bounded(counters.destination_may_be_modified));
}
bool install_native_profile() {
    using namespace skcoop::saves;
    const std::wstring account(config.account);
    const std::wstring coop_directory = std::wstring(config.coop_root) + L"\\Sekiro\\" + account;
    const std::wstring protected_directory = std::wstring(config.protected_root) + L"\\Sekiro";
    auto opened = CoOpSaveStore::open({coop_directory, protected_directory, L"co2"});
    if (!opened) {
        report_failure(opened.status.system_error ? opened.status.system_error : ERROR_ACCESS_DENIED);
        return false;
    }
    for (const auto kind : {SaveFileKind::primary, SaveFileKind::backup}) {
        const auto existing = opened.store->query_native(kind);
        if (!existing && existing.status.error != SaveStoreError::missing_file) {
            report_failure(existing.status.system_error ? existing.status.system_error : ERROR_ACCESS_DENIED);
            return false;
        }
    }
    SavePolicyConfig policy;
    policy.appdata_root = config.protected_root;
    policy.coop_profile_root = config.coop_root;
    policy.extension = L"co2";
    policy.destination_mode = DestinationMode::separate_profile;
    policy.selected_game_process_id = config.process_id;
    std::copy(std::begin(config.executable_sha256), std::end(config.executable_sha256), policy.expected_executable_sha256.begin());
    SaveProcessContext context;
    context.cooperative_profile_opt_in = true;
    context.is_selected_game_process = true;
    context.diagnostic_only = false;
    context.process_id = config.process_id;
    context.observed_executable_sha256 = policy.expected_executable_sha256;
    const auto hooks = install_native_io_hooks({std::move(policy), context, account, opened.store.get()});
    if (!hooks || !hooks.installed) {
        report_failure(hooks.system_error ? hooks.system_error : ERROR_INVALID_DATA);
        return false;
    }
    native_store = opened.store.release(); // Process lifetime; no live unload supported.
    installed = true;
    InterlockedExchange(&status->installed_hooks, 1);
    return true;
}
bool attach_bootstrap() {
    DWORD payload_size = 0;
    const auto* payload = DetourFindPayloadEx(SKCOOP_BOOTSTRAP_PAYLOAD_GUID, &payload_size);
    if (!payload || payload_size != sizeof(config)) return false;
    std::memcpy(&config, payload, sizeof(config));
    if (config.struct_size != sizeof(config) || config.version != SKCOOP_BOOTSTRAP_VERSION ||
        config.process_id != GetCurrentProcessId() ||
        (config.profile_mode != SKCOOP_BOOTSTRAP_DENY_ONLY && config.profile_mode != SKCOOP_BOOTSTRAP_NATIVE_PROFILE) ||
        (config.test_flags & ~SKCOOP_BOOTSTRAP_TEST_ABORT_TRANSACTION) != 0 ||
        !valid_root(config.protected_root) || !valid_root(config.coop_root) ||
        equal_text(config.protected_root, config.coop_root) || !valid_account() ||
        !config.status_mapping || config.status_mapping == INVALID_HANDLE_VALUE ||
        !config.ready_event || config.ready_event == INVALID_HANDLE_VALUE) return false;
    status = static_cast<skcoop_bootstrap_status*>(MapViewOfFile(config.status_mapping,
        FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(skcoop_bootstrap_status)));
    if (!status) return false;
    if (status->struct_size != sizeof(*status) || status->version != SKCOOP_BOOTSTRAP_VERSION ||
        status->process_id != config.process_id || status->profile_mode != config.profile_mode ||
        std::memcmp(status->nonce, config.nonce, sizeof(config.nonce)) != 0) {
        UnmapViewOfFile(status);
        status = nullptr;
        return false;
    }
    InterlockedExchange(&status->phase, SKCOOP_BOOTSTRAP_ATTACHING);
    if (config.profile_mode == SKCOOP_BOOTSTRAP_NATIVE_PROFILE) {
        if (config.test_flags != 0) { report_failure(ERROR_INVALID_PARAMETER); return false; }
        if (!install_native_profile()) return false;
    } else {
    LONG result = DetourTransactionBegin();
    if (result != NO_ERROR) { report_failure(static_cast<DWORD>(result)); return false; }
    result = DetourUpdateThread(GetCurrentThread());
    if (result == NO_ERROR)
        result = DetourAttach(reinterpret_cast<PVOID*>(&original_create_file), reinterpret_cast<PVOID>(guarded_create_file));
    if (result == NO_ERROR && (config.test_flags & SKCOOP_BOOTSTRAP_TEST_ABORT_TRANSACTION))
        result = ERROR_CANCELLED; // Deliberate failure after the first staged attachment.
    if (result != NO_ERROR) {
        const LONG abort_result = DetourTransactionAbort();
        InterlockedExchange(&status->transaction_aborted, abort_result == NO_ERROR ? 1 : -1);
        report_failure(static_cast<DWORD>(result));
        return false;
    }
    result = DetourTransactionCommit();
    if (result != NO_ERROR) { report_failure(static_cast<DWORD>(result)); return false; }
    installed = true;
    InterlockedExchange(&status->installed_hooks, 1);
    }
    InterlockedExchange(&status->ready_sequence, InterlockedIncrement(&status->sequence));
    InterlockedExchange(&status->phase, SKCOOP_BOOTSTRAP_READY);
    if (!SetEvent(config.ready_event)) {
        const DWORD error = GetLastError();
        if (native_store) {
            const auto hooks = skcoop::saves::uninstall_native_io_hooks();
            if (hooks) { delete native_store; native_store = nullptr; }
        } else {
        if (DetourTransactionBegin() == NO_ERROR) {
            DetourUpdateThread(GetCurrentThread());
            DetourDetach(reinterpret_cast<PVOID*>(&original_create_file), reinterpret_cast<PVOID>(guarded_create_file));
            DetourTransactionCommit();
        }
        }
        installed = false;
        report_failure(error);
        return false;
    }
    return true;
}
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        if (DetourIsHelperProcess()) return TRUE;
        if (!fixture_identity() || !DetourRestoreAfterWith()) return FALSE;
        try { return attach_bootstrap() ? TRUE : FALSE; }
        catch (...) { report_failure(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    }
    if (reason == DLL_PROCESS_DETACH && reserved == nullptr && installed) {
        if (native_store) {
            const auto hooks = skcoop::saves::uninstall_native_io_hooks();
            if (hooks) { delete native_store; native_store = nullptr; }
        } else {
        if (DetourTransactionBegin() == NO_ERROR) {
            DetourUpdateThread(GetCurrentThread());
            DetourDetach(reinterpret_cast<PVOID*>(&original_create_file), reinterpret_cast<PVOID>(guarded_create_file));
            DetourTransactionCommit();
        }
        }
    }
    return TRUE;
}

extern "C" SKCOOP_BOOTSTRAP_API BOOL skcoop_bootstrap_fixture_path(WCHAR* output, DWORD capacity) {
    if (!output || !status || status->phase != SKCOOP_BOOTSTRAP_READY) return FALSE;
    DWORD length = 0;
    while (length < SKCOOP_BOOTSTRAP_ROOT_CHARS && config.protected_root[length]) ++length;
    constexpr WCHAR middle[] = L"\\Sekiro\\";
    constexpr WCHAR suffix[] = L"\\S0000.sl2";
    DWORD account_length = 0;
    while (account_length < SKCOOP_BOOTSTRAP_ACCOUNT_CHARS && config.account[account_length]) ++account_length;
    constexpr DWORD middle_length = static_cast<DWORD>(sizeof(middle) / sizeof(middle[0]) - 1);
    constexpr DWORD suffix_chars = static_cast<DWORD>(sizeof(suffix) / sizeof(suffix[0]));
    if (length >= SKCOOP_BOOTSTRAP_ROOT_CHARS || account_length >= SKCOOP_BOOTSTRAP_ACCOUNT_CHARS ||
        capacity < length + middle_length + account_length + suffix_chars) return FALSE;
    std::memcpy(output, config.protected_root, length * sizeof(WCHAR));
    std::memcpy(output + length, middle, middle_length * sizeof(WCHAR));
    length += middle_length;
    std::memcpy(output + length, config.account, account_length * sizeof(WCHAR));
    length += account_length;
    std::memcpy(output + length, suffix, sizeof(suffix));
    return TRUE;
}

extern "C" SKCOOP_BOOTSTRAP_API BOOL skcoop_bootstrap_fixture_checkpoint(DWORD checkpoint, DWORD observed_error) {
    if (!status || status->phase != SKCOOP_BOOTSTRAP_READY ||
        (checkpoint != SKCOOP_BOOTSTRAP_TLS && checkpoint != SKCOOP_BOOTSTRAP_GLOBAL &&
            checkpoint != SKCOOP_BOOTSTRAP_MAIN && checkpoint != SKCOOP_BOOTSTRAP_IO_COMPLETE)) return FALSE;
    const DWORD expected = config.profile_mode == SKCOOP_BOOTSTRAP_NATIVE_PROFILE ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
    if (observed_error != expected) {
        InterlockedIncrement(&status->unexpected_results);
        report_failure(ERROR_INVALID_DATA);
        return FALSE;
    }
    const LONG sequence = InterlockedIncrement(&status->sequence);
    if (checkpoint == SKCOOP_BOOTSTRAP_TLS) InterlockedExchange(&status->tls_sequence, sequence);
    if (checkpoint == SKCOOP_BOOTSTRAP_GLOBAL) InterlockedExchange(&status->global_sequence, sequence);
    if (checkpoint == SKCOOP_BOOTSTRAP_MAIN) InterlockedExchange(&status->main_sequence, sequence);
    if (checkpoint == SKCOOP_BOOTSTRAP_IO_COMPLETE) InterlockedExchange(&status->io_sequence, sequence);
    InterlockedOr(&status->checkpoint_mask, static_cast<LONG>(checkpoint));
    snapshot_native_counters();
    return TRUE;
}

extern "C" SKCOOP_BOOTSTRAP_API DWORD skcoop_bootstrap_fixture_mode(void) {
    return config.profile_mode;
}
