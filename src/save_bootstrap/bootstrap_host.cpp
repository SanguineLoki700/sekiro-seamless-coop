#include "skcoop/save_bootstrap.h"
#include <detours.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <string>

namespace {
class Handle {
public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_;
};
class View {
public:
    explicit View(void* pointer) noexcept : pointer_(pointer) {}
    ~View() { if (pointer_) UnmapViewOfFile(pointer_); }
    void* get() const noexcept { return pointer_; }
private:
    void* pointer_;
};
bool copy_text(WCHAR* output, size_t capacity, const std::wstring& text) {
    if (text.empty() || text.size() >= capacity) return false;
    std::memcpy(output, text.c_str(), (text.size() + 1) * sizeof(WCHAR));
    return true;
}
std::wstring quote(const std::wstring& argument) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (WCHAR c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result.push_back(c);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}
bool ordinary_directory(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}
DWORD error_or(DWORD fallback) { const DWORD value = GetLastError(); return value == ERROR_SUCCESS ? fallback : value; }
bool hash_file(HANDLE file, uint8_t (&digest)[32]) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    LARGE_INTEGER start{};
    if (ok) ok = SetFilePointerEx(file, start, nullptr, FILE_BEGIN) != FALSE;
    std::array<UCHAR, 65536> buffer{};
    while (ok) {
        DWORD count = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) { ok = false; break; }
        if (count == 0) break;
        ok = BCryptHashData(hash, buffer.data(), count, 0) >= 0;
    }
    if (ok) ok = BCryptFinishHash(hash, digest, static_cast<ULONG>(sizeof(digest)), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}
}

