#include "native_io_hooks.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <ShlObj.h>
#include <detours.h>

#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

namespace skcoop::saves {
namespace {

#define REAL_API(name) decltype(&::name) real_##name = &::name
REAL_API(CreateFileW); REAL_API(CreateFileA); REAL_API(DeleteFileW); REAL_API(DeleteFileA);
REAL_API(CopyFileW); REAL_API(CopyFileA); REAL_API(CopyFileExW); REAL_API(CopyFileExA);
REAL_API(GetFileAttributesW); REAL_API(GetFileAttributesA);
REAL_API(GetFileAttributesExW); REAL_API(GetFileAttributesExA);
REAL_API(CreateDirectoryW); REAL_API(CreateDirectoryA); REAL_API(RemoveDirectoryW); REAL_API(RemoveDirectoryA);
REAL_API(FindFirstFileW); REAL_API(FindFirstFileA); REAL_API(FindFirstFileExW); REAL_API(FindFirstFileExA);
REAL_API(MoveFileW); REAL_API(MoveFileA); REAL_API(MoveFileExW); REAL_API(MoveFileExA);
REAL_API(ReplaceFileW); REAL_API(ReplaceFileA); REAL_API(SHGetFolderPathW);
#undef REAL_API

struct AtomicCounters {
    std::atomic<std::uint64_t> redirected{}, denied{}, passthrough{}, internal_bypass{};
    std::atomic<std::uint64_t> appdata_redirects{}, directory_queries{}, copy_failures{}, destination_may_be_modified{};
    NativeIoCounters snapshot() const noexcept {
        return {redirected.load(), denied.load(), passthrough.load(), internal_bypass.load(),
            appdata_redirects.load(), directory_queries.load(), copy_failures.load(), destination_may_be_modified.load()};
    }
    void clear() noexcept {
        redirected = 0; denied = 0; passthrough = 0; internal_bypass = 0;
        appdata_redirects = 0; directory_queries = 0; copy_failures = 0; destination_may_be_modified = 0;
    }
};
AtomicCounters counters;
thread_local unsigned internal_bypass{};
struct InternalCopy {
    InternalCopy() noexcept { ++internal_bypass; }
    ~InternalCopy() { --internal_bypass; }
};
struct DirectoryPin {
    HANDLE handle{INVALID_HANDLE_VALUE};
    std::wstring path;
    ~DirectoryPin() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
struct State {
    NativeIoBinding binding;
    std::array<std::wstring, 3> solo_directories;
    std::array<DirectoryPin, 3> private_directories;
};
// Deliberately not a destructed C++ owning singleton: callbacks may still run
// during CRT/process teardown. Explicit quiescent uninstall is the only delete.
std::atomic<State*> active_state{};
std::atomic<bool> installed{};
std::atomic_flag transition = ATOMIC_FLAG_INIT;

bool equal_windows(std::wstring_view left, std::wstring_view right) noexcept {
    return left.size() == right.size() && (left.empty() ||
        CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
            static_cast<int>(right.size()), TRUE) == CSTR_EQUAL);
}
std::wstring normalized(std::wstring_view path) {
    std::wstring value(path);
    for (auto& c : value) if (c == L'/') c = L'\\';
    if (value.size() > 3 && value.back() == L'\\') value.pop_back();
    return value;
}
std::wstring join(std::wstring_view base, std::wstring_view leaf) {
    std::wstring value(base);
    if (!value.empty() && value.back() != L'\\' && value.back() != L'/') value += L'\\';
    value += leaf;
    return value;
}
bool below(std::wstring_view tree, std::wstring_view value) noexcept {
    return value.size() > tree.size() && value[tree.size()] == L'\\' && equal_windows(value.substr(0, tree.size()), tree);
}
DWORD system_error(const SaveStoreStatus& status) noexcept {
    if (status.system_error) return status.system_error;
    switch (status.error) {
    case SaveStoreError::none: return ERROR_SUCCESS;
    case SaveStoreError::missing_file: return ERROR_FILE_NOT_FOUND;
    case SaveStoreError::busy: return ERROR_SHARING_VIOLATION;
    case SaveStoreError::backup_exists: return ERROR_FILE_EXISTS;
    case SaveStoreError::invalid_configuration: return ERROR_INVALID_PARAMETER;
    default: return ERROR_ACCESS_DENIED;
    }
}
template<class Result> Result denied(Result result, DWORD error) noexcept {
    ++counters.denied;
    SetLastError(error ? error : ERROR_ACCESS_DENIED);
    return result;
}
template<class Result, class Function> Result boundary(Result failure, Function&& function) noexcept {
    try { return function(); }
    catch (const std::bad_alloc&) { return denied(failure, ERROR_NOT_ENOUGH_MEMORY); }
    catch (...) { return denied(failure, ERROR_GEN_FAILURE); }
}
bool read_path(LPCWSTR path, std::wstring_view& result, DWORD& error) noexcept {
    if (!path) { error = ERROR_INVALID_PARAMETER; return false; }
    std::size_t length{};
    while (length < 32768 && path[length]) ++length;
    if (length == 32768) { error = ERROR_FILENAME_EXCED_RANGE; return false; }
    result = {path, length};
    return true;
}
bool convert_path(LPCSTR path, std::wstring& result, DWORD& error) {
    if (!path) { error = ERROR_INVALID_PARAMETER; return false; }
    std::size_t length{};
    while (length < 32768 && path[length]) ++length;
    if (length == 32768) { error = ERROR_FILENAME_EXCED_RANGE; return false; }
    if (length == 0) { result.clear(); return true; }
    const UINT page = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
    const int count = MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, path, static_cast<int>(length), nullptr, 0);
    if (count == 0) { error = GetLastError(); return false; }
    result.resize(static_cast<std::size_t>(count));
    if (!MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, path, static_cast<int>(length), result.data(), count)) {
        error = GetLastError(); return false;
    }
    return true;
}

