#include "skcoop/save_store.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winternl.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

namespace skcoop::saves {
namespace {
class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { reset(); value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE); }
        return *this;
    }
    void reset() noexcept {
        if (*this) CloseHandle(value_);
        value_ = INVALID_HANDLE_VALUE;
    }
    explicit operator bool() const noexcept { return value_ != nullptr && value_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

SaveStoreStatus fail(SaveStoreError error, DWORD system = ERROR_SUCCESS) { return {error, system, false, false}; }

bool equal_windows(std::wstring_view a, std::wstring_view b) noexcept {
    return a.size() == b.size() && (a.empty() || CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
        b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL);
}
bool separator(wchar_t value) noexcept { return value == L'\\' || value == L'/'; }
bool reserved_name(std::wstring_view component) noexcept {
    const auto stem = component.substr(0, component.find(L'.'));
    for (const auto name : {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$"})
        if (equal_windows(stem, name)) return true;
    return stem.size() == 4 &&
        (equal_windows(stem.substr(0, 3), L"COM") || equal_windows(stem.substr(0, 3), L"LPT")) &&
        ((stem[3] >= L'1' && stem[3] <= L'9') || stem[3] == L'\u00b9' || stem[3] == L'\u00b2' || stem[3] == L'\u00b3');
}

// The lexical policy is conservative. Physical checks below additionally reject
// 8.3 aliases, reparse points, volume aliases and overlapping directory identities.
bool normalize_directory(std::wstring_view input, std::wstring& output) {
    if (input.size() < 4 || input.size() > 32000 || input[1] != L':' || !separator(input[2]) ||
        !((input[0] >= L'A' && input[0] <= L'Z') || (input[0] >= L'a' && input[0] <= L'z'))) return false;
    std::size_t end = input.size();
    if (separator(input.back())) {
        --end;
        if (separator(input[end - 1])) return false;
    }
    output.assign(input.substr(0, 3));
    output[0] = input[0] >= L'a' && input[0] <= L'z' ? static_cast<wchar_t>(input[0] - L'a' + L'A') : input[0];
    output[2] = L'\\';
    for (std::size_t start = 3; start < end;) {
        auto at = input.find_first_of(L"\\/", start);
        if (at == std::wstring_view::npos || at > end) at = end;
        const auto part = input.substr(start, at - start);
        if (part.empty() || part.size() > 255 || part == L"." || part == L".." ||
            part.back() == L'.' || part.back() == L' ' || reserved_name(part)) return false;
        for (std::size_t i = 0; i < part.size(); ++i) {
            const auto unit = static_cast<std::uint16_t>(part[i]);
            if (unit < 32 || std::wstring_view(L"<>:\"/\\|?*").find(part[i]) != std::wstring_view::npos) return false;
            if (unit >= 0xd800 && unit <= 0xdbff) {
                if (++i == part.size()) return false;
                const auto low = static_cast<std::uint16_t>(part[i]);
                if (low < 0xdc00 || low > 0xdfff) return false;
            } else if (unit >= 0xdc00 && unit <= 0xdfff) return false;
        }
        if (output.size() > 3) output.push_back(L'\\');
        output += part;
        start = at + 1;
    }
    return output.size() > 3;
}

bool valid_extension(std::wstring_view value) noexcept {
    return !value.empty() && value.size() <= 120 && !equal_windows(value, L"sl2") &&
        std::all_of(value.begin(), value.end(), [](wchar_t c) {
            return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9');
        });
}

std::wstring final_path(HANDLE handle) {
    const auto size = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (size == 0 || size > 32768) return {};
    std::wstring result(size, L'\0');
    const auto length = GetFinalPathNameByHandleW(handle, result.data(), size, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (length == 0 || length >= size) return {};
    result.resize(length);
    if (!result.starts_with(L"\\\\?\\") || result.size() < 7 || result[5] != L':') return {};
    result.erase(0, 4);
    return result;
}

struct FileIdentity {
    DWORD volume{};
    DWORD high{};
    DWORD low{};
    bool operator==(const FileIdentity&) const = default;
};
FileIdentity identity(const BY_HANDLE_FILE_INFORMATION& info) noexcept {
    return {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow};
}
struct PinnedDirectory {
    std::wstring path;
    std::vector<Handle> handles;
    std::vector<FileIdentity> identities;
    HANDLE leaf() const noexcept { return handles.back().get(); }
};

SaveStoreStatus pin_directory(std::wstring_view input, PinnedDirectory& result, bool writable) {
    if (!normalize_directory(input, result.path)) return fail(SaveStoreError::invalid_configuration);
    std::vector<std::size_t> boundaries{3};
    for (std::size_t at = 3; at < result.path.size(); ++at)
        if (result.path[at] == L'\\') boundaries.push_back(at);
    boundaries.push_back(result.path.size());
    for (const auto end : boundaries) {
        const auto prefix = result.path.substr(0, end);
        const bool leaf = end == result.path.size();
        const DWORD access = FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE |
            ((leaf && writable) ? (FILE_LIST_DIRECTORY | FILE_ADD_FILE | FILE_DELETE_CHILD) : 0u);
        Handle handle(CreateFileW((L"\\\\?\\" + prefix).c_str(), access,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!handle) return fail(SaveStoreError::io_error, GetLastError());
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle.get(), &info)) return fail(SaveStoreError::io_error, GetLastError());
        if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return fail(SaveStoreError::reparse_point);
        if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return fail(SaveStoreError::invalid_configuration);
        if (!equal_windows(final_path(handle.get()), prefix)) return fail(SaveStoreError::alias_path);
        result.identities.push_back(identity(info));
        result.handles.push_back(std::move(handle));
    }
    wchar_t filesystem[32]{};
    if (!GetVolumeInformationByHandleW(result.leaf(), nullptr, 0, nullptr, nullptr, nullptr, filesystem, 32))
        return fail(SaveStoreError::io_error, GetLastError());
    if (!equal_windows(filesystem, L"NTFS")) return fail(SaveStoreError::unsupported_filesystem);
    return {};
}

using NtCreateFileFunction = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
    PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
using NtSetInformationFileFunction = NTSTATUS(NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
using NtStatusToDosFunction = ULONG(NTAPI*)(NTSTATUS);
struct NativeApi {
    NtCreateFileFunction create{};
    NtSetInformationFileFunction set_information{};
    NtStatusToDosFunction dos_error{};
    NativeApi() {
        const auto module = GetModuleHandleW(L"ntdll.dll");
        if (!module) return;
        // Copy function pointers rather than suppressing MSVC's FARPROC cast warning.
        const auto create_address = GetProcAddress(module, "NtCreateFile");
        const auto set_address = GetProcAddress(module, "NtSetInformationFile");
        const auto error_address = GetProcAddress(module, "RtlNtStatusToDosError");
        static_assert(sizeof(create) == sizeof(create_address));
        std::memcpy(&create, &create_address, sizeof(create));
        std::memcpy(&set_information, &set_address, sizeof(set_information));
        std::memcpy(&dos_error, &error_address, sizeof(dos_error));
    }
    bool available() const noexcept { return create && set_information && dos_error; }
};
const NativeApi& native() { static const NativeApi value; return value; }

SaveStoreStatus open_child(HANDLE directory, std::wstring_view name, ACCESS_MASK access,
                          ULONG share, ULONG disposition, Handle& output) {
    UNICODE_STRING filename{};
    filename.Buffer = const_cast<PWSTR>(name.data());
    filename.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    filename.MaximumLength = filename.Length;
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = directory;
    attributes.ObjectName = &filename;
    attributes.Attributes = OBJ_CASE_INSENSITIVE;
    IO_STATUS_BLOCK io{};
    HANDLE raw{};
    // FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT.
    const auto status = native().create(&raw, access, &attributes, &io, nullptr, FILE_ATTRIBUTE_NORMAL,
        share, disposition, 0x20u | 0x40u | 0x00200000u, nullptr, 0);
    if (status < 0) {
        const auto error = native().dos_error(status);
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return fail(SaveStoreError::missing_file, error);
        if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) return fail(SaveStoreError::busy, error);
        return fail(SaveStoreError::io_error, error);
    }
    output = Handle(raw);
    return {};
}

struct CheckedFile {
    Handle handle;
    std::uint64_t size{};
};

SaveStoreStatus inspect_file(HANDLE handle, std::wstring_view expected_path, bool bounded, std::uint64_t& size) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) return fail(SaveStoreError::io_error, GetLastError());
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return fail(SaveStoreError::reparse_point);
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return fail(SaveStoreError::invalid_configuration);
    if (info.nNumberOfLinks != 1) return fail(SaveStoreError::hard_link);
    if (!equal_windows(final_path(handle), expected_path)) return fail(SaveStoreError::alias_path);
    size = (std::uint64_t{info.nFileSizeHigh} << 32) | info.nFileSizeLow;
    if (bounded && (size == 0 || size > max_store_save_bytes)) return fail(SaveStoreError::invalid_size);
    return {};
}

SaveStoreReadResult read_checked(CheckedFile& file, std::optional<std::size_t> expected) {
    if (!file.handle) return {fail(SaveStoreError::missing_file), {}};
    if (expected && (*expected == 0 || *expected > max_store_save_bytes)) return {fail(SaveStoreError::invalid_size), {}};
    if (expected && file.size != *expected) return {fail(SaveStoreError::size_mismatch), {}};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file.size));
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(1024u * 1024u, bytes.size() - offset));
        DWORD read{};
        if (!ReadFile(file.handle.get(), bytes.data() + offset, chunk, &read, nullptr))
            return {fail(SaveStoreError::io_error, GetLastError()), {}};
        if (read == 0) return {fail(SaveStoreError::size_mismatch, ERROR_HANDLE_EOF), {}};
        offset += read;
    }
    return {{}, std::move(bytes)};
}