extern "C" BOOL skcoop_run_bootstrap_fixture(const WCHAR* fixture, const WCHAR* bootstrap,
    const WCHAR* controlled_root, uint32_t test_flags, DWORD timeout_ms,
    skcoop_bootstrap_run_result* output) {
    if (!output || output->struct_size != sizeof(*output)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *output = {};
    output->struct_size = sizeof(*output);
    output->process_exit_code = STILL_ACTIVE;
    const auto fail = [&](DWORD error) { output->host_error = error; SetLastError(error); return FALSE; };
    constexpr uint32_t allowed_flags = SKCOOP_BOOTSTRAP_TEST_NO_PAYLOAD | SKCOOP_BOOTSTRAP_TEST_TRUNCATED_PAYLOAD |
        SKCOOP_BOOTSTRAP_TEST_BAD_VERSION | SKCOOP_BOOTSTRAP_TEST_ABORT_TRANSACTION | SKCOOP_BOOTSTRAP_TEST_BAD_NONCE |
        SKCOOP_BOOTSTRAP_TEST_NATIVE_PROFILE;
    if (!fixture || !bootstrap || !controlled_root || !timeout_ms || timeout_ms > 30000 || (test_flags & ~allowed_flags))
        return fail(ERROR_INVALID_PARAMETER);
    try {
        const std::filesystem::path executable(fixture), dll(bootstrap), root(controlled_root);
        if (!executable.is_absolute() || !dll.is_absolute() || !root.is_absolute() ||
            _wcsicmp(executable.filename().c_str(), SKCOOP_BOOTSTRAP_FIXTURE_NAME) != 0 ||
            _wcsicmp(dll.filename().c_str(), SKCOOP_BOOTSTRAP_DLL_NAME) != 0)
            return fail(ERROR_ACCESS_DENIED);
#ifdef SKCOOP_BOOTSTRAP_FIXTURE_PATH
        if (_wcsicmp(std::filesystem::canonical(executable).c_str(),
            std::filesystem::canonical(std::filesystem::path(SKCOOP_BOOTSTRAP_FIXTURE_PATH)).c_str()) != 0)
            return fail(ERROR_ACCESS_DENIED);
#endif
        if (!ordinary_directory(root) || !ordinary_directory(root / L"solo") || !ordinary_directory(root / L"coop"))
            return fail(ERROR_DIRECTORY);
        // Hold the exact files without write/delete sharing until the child exits.
        Handle executable_file(CreateFileW(executable.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        Handle dll_file(CreateFileW(dll.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (executable_file.get() == INVALID_HANDLE_VALUE || dll_file.get() == INVALID_HANDLE_VALUE)
            return fail(error_or(ERROR_FILE_NOT_FOUND));
        BY_HANDLE_FILE_INFORMATION exe_info{}, dll_info{};
        if (!GetFileInformationByHandle(executable_file.get(), &exe_info) ||
            !GetFileInformationByHandle(dll_file.get(), &dll_info) ||
            (exe_info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            (dll_info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return fail(ERROR_ACCESS_DENIED);
        Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
            static_cast<DWORD>(sizeof(skcoop_bootstrap_status)), nullptr));
        Handle ready_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!mapping.get() || !ready_event.get()) return fail(error_or(ERROR_NOT_ENOUGH_MEMORY));
        View view(MapViewOfFile(mapping.get(), FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(skcoop_bootstrap_status)));
        if (!view.get()) return fail(error_or(ERROR_NOT_ENOUGH_MEMORY));
        auto* shared = static_cast<skcoop_bootstrap_status*>(view.get());
        std::memset(shared, 0, sizeof(*shared));
        shared->struct_size = sizeof(*shared);
        shared->version = SKCOOP_BOOTSTRAP_VERSION;
        if (BCryptGenRandom(nullptr, shared->nonce, static_cast<ULONG>(sizeof(shared->nonce)), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
            return fail(ERROR_GEN_FAILURE);
        skcoop_bootstrap_payload payload{};
        payload.struct_size = sizeof(payload);
        payload.version = SKCOOP_BOOTSTRAP_VERSION;
        payload.test_flags = test_flags & SKCOOP_BOOTSTRAP_TEST_ABORT_TRANSACTION;
        payload.profile_mode = (test_flags & SKCOOP_BOOTSTRAP_TEST_NATIVE_PROFILE) ?
            SKCOOP_BOOTSTRAP_NATIVE_PROFILE : SKCOOP_BOOTSTRAP_DENY_ONLY;
        shared->profile_mode = payload.profile_mode;
        if (!hash_file(executable_file.get(), payload.executable_sha256)) return fail(ERROR_GEN_FAILURE);
        std::memcpy(payload.nonce, shared->nonce, sizeof(payload.nonce));
        if (!copy_text(payload.protected_root, SKCOOP_BOOTSTRAP_ROOT_CHARS, (root / L"solo").wstring()) ||
            !copy_text(payload.coop_root, SKCOOP_BOOTSTRAP_ROOT_CHARS, (root / L"coop").wstring()) ||
            !copy_text(payload.account, SKCOOP_BOOTSTRAP_ACCOUNT_CHARS, L"12345678901234567"))
            return fail(ERROR_FILENAME_EXCED_RANGE);
        const std::wstring dll_wide = dll.wstring();
        const int dll_bytes = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, dll_wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (!dll_bytes) return fail(error_or(ERROR_NO_UNICODE_TRANSLATION));
        std::string dll_ansi(static_cast<size_t>(dll_bytes), '\0');
        BOOL used_default = FALSE;
        if (!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, dll_wide.c_str(), -1,
            dll_ansi.data(), dll_bytes, nullptr, &used_default) || used_default)
            return fail(ERROR_NO_UNICODE_TRANSLATION);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        auto command = quote(executable.wstring());
        if (!DetourCreateProcessWithDllExW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, executable.parent_path().c_str(), &startup,
            &child, dll_ansi.c_str(), nullptr)) return fail(error_or(ERROR_DLL_INIT_FAILED));
        Handle process(child.hProcess), thread(child.hThread);
        output->process_id = child.dwProcessId;
        shared->process_id = child.dwProcessId;
        payload.process_id = child.dwProcessId;
        bool resumed = false;
        DWORD preparation_error = ERROR_SUCCESS;
        if (!DuplicateHandle(GetCurrentProcess(), mapping.get(), process.get(), &payload.status_mapping,
            FILE_MAP_READ | FILE_MAP_WRITE, FALSE, 0) ||
            !DuplicateHandle(GetCurrentProcess(), ready_event.get(), process.get(), &payload.ready_event,
                EVENT_MODIFY_STATE, FALSE, 0)) preparation_error = error_or(ERROR_ACCESS_DENIED);
        if (test_flags & SKCOOP_BOOTSTRAP_TEST_BAD_VERSION) ++payload.version;
        if (test_flags & SKCOOP_BOOTSTRAP_TEST_BAD_NONCE) payload.nonce[0] ^= 0xff;
        if (preparation_error == ERROR_SUCCESS && !(test_flags & SKCOOP_BOOTSTRAP_TEST_NO_PAYLOAD)) {
            const DWORD bytes = static_cast<DWORD>(sizeof(payload) - ((test_flags & SKCOOP_BOOTSTRAP_TEST_TRUNCATED_PAYLOAD) ? 1u : 0u));
            if (!DetourCopyPayloadToProcess(process.get(), SKCOOP_BOOTSTRAP_PAYLOAD_GUID, &payload, bytes))
                preparation_error = error_or(ERROR_INVALID_DATA);
        }
        if (preparation_error == ERROR_SUCCESS) {
            resumed = ResumeThread(thread.get()) != static_cast<DWORD>(-1);
            if (!resumed) preparation_error = error_or(ERROR_GEN_FAILURE);
        }
        const ULONGLONG deadline = GetTickCount64() + timeout_ms;
        if (resumed) {
            const HANDLE waits[] = {ready_event.get(), process.get()};
            const DWORD first = WaitForMultipleObjects(2, waits, FALSE, timeout_ms);
            output->ready_signaled = first == WAIT_OBJECT_0;
            if (first == WAIT_FAILED) preparation_error = error_or(ERROR_GEN_FAILURE);
            if (first == WAIT_TIMEOUT) output->timed_out = TRUE;
            const ULONGLONG now = GetTickCount64();
            const DWORD remaining = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
            const DWORD ended = WaitForSingleObject(process.get(), remaining);
            output->process_observed = ended == WAIT_OBJECT_0;
            if (ended == WAIT_TIMEOUT) output->timed_out = TRUE;
            if (ended == WAIT_FAILED) preparation_error = error_or(ERROR_GEN_FAILURE);
        }
        if (!output->process_observed) {
            // This handle is exclusively the child created above; never an attached process.
            if (!TerminateProcess(process.get(), ERROR_CANCELLED) && GetLastError() != ERROR_ACCESS_DENIED)
                preparation_error = error_or(ERROR_GEN_FAILURE);
            output->process_observed = WaitForSingleObject(process.get(), 5000) == WAIT_OBJECT_0;
            if (!output->process_observed) preparation_error = ERROR_TIMEOUT;
        }
        if (!GetExitCodeProcess(process.get(), &output->process_exit_code))
            preparation_error = error_or(ERROR_GEN_FAILURE);
        std::memcpy(&output->status, shared, sizeof(*shared));
        if (preparation_error != ERROR_SUCCESS) return fail(preparation_error);
        if (output->timed_out) return fail(ERROR_TIMEOUT);
        return TRUE;
    } catch (...) { return fail(ERROR_INVALID_DATA); }
}
