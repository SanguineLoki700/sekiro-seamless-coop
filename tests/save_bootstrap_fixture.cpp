#include "skcoop/save_bootstrap.h"
#include <ShlObj.h>
#include <cstring>
#include <filesystem>
#include <string>

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
DWORD CALLBACK forbidden_progress(LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER,
    DWORD, DWORD, HANDLE, HANDLE, LPVOID context) {
    ++*static_cast<unsigned*>(context);
    return PROGRESS_CONTINUE;
}
DWORD native_contracts(const WCHAR* primary) {
    namespace fs = std::filesystem;
    const fs::path source(primary), account = source.parent_path();
    const auto expected_root = account.parent_path().parent_path().parent_path() / L"coop";
    const auto unrelated = account / L"fixture-unrelated.tmp";
    const auto outside = expected_root.parent_path() / L"S0000.sl2";
    struct Cleanup {
        const fs::path& unrelated;
        ~Cleanup() { DeleteFileW(unrelated.c_str()); }
    } cleanup{unrelated};
    WCHAR roaming[MAX_PATH]{};
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, roaming) != S_OK ||
        CompareStringOrdinal(roaming, -1, expected_root.c_str(), -1, TRUE) != CSTR_EQUAL)
        return ERROR_INVALID_DATA;
    if (!(GetFileAttributesW(roaming) & FILE_ATTRIBUTE_DIRECTORY) ||
        CreateDirectoryW(roaming, nullptr) || GetLastError() != ERROR_ALREADY_EXISTS)
        return ERROR_INVALID_DATA;
    // Startup probes the directory itself, including its trailing separator.
    // The returned metadata handle must name the private directory, and cannot
    // read file data. It is a real handle owned and closed by the caller.
    const auto directory_path = account.wstring() + L"\\";
    const auto directory_file = CreateFileW(directory_path.c_str(), 0, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directory_file == INVALID_HANDLE_VALUE) return failure_error();
    WCHAR directory_actual[path_capacity + 16]{};
    BY_HANDLE_FILE_INFORMATION directory_info{};
    const auto directory_length = GetFinalPathNameByHandleW(directory_file, directory_actual,
        static_cast<DWORD>(sizeof(directory_actual) / sizeof(directory_actual[0])), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    const auto directory_expected = L"\\\\?\\" + (expected_root / L"Sekiro" / account.filename()).wstring();
    BYTE forbidden{}; DWORD directory_read{};
    const bool directory_valid = directory_length && directory_length < sizeof(directory_actual) / sizeof(directory_actual[0]) &&
        CompareStringOrdinal(directory_actual, -1, directory_expected.c_str(), -1, TRUE) == CSTR_EQUAL &&
        GetFileInformationByHandle(directory_file, &directory_info) && (directory_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !ReadFile(directory_file, &forbidden, sizeof(forbidden), &directory_read, nullptr);
    if (!CloseHandle(directory_file) || !directory_valid) return ERROR_INVALID_DATA;
    const auto relocated = fs::path(roaming) / L"Sekiro" / account.filename() / L"S0000.sl2";
    const auto relocated_file = CreateFileW(relocated.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (relocated_file == INVALID_HANDLE_VALUE) return failure_error();
    if (!CloseHandle(relocated_file)) return failure_error();

    char ansi[path_capacity * 4]{};
    const UINT page = AreFileApisANSI() ? GetACP() : GetOEMCP();
    BOOL substituted = FALSE;
    if (!WideCharToMultiByte(page, 0, primary, -1, ansi, static_cast<int>(sizeof(ansi)), nullptr,
            page == CP_UTF8 || page == CP_UTF7 ? nullptr : &substituted) || substituted)
        return ERROR_NO_UNICODE_TRANSLATION;
    const auto ansi_file = CreateFileA(ansi, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (ansi_file == INVALID_HANDLE_VALUE) return failure_error();
    WCHAR actual_path[path_capacity + 16]{};
    const auto length = GetFinalPathNameByHandleW(ansi_file, actual_path,
        static_cast<DWORD>(sizeof(actual_path) / sizeof(actual_path[0])), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    const bool ansi_redirected = length && length < sizeof(actual_path) / sizeof(actual_path[0]) &&
        cooperative_suffix(actual_path, length);
    if (!CloseHandle(ansi_file) || !ansi_redirected) return ERROR_INVALID_DATA;

    // Unsupported APIs must fail without invoking callbacks or touching files.
    WIN32_FIND_DATAW found{};
    const auto find = FindFirstFileW(primary, &found);
    const auto find_error = GetLastError();
    if (find != INVALID_HANDLE_VALUE) { FindClose(find); return ERROR_INVALID_DATA; }
    if (find_error != ERROR_NOT_SUPPORTED || GetFileAttributesA(ansi) != INVALID_FILE_ATTRIBUTES ||
        GetLastError() != ERROR_NOT_SUPPORTED || DeleteFileA(ansi) || GetLastError() != ERROR_NOT_SUPPORTED ||
        CopyFileA(ansi, ansi, FALSE) || GetLastError() != ERROR_NOT_SUPPORTED)
        return ERROR_INVALID_DATA;
    const auto backup = source.wstring() + L".bak";
    unsigned callback_calls{};
    if (CopyFileExW(primary, backup.c_str(), forbidden_progress, &callback_calls, nullptr, 0) ||
        GetLastError() != ERROR_NOT_SUPPORTED || callback_calls ||
        MoveFileW(primary, backup.c_str()) || GetLastError() != ERROR_NOT_SUPPORTED ||
        ReplaceFileW(primary, backup.c_str(), nullptr, 0, nullptr, nullptr) || GetLastError() != ERROR_NOT_SUPPORTED)
        return ERROR_INVALID_DATA;
    const auto refused_open = [](const fs::path& path, DWORD flags, LPSECURITY_ATTRIBUTES security, DWORD expected_error) {
        const auto file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, security,
            OPEN_ALWAYS, flags, nullptr);
        const auto error = GetLastError();
        if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); return false; }
        return error == expected_error;
    };
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, FALSE};
    if (!refused_open(source, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr, ERROR_NOT_SUPPORTED) ||
        !refused_open(source, FILE_ATTRIBUTE_NORMAL, &security, ERROR_NOT_SUPPORTED) ||
        !refused_open(account, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr, ERROR_NOT_SUPPORTED) ||
        !refused_open(outside, FILE_ATTRIBUTE_NORMAL, nullptr, ERROR_ACCESS_DENIED) ||
        !refused_open(account.parent_path() / L"987654321" / L"S0000.sl2", FILE_ATTRIBUTE_NORMAL, nullptr, ERROR_ACCESS_DENIED))
        return ERROR_INVALID_DATA;

    // Unrelated files retain native creation/errors/data. A mixed save/non-save
    // copy must not overwrite this sentinel even when fail_if_exists is false.
    auto file = CreateFileW(unrelated.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return failure_error();
    constexpr BYTE sentinel[]{0x19, 0x36, 0x72, 0xa4}; DWORD count{};
    const bool written = WriteFile(file, sentinel, sizeof(sentinel), &count, nullptr) && count == sizeof(sentinel);
    if (!CloseHandle(file) || !written) return ERROR_INVALID_DATA;
    file = CreateFileW(unrelated.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const auto open_error = GetLastError();
    if (file == INVALID_HANDLE_VALUE) return failure_error();
    if (!CloseHandle(file) || open_error != ERROR_ALREADY_EXISTS) return ERROR_INVALID_DATA;
    if (CopyFileW(primary, unrelated.c_str(), FALSE) || GetLastError() != ERROR_ACCESS_DENIED)
        return ERROR_INVALID_DATA;
    file = CreateFileW(unrelated.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return failure_error();
    BYTE content[sizeof(sentinel)]{};
    const bool preserved = ReadFile(file, content, sizeof(content), &count, nullptr) &&
        count == sizeof(content) && std::memcmp(content, sentinel, sizeof(content)) == 0;
    if (!CloseHandle(file) || !preserved) return ERROR_INVALID_DATA;
    return ERROR_SUCCESS;
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
            else {
                auto result = native_lifecycle(path);
                if (result == ERROR_SUCCESS) {
                    try { result = native_contracts(path); }
                    catch (...) { result = ERROR_GEN_FAILURE; }
                }
                if (!record(SKCOOP_BOOTSTRAP_IO_COMPLETE, result)) InterlockedExchange(&failed, 1);
            }
        }
    }
    return failed == 0 ? 0 : 40;
}