struct PendingFile {
    Handle handle;
    bool committed{};
    ~PendingFile() {
        if (handle && !committed) discard();
    }
    DWORD discard() noexcept {
        DWORD error = ERROR_SUCCESS;
        if (handle) {
            FILE_DISPOSITION_INFO remove{TRUE};
            if (!SetFileInformationByHandle(handle.get(), FileDispositionInfo, &remove, sizeof(remove)))
                error = GetLastError();
            handle.reset();
        }
        return error;
    }
};
struct NativeRename {
    BOOLEAN replace;
    HANDLE root_directory;
    ULONG filename_length;
    WCHAR filename[1];
};
static_assert(offsetof(NativeRename, root_directory) == 8 && offsetof(NativeRename, filename) == 20);
std::atomic<std::uint64_t> temporary_sequence{};

SaveStoreStatus atomic_write(HANDLE directory, std::wstring_view directory_path,
                            std::wstring_view name, std::span<const std::uint8_t> bytes, bool replace) {
    PendingFile pending;
    const auto abort = [&](SaveStoreStatus status) {
        status.cleanup_error = pending.discard();
        return status;
    };
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const auto temporary = L".skcoop-save-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++temporary_sequence) + L".tmp";
        auto opened = open_child(directory, temporary, GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                                 0, 2u, pending.handle); // FILE_CREATE, never truncate an existing name.
        if (!opened && (opened.system_error == ERROR_FILE_EXISTS || opened.system_error == ERROR_ALREADY_EXISTS)) continue;
        if (!opened) return abort(opened);
        std::uint64_t size{};
        auto inspected = inspect_file(pending.handle.get(), std::wstring(directory_path) + L"\\" + temporary, false, size);
        if (!inspected) return abort(inspected);
        break;
    }
    if (!pending.handle) return abort(fail(SaveStoreError::io_error, ERROR_FILE_EXISTS));
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(1024u * 1024u, bytes.size() - offset));
        DWORD written{};
        if (!WriteFile(pending.handle.get(), bytes.data() + offset, chunk, &written, nullptr))
            return abort(fail(SaveStoreError::io_error, GetLastError()));
        if (written == 0) return abort(fail(SaveStoreError::io_error, ERROR_WRITE_FAULT));
        offset += written;
    }
    if (!FlushFileBuffers(pending.handle.get())) return abort(fail(SaveStoreError::io_error, GetLastError()));
    BY_HANDLE_FILE_INFORMATION metadata{};
    if (!GetFileInformationByHandle(pending.handle.get(), &metadata)) return abort(fail(SaveStoreError::io_error, GetLastError()));
    if (metadata.nNumberOfLinks != 1 || (metadata.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return abort(fail(SaveStoreError::hard_link));
    const auto length = offsetof(NativeRename, filename) + name.size() * sizeof(wchar_t);
    std::vector<std::uint8_t> storage(length, 0);
    NativeRename header{};
    header.replace = replace ? TRUE : FALSE;
    header.root_directory = directory;
    header.filename_length = static_cast<ULONG>(name.size() * sizeof(wchar_t));
    std::memcpy(storage.data(), &header, offsetof(NativeRename, filename));
    std::memcpy(storage.data() + offsetof(NativeRename, filename), name.data(), name.size() * sizeof(wchar_t));
    IO_STATUS_BLOCK io{};
    const auto status = native().set_information(pending.handle.get(), &io, storage.data(), static_cast<ULONG>(length),
        static_cast<FILE_INFORMATION_CLASS>(10)); // FileRenameInformation; relative to pinned directory.
    if (status < 0) return abort(fail(SaveStoreError::commit_failed, native().dos_error(status)));
    pending.committed = true;
    return {};
}
} // namespace

