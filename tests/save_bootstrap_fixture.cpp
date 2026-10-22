#include "skcoop/save_bootstrap.h"
#include <cstring>

extern "C" __declspec(dllexport) const uint64_t skcoop_bootstrap_fixture_marker = SKCOOP_BOOTSTRAP_FIXTURE_MAGIC;

namespace {
volatile LONG failed = 0;
constexpr DWORD path_capacity = SKCOOP_BOOTSTRAP_ROOT_CHARS + SKCOOP_BOOTSTRAP_ACCOUNT_CHARS + 32;
DWORD failure_error() noexcept { const auto error = GetLastError(); return error ? error : ERROR_INVALID_DATA; }
bool cooperative_suffix(const WCHAR* text, DWORD count) noexcept {
    constexpr WCHAR name[] = L"S0000.co2";
    if (count < 9) return false;
    for (DWORD i = 0; i < 9; ++i) {
        WCHAR value = text[count - 9 + i];
        if (value >= L'A' && value <= L'Z') value = static_cast<WCHAR>(value + L'a' - L'A');
        WCHAR expected = name[i];
        if (expected >= L'A' && expected <= L'Z') expected = static_cast<WCHAR>(expected + L'a' - L'A');
        if (value != expected) return false;
    }
    return true;
}
template<class Function> Function resolve(HMODULE module, const char* name) noexcept {
    const auto address = GetProcAddress(module, name);
    Function result{};
    static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result));
    return result;
}
DWORD native_write(HANDLE file, DWORD phase) noexcept {
    const LONGLONG offset = phase == SKCOOP_BOOTSTRAP_TLS ? 16 : phase == SKCOOP_BOOTSTRAP_GLOBAL ? 64 : 128;
    const BYTE pattern[8] = {'S', 'K', 'C', 'O', 'O', 'P', static_cast<BYTE>(phase), 0xa5};
    BYTE actual[8]{};
    WCHAR actual_path[path_capacity + 16]{};
    const DWORD path_length = GetFinalPathNameByHandleW(file, actual_path,
        static_cast<DWORD>(sizeof(actual_path) / sizeof(actual_path[0])), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!path_length || path_length >= sizeof(actual_path) / sizeof(actual_path[0]) ||
        !cooperative_suffix(actual_path, path_length) || GetFileType(file) != FILE_TYPE_DISK)
        return ERROR_INVALID_HANDLE;
    LARGE_INTEGER position{}, size{};
    position.QuadPart = offset;
    DWORD count = 0;
    if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN) ||
        !WriteFile(file, pattern, sizeof(pattern), &count, nullptr) || count != sizeof(pattern) ||
        !FlushFileBuffers(file) || !GetFileSizeEx(file, &size) || size.QuadPart < offset + static_cast<LONGLONG>(sizeof(pattern)) ||
        !SetFilePointerEx(file, position, nullptr, FILE_BEGIN) ||
        !ReadFile(file, actual, sizeof(actual), &count, nullptr) || count != sizeof(actual)) {
        const DWORD error = GetLastError();
        return error == ERROR_SUCCESS ? ERROR_INVALID_DATA : error;
    }
    return std::memcmp(pattern, actual, sizeof(pattern)) == 0 ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}