enum class DirectoryAction { unrelated, exact, deny };
struct DirectoryDecision { DirectoryAction action{DirectoryAction::unrelated}; std::size_t index{}; };
DirectoryDecision directory(State& state, std::wstring_view path, bool creating = false) {
    const auto value = normalized(path);
    for (std::size_t i = 0; i < 3; ++i)
        if (equal_windows(value, state.solo_directories[i]) || equal_windows(value, state.private_directories[i].path))
            return {DirectoryAction::exact, i};
    for (const auto tree : {std::wstring_view(state.solo_directories[1]), std::wstring_view(state.private_directories[1].path)}) {
        if (!below(tree, value)) continue;
        const auto remainder = std::wstring_view(value).substr(tree.size() + 1);
        const auto slash = remainder.find(L'\\');
        const auto candidate_account = remainder.substr(0, slash);
        if (candidate_account != state.binding.account || creating) return {DirectoryAction::deny, 0};
    }
    return {};
}
NativePathDecision file(State& state, std::wstring_view path) {
    return decide_native_save_path(path, state.binding.policy, state.binding.process, state.binding.account);
}
bool selected(State& state, std::wstring_view path) {
    return file(state, path).action != NativePathAction::passthrough || directory(state, path).action != DirectoryAction::unrelated;
}
DWORD path_error(const NativePathDecision& decision) noexcept {
    return decision.error == NativePathError::same_destination ? ERROR_INVALID_PARAMETER : ERROR_ACCESS_DENIED;
}
void filetime(FILETIME& result, std::uint64_t value) noexcept {
    result.dwLowDateTime = static_cast<DWORD>(value);
    result.dwHighDateTime = static_cast<DWORD>(value >> 32);
}
bool query(State& state, std::wstring_view path, WIN32_FILE_ATTRIBUTE_DATA& result, DWORD& error) {
    const auto decision = file(state, path);
    if (decision.action == NativePathAction::deny) { error = path_error(decision); return false; }
    if (decision.action == NativePathAction::redirect) {
        const auto information = state.binding.store->query_native(decision.kind);
        if (!information) { error = system_error(information.status); return false; }
        result.dwFileAttributes = information.attributes;
        result.nFileSizeLow = static_cast<DWORD>(information.size);
        result.nFileSizeHigh = static_cast<DWORD>(information.size >> 32);
        filetime(result.ftCreationTime, information.creation_time);
        filetime(result.ftLastAccessTime, information.access_time);
        filetime(result.ftLastWriteTime, information.write_time);
        ++counters.redirected;
        return true;
    }
    const auto folder = directory(state, path);
    if (folder.action != DirectoryAction::exact) { error = ERROR_ACCESS_DENIED; return false; }
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(state.private_directories[folder.index].handle, &information)) {
        error = GetLastError(); return false;
    }
    result = {information.dwFileAttributes, information.ftCreationTime, information.ftLastAccessTime,
        information.ftLastWriteTime, information.nFileSizeHigh, information.nFileSizeLow};
    ++counters.redirected;
    ++counters.directory_queries;
    return true;
}