struct CoOpSaveStore::Impl {
    PinnedDirectory coop;
    PinnedDirectory protected_tree;
    Handle lease;
    std::wstring primary;
    std::wstring backup;
    std::mutex mutex;

    SaveStoreStatus inspect(std::wstring_view name, CheckedFile& file, bool bounded) {
        auto opened = open_child(coop.leaf(), name, GENERIC_READ | SYNCHRONIZE, FILE_SHARE_READ, 1u, file.handle);
        if (!opened && opened.error == SaveStoreError::missing_file) return {};
        if (!opened) return opened;
        return inspect_file(file.handle.get(), coop.path + L"\\" + std::wstring(name), bounded, file.size);
    }
    SaveStoreStatus inspect_both(CheckedFile& primary_file, CheckedFile& backup_file,
                                bool bounded_primary = true, bool bounded_backup = true) {
        auto status = inspect(primary, primary_file, bounded_primary);
        if (!status) return status;
        return inspect(backup, backup_file, bounded_backup);
    }
};

CoOpSaveStore::CoOpSaveStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
CoOpSaveStore::~CoOpSaveStore() = default;

SaveStoreOpenResult CoOpSaveStore::open(const SaveStoreConfig& config) {
    if (!valid_extension(config.extension)) return {fail(SaveStoreError::invalid_extension), {}};
    if (!native().available()) return {fail(SaveStoreError::io_error, ERROR_PROC_NOT_FOUND), {}};
    auto impl = std::make_unique<Impl>();
    auto status = pin_directory(config.coop_directory, impl->coop, true);
    if (!status) return {status, {}};
    status = pin_directory(config.protected_directory, impl->protected_tree, false);
    if (!status) return {status, {}};
    const auto coop_leaf = impl->coop.identities.back();
    const auto protected_leaf = impl->protected_tree.identities.back();
    if (std::find(impl->protected_tree.identities.begin(), impl->protected_tree.identities.end(), coop_leaf) != impl->protected_tree.identities.end() ||
        std::find(impl->coop.identities.begin(), impl->coop.identities.end(), protected_leaf) != impl->coop.identities.end())
        return {fail(SaveStoreError::overlapping_roots), {}};
    impl->primary = L"S0000." + config.extension;
    impl->backup = impl->primary + L".bak";
    status = open_child(impl->coop.leaf(), L".skcoop-store.lock", GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                        0, 3u, impl->lease); // FILE_OPEN_IF, never truncate a stale lease file.
    if (!status) return {status, {}};
    std::uint64_t size{};
    status = inspect_file(impl->lease.get(), impl->coop.path + L"\\.skcoop-store.lock", false, size);
    if (!status) return {status, {}};
    CheckedFile primary_file, backup_file;
    // Opening validates namespace safety without rejecting a corrupt primary
    // that a later explicit restore is meant to replace.
    status = impl->inspect_both(primary_file, backup_file, false, false);
    if (!status) return {status, {}};
    return {{}, std::unique_ptr<CoOpSaveStore>(new CoOpSaveStore(std::move(impl)))};
}