void checkpoint(DWORD phase) noexcept {
    const auto module = GetModuleHandleW(SKCOOP_BOOTSTRAP_DLL_NAME);
    if (!module) { InterlockedExchange(&failed, 1); return; }
    const auto get_path = resolve<skcoop_bootstrap_fixture_path_fn>(module, "skcoop_bootstrap_fixture_path");
    const auto record = resolve<skcoop_bootstrap_fixture_checkpoint_fn>(module, "skcoop_bootstrap_fixture_checkpoint");
    const auto get_mode = resolve<skcoop_bootstrap_fixture_mode_fn>(module, "skcoop_bootstrap_fixture_mode");
    WCHAR path[path_capacity]{};
    if (!get_path || !record || !get_mode || !get_path(path, path_capacity)) {
        InterlockedExchange(&failed, 1);
        return;
    }
    const bool native = get_mode() == SKCOOP_BOOTSTRAP_NATIVE_PROFILE;
    SetLastError(ERROR_SUCCESS);
    const auto handle = CreateFileW(path, native ? GENERIC_READ | GENERIC_WRITE : GENERIC_WRITE,
        0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD error = handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    if (handle != INVALID_HANDLE_VALUE) {
        if (native) error = native_write(handle, phase);
        if (!CloseHandle(handle)) error = GetLastError();
    }
    if (!record(phase, error)) InterlockedExchange(&failed, 1);
}
DWORD native_lifecycle(const WCHAR* primary) noexcept {
    WCHAR backup[path_capacity]{};
    size_t length = 0;
    while (length < path_capacity && primary[length]) ++length;
    if (length >= path_capacity - 4) return ERROR_FILENAME_EXCED_RANGE;
    std::memcpy(backup, primary, length * sizeof(WCHAR));
    std::memcpy(backup + length, L".bak", 5 * sizeof(WCHAR));
    WIN32_FILE_ATTRIBUTE_DATA before{}, copy{};
    if (GetFileAttributesW(primary) == INVALID_FILE_ATTRIBUTES ||
        !GetFileAttributesExW(primary, GetFileExInfoStandard, &before) ||
        !CopyFileW(primary, backup, TRUE) ||
        !GetFileAttributesExW(backup, GetFileExInfoStandard, &copy)) return failure_error();
    if (before.nFileSizeHigh != copy.nFileSizeHigh || before.nFileSizeLow != copy.nFileSizeLow)
        return ERROR_INVALID_DATA;
    if (!DeleteFileW(primary)) return failure_error();
    SetLastError(ERROR_SUCCESS);
    if (GetFileAttributesW(primary) != INVALID_FILE_ATTRIBUTES || GetLastError() != ERROR_FILE_NOT_FOUND)
        return ERROR_INVALID_DATA;
    if (!CopyFileW(backup, primary, TRUE) || !DeleteFileW(backup) || !CopyFileW(primary, backup, TRUE))
        return failure_error();
    const auto restored = CreateFileW(primary, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (restored == INVALID_HANDLE_VALUE) return failure_error();
    BYTE actual[8]{};
    const BYTE expected[8] = {'S', 'K', 'C', 'O', 'O', 'P', SKCOOP_BOOTSTRAP_TLS, 0xa5};
    LARGE_INTEGER offset{};
    offset.QuadPart = 16;
    DWORD count = 0;
    bool ok = SetFilePointerEx(restored, offset, nullptr, FILE_BEGIN) &&
        ReadFile(restored, actual, sizeof(actual), &count, nullptr) && count == sizeof(actual) &&
        std::memcmp(actual, expected, sizeof(actual)) == 0;
    if (!CloseHandle(restored)) ok = false;
    return ok ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}
void NTAPI tls_callback(PVOID, DWORD reason, PVOID) {
    if (reason == DLL_PROCESS_ATTACH) checkpoint(SKCOOP_BOOTSTRAP_TLS);
}
struct GlobalProbe {
    GlobalProbe() noexcept { checkpoint(SKCOOP_BOOTSTRAP_GLOBAL); }
} global_probe;
}

#pragma section(".CRT$XLB", long, read)
extern "C" {
__declspec(allocate(".CRT$XLB")) PIMAGE_TLS_CALLBACK skcoop_bootstrap_tls_callback = tls_callback;
}
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:skcoop_bootstrap_tls_callback")

int wmain() {
    checkpoint(SKCOOP_BOOTSTRAP_MAIN);
    const auto module = GetModuleHandleW(SKCOOP_BOOTSTRAP_DLL_NAME);
    if (module && failed == 0) {
        const auto get_mode = resolve<skcoop_bootstrap_fixture_mode_fn>(module, "skcoop_bootstrap_fixture_mode");
        if (get_mode && get_mode() == SKCOOP_BOOTSTRAP_NATIVE_PROFILE) {
            const auto get_path = resolve<skcoop_bootstrap_fixture_path_fn>(module, "skcoop_bootstrap_fixture_path");
            const auto record = resolve<skcoop_bootstrap_fixture_checkpoint_fn>(module, "skcoop_bootstrap_fixture_checkpoint");
            WCHAR path[path_capacity]{};
            if (!get_path || !record || !get_path(path, path_capacity)) InterlockedExchange(&failed, 1);
            else if (!record(SKCOOP_BOOTSTRAP_IO_COMPLETE, native_lifecycle(path))) InterlockedExchange(&failed, 1);
        }
    }
    return failed == 0 ? 0 : 40;
}