template<class Function> HANDLE create_file(std::wstring_view path, DWORD access, DWORD sharing,
    LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE model,
    DWORD incoming_error, Function&& original) {
    auto* state = active_state.load();
    if (!state) return denied(INVALID_HANDLE_VALUE, ERROR_NOT_READY);
    const auto decision = file(*state, path);
    if (decision.action == NativePathAction::deny) return denied(INVALID_HANDLE_VALUE, path_error(decision));
    if (decision.action == NativePathAction::redirect) {
        const auto result = state->binding.store->open_native(decision.kind,
            {access, sharing, disposition, flags, security != nullptr, model != nullptr});
        if (!result) return denied(INVALID_HANDLE_VALUE, result.last_error ? result.last_error : system_error(result.status));
        ++counters.redirected;
        SetLastError(result.last_error);
        return result.handle;
    }
    const auto folder = directory(*state, path);
    if (folder.action == DirectoryAction::deny) return denied(INVALID_HANDLE_VALUE, ERROR_ACCESS_DENIED);
    if (folder.action == DirectoryAction::exact) {
        // Directory handles are not exposed by this file adapter: relative NT
        // opens through them would require an additional verified boundary.
        return denied(INVALID_HANDLE_VALUE, ERROR_NOT_SUPPORTED);
    }
    ++counters.passthrough;
    SetLastError(incoming_error);
    return original();
}

HANDLE WINAPI hook_CreateFileW(LPCWSTR name, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES security,
    DWORD disposition, DWORD flags, HANDLE model) {
    const auto incoming = GetLastError();
    if (internal_bypass) { ++counters.internal_bypass; return real_CreateFileW(name, access, sharing, security, disposition, flags, model); }
    return boundary<HANDLE>(INVALID_HANDLE_VALUE, [&] {
        std::wstring_view path; DWORD error{};
        if (!read_path(name, path, error)) return denied(INVALID_HANDLE_VALUE, error);
        return create_file(path, access, sharing, security, disposition, flags, model, incoming,
            [&] { return real_CreateFileW(name, access, sharing, security, disposition, flags, model); });
    });
}
HANDLE WINAPI hook_CreateFileA(LPCSTR name, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES security,
    DWORD disposition, DWORD flags, HANDLE model) {
    const auto incoming = GetLastError();
    if (internal_bypass) { ++counters.internal_bypass; return real_CreateFileA(name, access, sharing, security, disposition, flags, model); }
    return boundary<HANDLE>(INVALID_HANDLE_VALUE, [&] {
        std::wstring path; DWORD error{};
        if (!convert_path(name, path, error)) return denied(INVALID_HANDLE_VALUE, error);
        return create_file(path, access, sharing, security, disposition, flags, model, incoming,
            [&] { return real_CreateFileA(name, access, sharing, security, disposition, flags, model); });
    });
}