SaveStoreReadResult CoOpSaveStore::read(SaveFileKind kind, std::optional<std::size_t> expected_size) {
    std::lock_guard lock(impl_->mutex);
    if (kind != SaveFileKind::primary && kind != SaveFileKind::backup)
        return {fail(SaveStoreError::invalid_configuration), {}};
    CheckedFile primary, backup;
    const auto status = impl_->inspect_both(primary, backup,
        kind == SaveFileKind::primary, kind == SaveFileKind::backup);
    if (!status) return {status, {}};
    return read_checked(kind == SaveFileKind::primary ? primary : backup, expected_size);
}

SaveStoreStatus CoOpSaveStore::write(std::span<const std::uint8_t> bytes, BackupMode mode) {
    std::lock_guard lock(impl_->mutex);
    if (bytes.empty() || bytes.size() > max_store_save_bytes) return fail(SaveStoreError::invalid_size);
    if (mode != BackupMode::none && mode != BackupMode::create_if_missing && mode != BackupMode::replace_existing)
        return fail(SaveStoreError::invalid_configuration);
    CheckedFile primary, backup;
    auto status = impl_->inspect_both(primary, backup);
    if (!status) return status;
    bool backup_committed = false;
    if (primary.handle && mode != BackupMode::none) {
        if (backup.handle && mode == BackupMode::create_if_missing) return fail(SaveStoreError::backup_exists);
        auto previous = read_checked(primary, {});
        if (!previous) return previous.status;
        backup.handle.reset();
        status = atomic_write(impl_->coop.leaf(), impl_->coop.path, impl_->backup, previous.bytes,
                              mode == BackupMode::replace_existing);
        if (!status) return status;
        backup_committed = true;
    }
    primary.handle.reset();
    status = atomic_write(impl_->coop.leaf(), impl_->coop.path, impl_->primary, bytes, true);
    status.backup_committed = backup_committed;
    status.primary_committed = static_cast<bool>(status);
    return status;
}

SaveStoreStatus CoOpSaveStore::restore_backup(std::optional<std::size_t> expected_size) {
    std::lock_guard lock(impl_->mutex);
    CheckedFile primary, backup;
    auto status = impl_->inspect_both(primary, backup, false, true);
    if (!status) return status;
    auto recovery = read_checked(backup, expected_size);
    if (!recovery) return recovery.status;
    primary.handle.reset();
    status = atomic_write(impl_->coop.leaf(), impl_->coop.path, impl_->primary, recovery.bytes, true);
    status.primary_committed = static_cast<bool>(status);
    return status;
}

} // namespace skcoop::saves