BOOL WINAPI hook_DeleteFileW(LPCWSTR name) {
    const auto incoming = GetLastError();
    if (internal_bypass) { ++counters.internal_bypass; return real_DeleteFileW(name); }
    return boundary<BOOL>(FALSE, [&] {
        std::wstring_view path; DWORD error{};
        if (!read_path(name, path, error)) return denied(FALSE, error);
        auto* state = active_state.load();
        if (!state) return denied(FALSE, ERROR_NOT_READY);
        const auto decision = file(*state, path);
        if (decision.action == NativePathAction::deny) return denied(FALSE, path_error(decision));
        if (decision.action == NativePathAction::redirect) {
            const auto result = state->binding.store->remove_native(decision.kind);
            if (!result) return denied(FALSE, system_error(result));
            ++counters.redirected; SetLastError(ERROR_SUCCESS); return TRUE;
        }
        if (directory(*state, path).action != DirectoryAction::unrelated) return denied(FALSE, ERROR_ACCESS_DENIED);
        ++counters.passthrough; SetLastError(incoming); return real_DeleteFileW(name);
    });
}
BOOL WINAPI hook_CopyFileW(LPCWSTR from, LPCWSTR to, BOOL fail_if_exists) {
    const auto incoming = GetLastError();
    if (internal_bypass) { ++counters.internal_bypass; return real_CopyFileW(from, to, fail_if_exists); }
    return boundary<BOOL>(FALSE, [&] {
        std::wstring_view source, target; DWORD error{};
        if (!read_path(from, source, error) || !read_path(to, target, error)) return denied(FALSE, error);
        auto* state = active_state.load();
        if (!state) return denied(FALSE, ERROR_NOT_READY);
        const auto decision = decide_native_save_pair(source, target, state->binding.policy, state->binding.process, state->binding.account);
        if (decision.action == NativePathAction::deny) return denied(FALSE,
            decision.error == NativePathError::same_destination ? ERROR_INVALID_PARAMETER : ERROR_ACCESS_DENIED);
        if (decision.action == NativePathAction::redirect) {
            SaveStoreStatus result;
            { InternalCopy trusted; result = state->binding.store->copy_native(decision.source.kind, decision.target.kind, fail_if_exists != FALSE); }
            if (result.destination_may_be_modified) ++counters.destination_may_be_modified;
            if (!result) { ++counters.copy_failures; return denied(FALSE, system_error(result)); }
            ++counters.redirected; SetLastError(ERROR_SUCCESS); return TRUE;
        }
        if (directory(*state, source).action != DirectoryAction::unrelated || directory(*state, target).action != DirectoryAction::unrelated)
            return denied(FALSE, ERROR_ACCESS_DENIED);
        ++counters.passthrough; SetLastError(incoming); return real_CopyFileW(from, to, fail_if_exists);
    });
}
DWORD WINAPI hook_GetFileAttributesW(LPCWSTR name) {
    const auto incoming = GetLastError();
    if (internal_bypass) { ++counters.internal_bypass; return real_GetFileAttributesW(name); }
    return boundary<DWORD>(INVALID_FILE_ATTRIBUTES, [&] {
        std::wstring_view path; DWORD error{};
        if (!read_path(name, path, error)) return denied(INVALID_FILE_ATTRIBUTES, error);
        auto* state = active_state.load();
        if (!state) return denied(INVALID_FILE_ATTRIBUTES, ERROR_NOT_READY);
        if (!selected(*state, path)) { ++counters.passthrough; SetLastError(incoming); return real_GetFileAttributesW(name); }
        WIN32_FILE_ATTRIBUTE_DATA information{};
        if (!query(*state, path, information, error)) return denied(INVALID_FILE_ATTRIBUTES, error);
        SetLastError(ERROR_SUCCESS); return information.dwFileAttributes;
    });
}
BOOL WINAPI hook_GetFileAttributesExW(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID output) {
    const auto incoming = GetLastError();
    if (internal_bypass) { ++counters.internal_bypass; return real_GetFileAttributesExW(name, level, output); }
    return boundary<BOOL>(FALSE, [&] {
        std::wstring_view path; DWORD error{};
        if (!read_path(name, path, error)) return denied(FALSE, error);
        auto* state = active_state.load();
        if (!state) return denied(FALSE, ERROR_NOT_READY);
        if (!selected(*state, path)) { ++counters.passthrough; SetLastError(incoming); return real_GetFileAttributesExW(name, level, output); }
        if (level != GetFileExInfoStandard || !output) return denied(FALSE, ERROR_INVALID_PARAMETER);
        WIN32_FILE_ATTRIBUTE_DATA information{};
        if (!query(*state, path, information, error)) return denied(FALSE, error);
        *static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(output) = information;
        SetLastError(ERROR_SUCCESS); return TRUE;
    });
}
BOOL WINAPI hook_CreateDirectoryW(LPCWSTR name, LPSECURITY_ATTRIBUTES security) {
    const auto incoming = GetLastError();
    if (internal_bypass) { ++counters.internal_bypass; return real_CreateDirectoryW(name, security); }
    return boundary<BOOL>(FALSE, [&] {
        std::wstring_view path; DWORD error{};
        if (!read_path(name, path, error)) return denied(FALSE, error);
        auto* state = active_state.load();
        if (!state) return denied(FALSE, ERROR_NOT_READY);
        const auto folder = directory(*state, path, true);
        if (folder.action == DirectoryAction::exact) { ++counters.redirected; SetLastError(ERROR_ALREADY_EXISTS); return FALSE; }
        if (folder.action == DirectoryAction::deny || file(*state, path).action != NativePathAction::passthrough)
            return denied(FALSE, ERROR_ACCESS_DENIED);
        ++counters.passthrough; SetLastError(incoming); return real_CreateDirectoryW(name, security);
    });
}
HRESULT WINAPI hook_SHGetFolderPathW(HWND window, int folder, HANDLE token, DWORD flags, LPWSTR output) {
    if (internal_bypass) { ++counters.internal_bypass; return real_SHGetFolderPathW(window, folder, token, flags, output); }
    return boundary<HRESULT>(E_FAIL, [&] {
        if ((folder & 0xff) != CSIDL_APPDATA) { ++counters.passthrough; return real_SHGetFolderPathW(window, folder, token, flags, output); }
        auto* state = active_state.load();
        if (!state || !output || token || (flags != SHGFP_TYPE_CURRENT && flags != SHGFP_TYPE_DEFAULT)) {
            if (output) output[0] = L'\0';
            const DWORD error = state ? ERROR_INVALID_PARAMETER : ERROR_NOT_READY;
            denied(FALSE, error); return HRESULT_FROM_WIN32(error);
        }
        const auto& root = state->private_directories[0].path;
        std::memcpy(output, root.c_str(), (root.size() + 1) * sizeof(wchar_t));
        ++counters.appdata_redirects; ++counters.redirected;
        SetLastError(ERROR_SUCCESS); return S_OK;
    });
}

template<class Result, class Function> Result unsupported_w(LPCWSTR name, Result failure, Function&& original) {
    const auto incoming = GetLastError();
    if (internal_bypass) { ++counters.internal_bypass; return original(); }
    return boundary<Result>(failure, [&] {
        std::wstring_view path; DWORD error{};
        if (!read_path(name, path, error)) return denied(failure, error);
        auto* state = active_state.load();
        if (!state) return denied(failure, ERROR_NOT_READY);
        if (selected(*state, path)) return denied(failure, ERROR_NOT_SUPPORTED);
        ++counters.passthrough; SetLastError(incoming); return original();
    });
}
template<class Result, class Function> Result unsupported_a(LPCSTR name, Result failure, Function&& original) {
    const auto incoming = GetLastError();
    if (internal_bypass) { ++counters.internal_bypass; return original(); }
    return boundary<Result>(failure, [&] {
        std::wstring path; DWORD error{};
        if (!convert_path(name, path, error)) return denied(failure, error);
        auto* state = active_state.load();
        if (!state) return denied(failure, ERROR_NOT_READY);
        if (selected(*state, path)) return denied(failure, ERROR_NOT_SUPPORTED);
        ++counters.passthrough; SetLastError(incoming); return original();
    });
}
template<class Function> BOOL unsupported_pair_w(LPCWSTR from, LPCWSTR to, LPCWSTR backup, Function&& original) {
    const auto incoming = GetLastError();
    if (internal_bypass) { ++counters.internal_bypass; return original(); }
    return boundary<BOOL>(FALSE, [&] {
        std::wstring_view source, target, third; DWORD error{};
        if (!read_path(from, source, error) || !read_path(to, target, error) ||
            (backup && !read_path(backup, third, error))) return denied(FALSE, error);
        auto* state = active_state.load();
        if (!state) return denied(FALSE, ERROR_NOT_READY);
        if (selected(*state, source) || selected(*state, target) || (backup && selected(*state, third))) return denied(FALSE, ERROR_NOT_SUPPORTED);
        ++counters.passthrough; SetLastError(incoming); return original();
    });
}
template<class Function> BOOL unsupported_pair_a(LPCSTR from, LPCSTR to, LPCSTR backup, Function&& original) {
    const auto incoming = GetLastError();
    if (internal_bypass) { ++counters.internal_bypass; return original(); }
    return boundary<BOOL>(FALSE, [&] {
        std::wstring source, target, third; DWORD error{};
        if (!convert_path(from, source, error) || !convert_path(to, target, error) ||
            (backup && !convert_path(backup, third, error))) return denied(FALSE, error);
        auto* state = active_state.load();
        if (!state) return denied(FALSE, ERROR_NOT_READY);
        if (selected(*state, source) || selected(*state, target) || (backup && selected(*state, third))) return denied(FALSE, ERROR_NOT_SUPPORTED);
        ++counters.passthrough; SetLastError(incoming); return original();
    });
}

#define DENY_ONE_W(name, result, failure, parameters, path, arguments) \
    result WINAPI hook_##name parameters { return unsupported_w(path, failure, [&] { return real_##name arguments; }); }
#define DENY_ONE_A(name, result, failure, parameters, path, arguments) \
    result WINAPI hook_##name parameters { return unsupported_a(path, failure, [&] { return real_##name arguments; }); }
DENY_ONE_A(DeleteFileA, BOOL, FALSE, (LPCSTR p), p, (p))
DENY_ONE_A(GetFileAttributesA, DWORD, INVALID_FILE_ATTRIBUTES, (LPCSTR p), p, (p))
DENY_ONE_A(GetFileAttributesExA, BOOL, FALSE, (LPCSTR p, GET_FILEEX_INFO_LEVELS l, LPVOID o), p, (p, l, o))
DENY_ONE_A(CreateDirectoryA, BOOL, FALSE, (LPCSTR p, LPSECURITY_ATTRIBUTES s), p, (p, s))
DENY_ONE_W(RemoveDirectoryW, BOOL, FALSE, (LPCWSTR p), p, (p))
DENY_ONE_A(RemoveDirectoryA, BOOL, FALSE, (LPCSTR p), p, (p))
DENY_ONE_W(FindFirstFileW, HANDLE, INVALID_HANDLE_VALUE, (LPCWSTR p, LPWIN32_FIND_DATAW o), p, (p, o))
DENY_ONE_A(FindFirstFileA, HANDLE, INVALID_HANDLE_VALUE, (LPCSTR p, LPWIN32_FIND_DATAA o), p, (p, o))
DENY_ONE_W(FindFirstFileExW, HANDLE, INVALID_HANDLE_VALUE,
    (LPCWSTR p, FINDEX_INFO_LEVELS l, LPVOID o, FINDEX_SEARCH_OPS s, LPVOID f, DWORD d), p, (p, l, o, s, f, d))
DENY_ONE_A(FindFirstFileExA, HANDLE, INVALID_HANDLE_VALUE,
    (LPCSTR p, FINDEX_INFO_LEVELS l, LPVOID o, FINDEX_SEARCH_OPS s, LPVOID f, DWORD d), p, (p, l, o, s, f, d))
#undef DENY_ONE_W
#undef DENY_ONE_A

BOOL WINAPI hook_CopyFileA(LPCSTR a, LPCSTR b, BOOL f) { return unsupported_pair_a(a, b, nullptr, [&] { return real_CopyFileA(a, b, f); }); }
BOOL WINAPI hook_CopyFileExW(LPCWSTR a, LPCWSTR b, LPPROGRESS_ROUTINE r, LPVOID d, LPBOOL c, DWORD f) {
    return unsupported_pair_w(a, b, nullptr, [&] { return real_CopyFileExW(a, b, r, d, c, f); });
}
BOOL WINAPI hook_CopyFileExA(LPCSTR a, LPCSTR b, LPPROGRESS_ROUTINE r, LPVOID d, LPBOOL c, DWORD f) {
    return unsupported_pair_a(a, b, nullptr, [&] { return real_CopyFileExA(a, b, r, d, c, f); });
}
BOOL WINAPI hook_MoveFileW(LPCWSTR a, LPCWSTR b) { return unsupported_pair_w(a, b, nullptr, [&] { return real_MoveFileW(a, b); }); }
BOOL WINAPI hook_MoveFileA(LPCSTR a, LPCSTR b) { return unsupported_pair_a(a, b, nullptr, [&] { return real_MoveFileA(a, b); }); }
BOOL WINAPI hook_MoveFileExW(LPCWSTR a, LPCWSTR b, DWORD f) {
    // Null destination is a deletion request, not an unrelated second path.
    if (!b) return unsupported_w(a, FALSE, [&] { return real_MoveFileExW(a, b, f); });
    return unsupported_pair_w(a, b, nullptr, [&] { return real_MoveFileExW(a, b, f); });
}
BOOL WINAPI hook_MoveFileExA(LPCSTR a, LPCSTR b, DWORD f) {
    if (!b) return unsupported_a(a, FALSE, [&] { return real_MoveFileExA(a, b, f); });
    return unsupported_pair_a(a, b, nullptr, [&] { return real_MoveFileExA(a, b, f); });
}
BOOL WINAPI hook_ReplaceFileW(LPCWSTR a, LPCWSTR b, LPCWSTR backup, DWORD f, LPVOID e, LPVOID r) {
    return unsupported_pair_w(a, b, backup, [&] { return real_ReplaceFileW(a, b, backup, f, e, r); });
}
BOOL WINAPI hook_ReplaceFileA(LPCSTR a, LPCSTR b, LPCSTR backup, DWORD f, LPVOID e, LPVOID r) {
    return unsupported_pair_a(a, b, backup, [&] { return real_ReplaceFileA(a, b, backup, f, e, r); });
}

template<class Function> LONG detour(Function& original, Function replacement, bool detach) noexcept {
    static_assert(sizeof(Function) == sizeof(PVOID));
    PVOID address{};
    std::memcpy(&address, &replacement, sizeof(address));
    return detach ? DetourDetach(reinterpret_cast<PVOID*>(&original), address) : DetourAttach(reinterpret_cast<PVOID*>(&original), address);
}
LONG configure_detours(bool detach) noexcept {
    LONG status = DetourTransactionBegin();
    if (status != NO_ERROR) return status;
    status = DetourUpdateThread(GetCurrentThread());
#define CHANGE(name) if (status == NO_ERROR) status = detour(real_##name, &hook_##name, detach)
    CHANGE(CreateFileW); CHANGE(CreateFileA); CHANGE(DeleteFileW); CHANGE(DeleteFileA);
    CHANGE(CopyFileW); CHANGE(CopyFileA); CHANGE(CopyFileExW); CHANGE(CopyFileExA);
    CHANGE(GetFileAttributesW); CHANGE(GetFileAttributesA); CHANGE(GetFileAttributesExW); CHANGE(GetFileAttributesExA);
    CHANGE(CreateDirectoryW); CHANGE(CreateDirectoryA); CHANGE(RemoveDirectoryW); CHANGE(RemoveDirectoryA);
    CHANGE(FindFirstFileW); CHANGE(FindFirstFileA); CHANGE(FindFirstFileExW); CHANGE(FindFirstFileExA);
    CHANGE(MoveFileW); CHANGE(MoveFileA); CHANGE(MoveFileExW); CHANGE(MoveFileExA);
    CHANGE(ReplaceFileW); CHANGE(ReplaceFileA); CHANGE(SHGetFolderPathW);
#undef CHANGE
    if (status != NO_ERROR) { DetourTransactionAbort(); return status; }
    return DetourTransactionCommit();
}

DWORD pin(DirectoryPin& result) {
    result.handle = real_CreateFileW((L"\\\\?\\" + result.path).c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (result.handle == INVALID_HANDLE_VALUE) return GetLastError();
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(result.handle, &information)) return GetLastError();
    if (!(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return ERROR_ACCESS_DENIED;
    const auto length = GetFinalPathNameByHandleW(result.handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length > 32768) return ERROR_ACCESS_DENIED;
    std::wstring final(length, L'\0');
    const auto used = GetFinalPathNameByHandleW(result.handle, final.data(), length, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!used || used >= length) return ERROR_ACCESS_DENIED;
    final.resize(used);
    if (!equal_windows(final, L"\\\\?\\" + result.path)) return ERROR_ACCESS_DENIED;
    return ERROR_SUCCESS;
}
NativeIoHookStatus status(NativeIoHookError error = NativeIoHookError::none, DWORD system = ERROR_SUCCESS) noexcept {
    return {error, system, installed.load(), counters.snapshot()};
}
struct Transition {
    bool acquired{!transition.test_and_set(std::memory_order_acquire)};
    ~Transition() { if (acquired) transition.clear(std::memory_order_release); }
};

} // namespace

NativeIoHookStatus install_native_io_hooks(const NativeIoBinding& binding) noexcept {
    Transition operation;
    if (!operation.acquired) return status(NativeIoHookError::busy, ERROR_BUSY);
    if (installed.load() || active_state.load()) return status(NativeIoHookError::already_installed, ERROR_ALREADY_EXISTS);
    try {
        if (!binding.store || binding.policy.destination_mode != DestinationMode::separate_profile ||
            binding.process.process_id != GetCurrentProcessId()) return status(NativeIoHookError::invalid_binding, ERROR_INVALID_PARAMETER);
        const auto primary = join(join(join(binding.policy.appdata_root, L"Sekiro"), binding.account), L"S0000.sl2");
        if (decide_native_save_path(primary, binding.policy, binding.process, binding.account).action != NativePathAction::redirect)
            return status(NativeIoHookError::invalid_binding, ERROR_ACCESS_DENIED);
        if (!binding.store->matches_binding({join(join(binding.policy.coop_profile_root, L"Sekiro"), binding.account),
            join(binding.policy.appdata_root, L"Sekiro"), binding.policy.extension}))
            return status(NativeIoHookError::invalid_binding, ERROR_ACCESS_DENIED);
        auto state = std::make_unique<State>();
        state->binding = binding;
        state->solo_directories[0] = normalized(binding.policy.appdata_root);
        state->solo_directories[1] = join(state->solo_directories[0], L"Sekiro");
        state->solo_directories[2] = join(state->solo_directories[1], binding.account);
        state->private_directories[0].path = normalized(binding.policy.coop_profile_root);
        state->private_directories[1].path = join(state->private_directories[0].path, L"Sekiro");
        state->private_directories[2].path = join(state->private_directories[1].path, binding.account);
        if (state->private_directories[0].path.size() >= MAX_PATH)
            return status(NativeIoHookError::invalid_binding, ERROR_FILENAME_EXCED_RANGE);
        for (auto& folder : state->private_directories) {
            const auto error = pin(folder);
            if (error) return status(NativeIoHookError::directory_validation_failed, error);
        }
        counters.clear();
        active_state.store(state.get());
        const auto result = configure_detours(false);
        if (result != NO_ERROR) {
            active_state.store(nullptr);
            return status(NativeIoHookError::transaction_failed, static_cast<DWORD>(result));
        }
        state.release();
        installed.store(true);
        return status();
    } catch (const std::bad_alloc&) {
        return status(NativeIoHookError::allocation_failure, ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        return status(NativeIoHookError::invalid_binding, ERROR_GEN_FAILURE);
    }
}

NativeIoHookStatus uninstall_native_io_hooks() noexcept {
    Transition operation;
    if (!operation.acquired) return status(NativeIoHookError::busy, ERROR_BUSY);
    if (!installed.load()) return status();
    const auto result = configure_detours(true);
    if (result != NO_ERROR) return status(NativeIoHookError::transaction_failed, static_cast<DWORD>(result));
    installed.store(false);
    delete active_state.exchange(nullptr);
    return status();
}

NativeIoHookStatus native_io_hook_status() noexcept { return status(); }

} // namespace skcoop::saves
