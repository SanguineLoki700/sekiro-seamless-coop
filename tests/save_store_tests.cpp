#include "skcoop/save_store.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winioctl.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace skcoop::saves;
namespace fs = std::filesystem;
struct Skip : std::runtime_error { using std::runtime_error::runtime_error; };
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct NativeHandle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~NativeHandle() { if (value != nullptr && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
fs::path canonical_temporary_parent() {
    const auto requested = fs::temp_directory_path();
    NativeHandle handle{CreateFileW(requested.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    require(handle.value != INVALID_HANDLE_VALUE, "cannot inspect temporary parent");
    const auto needed = GetFinalPathNameByHandleW(handle.value, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    require(needed > 0 && needed < 32768, "temporary parent canonical name unavailable");
    std::wstring resolved(needed, L'\0');
    const auto length = GetFinalPathNameByHandleW(handle.value, resolved.data(), needed,
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    require(length > 0 && length < needed, "temporary parent canonical name failed");
    resolved.resize(length);
    require(resolved.starts_with(L"\\\\?\\") && resolved.size() >= 7 && resolved[5] == L':',
        "temporary fixtures require a local drive");
    resolved.erase(0, 4);
    return fs::path(resolved).lexically_normal();
}
struct Fixture {
    // Hosted Windows runners may expose TEMP through a RUNNER~1 alias. Fixtures
    // use the actual directory name; production alias refusal stays unchanged.
    fs::path parent = canonical_temporary_parent();
    fs::path root;
    Fixture() {
        require(parent.is_absolute(), "temporary parent is not absolute");
        const auto prefix = L"skcoop-store-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        for (int n = 0; n < 100; ++n) {
            auto path = parent / (prefix + L"-" + std::to_wstring(n));
            if (fs::create_directory(path)) { root = path; break; }
        }
        require(!root.empty(), "could not create exclusive fixture root");
        fs::create_directory(root / L"coop-ç-保存");
        fs::create_directory(root / L"protected-個人");
    }
    ~Fixture() {
        if (!root.empty() && root.is_absolute() && root.parent_path() == parent &&
            root.filename().wstring().starts_with(L"skcoop-store-")) {
            std::error_code error; fs::remove_all(root, error);
        }
    }
    SaveStoreConfig config() const { return {(root / L"coop-ç-保存").wstring(), (root / L"protected-個人").wstring(), L"co2"}; }
    fs::path primary() const { return fs::path(config().coop_directory) / L"S0000.co2"; }
    fs::path backup() const { return fs::path(config().coop_directory) / L"S0000.co2.bak"; }
};
const std::vector<std::uint8_t> v1{1, 3, 5, 7, 9};
const std::vector<std::uint8_t> v2{2, 4, 6, 8, 0, 10, 12};
void seed(const fs::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(out), "could not open controlled fixture");
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(out), "could not write controlled fixture");
}
std::vector<std::uint8_t> raw_read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), "could not read controlled fixture");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void no_staging(const Fixture& fixture) {
    for (const auto& entry : fs::directory_iterator(fixture.config().coop_directory))
        require(!entry.path().filename().wstring().starts_with(L".skcoop-save-"), "uncommitted staging file was not cleaned up");
}

void write_backup_restore_exact_bytes() {
    Fixture fixture;
    auto opened = CoOpSaveStore::open(fixture.config());
    if (!opened) std::cerr << "Store open error=" << static_cast<int>(opened.status.error)
                          << " system=" << opened.status.system_error << '\n';
    require(static_cast<bool>(opened), "store open failed");
    auto first = opened.store->write(v1);
    require(first && first.primary_committed && !first.backup_committed, "first write status incorrect");
    auto second = opened.store->write(v2, BackupMode::create_if_missing);
    require(second && second.primary_committed && second.backup_committed, "second write did not preserve previous data");
    auto primary = opened.store->read(SaveFileKind::primary, v2.size());
    auto backup = opened.store->read(SaveFileKind::backup, v1.size());
    require(primary && primary.bytes == v2 && backup && backup.bytes == v1, "primary/backup bytes changed");
    const auto refused = opened.store->write(v1, BackupMode::create_if_missing);
    require(!refused && refused.error == SaveStoreError::backup_exists && !refused.primary_committed,
            "backup was silently overwritten");
    require(raw_read(fixture.primary()) == v2 && raw_read(fixture.backup()) == v1, "backup refusal mutated files");
    const auto restored = opened.store->restore_backup(v1.size());
    require(restored && restored.primary_committed && !restored.backup_committed, "explicit restore failed");
    require(raw_read(fixture.primary()) == v1 && raw_read(fixture.backup()) == v1, "restore did not preserve backup");
    no_staging(fixture);
}

void explicit_backup_replacement() {
    Fixture fixture;
    auto opened = CoOpSaveStore::open(fixture.config());
    require(opened && opened.store->write(v1) && opened.store->write(v2, BackupMode::create_if_missing), "seed failed");
    const auto written = opened.store->write(v1, BackupMode::replace_existing);
    require(written && written.backup_committed && raw_read(fixture.backup()) == v2, "explicit backup replacement failed");
}

void invalid_names_and_roots_fail_without_discovery() {
    Fixture fixture;
    for (const auto extension : {L"sl2", L"SL2", L".co2", L"co2.bak", L"", L"../co2", L"co2:stream"}) {
        auto config = fixture.config(); config.extension = extension;
        auto result = CoOpSaveStore::open(config);
        require(!result && result.status.error == SaveStoreError::invalid_extension, "invalid extension accepted");
    }
    for (const auto root : {L"relative", L"C:relative", L"\\\\?\\C:\\profiles", L"\\\\server\\share", L"C:\\CON\\profile"}) {
        auto config = fixture.config(); config.coop_directory = root;
        require(!CoOpSaveStore::open(config), "ambiguous root accepted");
    }
    auto config = fixture.config(); config.coop_directory += L"\\..\\other";
    require(!CoOpSaveStore::open(config), "traversal root accepted");
    config = fixture.config(); config.coop_directory += L"\\missing";
    require(!CoOpSaveStore::open(config) && !fs::exists(config.coop_directory), "missing root was created implicitly");
    config = fixture.config(); config.extension = std::wstring(121, L'a');
    require(!CoOpSaveStore::open(config), "oversized extension accepted");
    config = fixture.config(); config.extension = L"test2026";
    auto custom = CoOpSaveStore::open(config);
    require(custom && custom.store->write(v1), "validated custom namespace failed");
    require(fs::exists(fs::path(config.coop_directory) / L"S0000.test2026") && !fs::exists(fixture.primary()),
            "custom namespace fell back to co2 or solo");
}

void physical_tree_overlap_is_rejected() {
    Fixture fixture;
    auto config = fixture.config(); config.coop_directory = config.protected_directory;
    auto equal = CoOpSaveStore::open(config);
    require(!equal && equal.status.error == SaveStoreError::overlapping_roots, "same physical directory accepted");
    config = fixture.config(); config.protected_directory = fixture.root.wstring();
    auto ancestor = CoOpSaveStore::open(config);
    require(!ancestor && ancestor.status.error == SaveStoreError::overlapping_roots, "protected ancestor accepted");
    config = fixture.config(); config.coop_directory = fixture.root.wstring();
    auto reverse = CoOpSaveStore::open(config);
    require(!reverse && reverse.status.error == SaveStoreError::overlapping_roots, "co-op ancestor of protected tree accepted");
}

void existing_primary_hardlink_never_truncates_protected_seed() {
    Fixture fixture;
    const auto solo = fs::path(fixture.config().protected_directory) / L"solo.seed";
    seed(solo, v1);
    require(CreateHardLinkW(fixture.primary().c_str(), solo.c_str(), nullptr) != FALSE, "fixture hard link unavailable");
    const auto result = CoOpSaveStore::open(fixture.config());
    require(!result && result.status.error == SaveStoreError::hard_link, "primary hard link accepted");
    require(raw_read(solo) == v1 && raw_read(fixture.primary()) == v1, "primary validation truncated a shared seed");
    no_staging(fixture);
}

void backup_hardlink_added_after_open_is_rejected() {
    Fixture fixture;
    const auto solo = fs::path(fixture.config().protected_directory) / L"solo.seed";
    seed(solo, v2);
    auto opened = CoOpSaveStore::open(fixture.config());
    require(opened && opened.store->write(v1), "store seed failed");
    require(CreateHardLinkW(fixture.backup().c_str(), solo.c_str(), nullptr) != FALSE, "fixture backup hard link unavailable");
    const auto result = opened.store->write(v2, BackupMode::replace_existing);
    require(!result && result.error == SaveStoreError::hard_link, "late backup hard link accepted");
    require(raw_read(solo) == v2 && raw_read(fixture.primary()) == v1, "backup validation changed primary or protected bytes");
    require(!opened.store->restore_backup(), "unsafe backup restored");
    no_staging(fixture);
}

void session_lock_is_exclusive_and_reusable() {
    Fixture fixture;
    auto first = CoOpSaveStore::open(fixture.config());
    require(static_cast<bool>(first), "first session lock failed");
    const auto second = CoOpSaveStore::open(fixture.config());
    require(!second && second.status.error == SaveStoreError::busy, "second session acquired an exclusive store");
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    require(length != 0 && length < 32768, "test executable path unavailable");
    auto command = std::wstring(L"\"") + executable + L"\" --probe-lock \"" + fixture.config().coop_directory +
                   L"\" \"" + fixture.config().protected_directory + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup, &child) != FALSE, "could not launch owned lock probe");
    NativeHandle process{child.hProcess}, thread{child.hThread};
    if (WaitForSingleObject(process.value, 10000) != WAIT_OBJECT_0) {
        TerminateProcess(process.value, 9); // Only the exact child created by this test.
        WaitForSingleObject(process.value, 10000);
        throw std::runtime_error("owned lock probe timed out");
    }
    DWORD exit{};
    require(GetExitCodeProcess(process.value, &exit) && exit == 0, "second process did not observe busy store");
    first.store.reset();
    require(static_cast<bool>(CoOpSaveStore::open(fixture.config())), "stale lock file prevented a new session");
}

void empty_truncated_missing_and_oversized_data_fail() {
    Fixture fixture;
    auto opened = CoOpSaveStore::open(fixture.config());
    require(static_cast<bool>(opened), "store open failed");
    require(!opened.store->read(SaveFileKind::primary), "missing primary returned data");
    require(!opened.store->restore_backup(), "missing backup restored");
    require(!opened.store->write({}), "empty payload accepted");
    require(static_cast<bool>(opened.store->write(v1)), "valid payload rejected");
    auto truncated = opened.store->read(SaveFileKind::primary, v1.size() + 1);
    require(!truncated && truncated.status.error == SaveStoreError::size_mismatch && truncated.bytes.empty(),
            "expected-length truncation was accepted");
    std::vector<std::uint8_t> too_large(max_store_save_bytes + 1, 0x55);
    require(!opened.store->write(too_large) && raw_read(fixture.primary()) == v1, "oversized write changed primary");
    seed(fixture.primary(), {});
    require(!opened.store->read(SaveFileKind::primary), "empty on-disk file accepted");
    NativeHandle file{CreateFileW(fixture.primary().c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr)};
    require(file.value != INVALID_HANDLE_VALUE, "oversized fixture handle failed");
    LARGE_INTEGER end{}; end.QuadPart = static_cast<LONGLONG>(max_store_save_bytes) + 1;
    require(SetFilePointerEx(file.value, end, nullptr, FILE_BEGIN) && SetEndOfFile(file.value), "could not size oversized fixture");
    CloseHandle(file.value); file.value = INVALID_HANDLE_VALUE;
    require(!opened.store->read(SaveFileKind::primary), "oversized on-disk file accepted");
    no_staging(fixture);
}

void failed_commit_preserves_primary_and_removes_staging() {
    Fixture fixture;
    auto opened = CoOpSaveStore::open(fixture.config());
    require(opened && opened.store->write(v1), "seed failed");
    NativeHandle blocker{CreateFileW(fixture.primary().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    require(blocker.value != INVALID_HANDLE_VALUE, "fixture commit blocker failed");
    const auto result = opened.store->write(v2);
    require(!result && result.error == SaveStoreError::commit_failed && !result.primary_committed &&
            !result.backup_committed && result.cleanup_error == 0, "failed commit was reported incorrectly");
    require(raw_read(fixture.primary()) == v1, "failed commit removed or changed primary");
    no_staging(fixture);
}

void reopen_with_empty_primary_allows_explicit_restore() {
    Fixture fixture;
    seed(fixture.primary(), {});
    seed(fixture.backup(), v1);
    auto opened = CoOpSaveStore::open(fixture.config());
    require(static_cast<bool>(opened), "empty primary prevented recovery store open");
    require(!opened.store->read(SaveFileKind::primary), "empty primary read succeeded");
    const auto restored = opened.store->restore_backup(v1.size());
    require(restored && restored.primary_committed && raw_read(fixture.primary()) == v1,
            "valid backup could not recover an empty primary");
    require(raw_read(fixture.backup()) == v1, "recovery altered its backup");
    no_staging(fixture);
}

void partial_pair_commit_is_reported_without_claiming_transaction() {
    Fixture fixture;
    auto opened = CoOpSaveStore::open(fixture.config());
    require(opened && opened.store->write(v1), "seed failed");
    NativeHandle blocker{CreateFileW(fixture.primary().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    require(blocker.value != INVALID_HANDLE_VALUE, "fixture blocker failed");
    const auto result = opened.store->write(v2, BackupMode::create_if_missing);
    require(!result && result.error == SaveStoreError::commit_failed && result.backup_committed &&
            !result.primary_committed && result.cleanup_error == 0, "partial pair commit was hidden");
    require(raw_read(fixture.primary()) == v1 && raw_read(fixture.backup()) == v1, "old data was not recoverable");
    no_staging(fixture);
}

struct JunctionHeader {
    DWORD tag;
    USHORT data_length, reserved;
    USHORT substitute_offset, substitute_length, print_offset, print_length;
    WCHAR path[1];
};
void junction_root_is_rejected_or_explicitly_skipped() {
    Fixture fixture;
    const auto link = fixture.root / L"junction";
    fs::create_directory(link);
    struct RemoveJunction { fs::path path; ~RemoveJunction() { RemoveDirectoryW(path.c_str()); } } cleanup{link};
    NativeHandle directory{CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (directory.value == INVALID_HANDLE_VALUE) throw Skip("junction creation not available on this filesystem/account");
    const auto target = fixture.config().protected_directory;
    const auto substitute = L"\\??\\" + target;
    const auto sub_bytes = static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
    const auto print_bytes = static_cast<USHORT>(target.size() * sizeof(wchar_t));
    JunctionHeader header{};
    header.tag = IO_REPARSE_TAG_MOUNT_POINT;
    header.data_length = static_cast<USHORT>(8 + sub_bytes + sizeof(wchar_t) + print_bytes + sizeof(wchar_t));
    header.substitute_length = sub_bytes;
    header.print_offset = static_cast<USHORT>(sub_bytes + sizeof(wchar_t));
    header.print_length = print_bytes;
    std::vector<std::uint8_t> data(8 + header.data_length, 0);
    static_assert(offsetof(JunctionHeader, path) == 16);
    std::memcpy(data.data(), &header, offsetof(JunctionHeader, path));
    std::memcpy(data.data() + 16, substitute.data(), sub_bytes);
    std::memcpy(data.data() + 16 + header.print_offset, target.data(), print_bytes);
    DWORD returned{};
    if (!DeviceIoControl(directory.value, FSCTL_SET_REPARSE_POINT, data.data(), static_cast<DWORD>(data.size()),
                         nullptr, 0, &returned, nullptr)) throw Skip("real junction creation denied; no reparse assertion claimed");
    CloseHandle(directory.value); directory.value = INVALID_HANDLE_VALUE;
    auto config = fixture.config(); config.coop_directory = link.wstring();
    const auto result = CoOpSaveStore::open(config);
    require(!result && result.status.error == SaveStoreError::reparse_point, "real junction root accepted");
    require(!fs::exists(fs::path(config.protected_directory) / L".skcoop-store.lock"), "junction wrote into protected directory");
}

void native_handles_preserve_partial_writes_and_position() {
    Fixture fixture;
    seed(fixture.primary(), v2);
    auto store = CoOpSaveStore::open(fixture.config());
    require(static_cast<bool>(store), "native store open failed");
    {
        const auto result = store.store->open_native(SaveFileKind::primary,
            {GENERIC_READ | GENERIC_WRITE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL});
        require(result && !result.created && result.last_error == ERROR_ALREADY_EXISTS, "OPEN_ALWAYS existing semantics lost");
        NativeHandle file{result.handle};
        LARGE_INTEGER position{}; position.QuadPart = 2;
        require(SetFilePointerEx(file.value, position, nullptr, FILE_BEGIN) != FALSE, "native seek failed");
        const std::array<std::uint8_t, 2> patch{0xee, 0xdd};
        DWORD transferred{};
        require(WriteFile(file.value, patch.data(), 2, &transferred, nullptr) && transferred == 2, "native partial write failed");
        position.QuadPart = 6;
        require(SetFilePointerEx(file.value, position, nullptr, FILE_BEGIN) != FALSE, "second seek failed");
        const std::uint8_t tail = 77;
        require(WriteFile(file.value, &tail, 1, &transferred, nullptr) && transferred == 1, "second partial write failed");
        require(FlushFileBuffers(file.value) != FALSE, "native flush failed");
        LARGE_INTEGER size{};
        require(GetFileSizeEx(file.value, &size) && size.QuadPart == static_cast<LONGLONG>(v2.size()), "partial write truncated untouched bytes");
        position.QuadPart = 0;
        require(SetFilePointerEx(file.value, position, nullptr, FILE_BEGIN) != FALSE, "read seek failed");
        std::array<std::uint8_t, 7> bytes{};
        require(ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &transferred, nullptr) && transferred == bytes.size(), "native read failed");
        auto expected = v2; expected[2] = 0xee; expected[3] = 0xdd; expected[6] = 77;
        require(std::vector<std::uint8_t>(bytes.begin(), bytes.end()) == expected, "unwritten bytes or offsets changed");
    }
    const auto info = store.store->query_native(SaveFileKind::primary);
    require(info && info.size == v2.size() && !(info.attributes & FILE_ATTRIBUTE_DIRECTORY), "native metadata query mismatch");
}

void native_creation_dispositions_are_explicit() {
    Fixture fixture;
    auto store = CoOpSaveStore::open(fixture.config());
    require(static_cast<bool>(store), "store open failed");
    auto opened = store.store->open_native(SaveFileKind::primary, {GENERIC_WRITE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL});
    require(opened && opened.created && opened.last_error == ERROR_SUCCESS, "missing OPEN_ALWAYS did not create");
    require(CloseHandle(opened.handle) != FALSE, "native close failed");
    seed(fixture.primary(), v1);
    auto collision = store.store->open_native(SaveFileKind::primary, {GENERIC_WRITE, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL});
    require(!collision && collision.last_error == ERROR_FILE_EXISTS && raw_read(fixture.primary()) == v1, "CREATE_NEW overwrote existing file");
    auto missing = store.store->open_native(SaveFileKind::backup, {GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL});
    require(!missing && missing.last_error == ERROR_FILE_NOT_FOUND && !fs::exists(fixture.backup()), "missing OPEN_EXISTING created or fell back");
    auto truncate = store.store->open_native(SaveFileKind::primary, {GENERIC_WRITE, 0, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL});
    require(truncate && !truncate.created, "valid truncate failed");
    require(CloseHandle(truncate.handle) != FALSE && fs::file_size(fixture.primary()) == 0, "truncate did not set length0");
    seed(fixture.primary(), v2);
    auto recreate = store.store->open_native(SaveFileKind::primary, {GENERIC_WRITE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL});
    require(recreate && !recreate.created && recreate.last_error == ERROR_ALREADY_EXISTS, "CREATE_ALWAYS existing status wrong");
    require(CloseHandle(recreate.handle) != FALSE && fs::file_size(fixture.primary()) == 0, "CREATE_ALWAYS did not truncate validated file");
}

void native_hardlinks_are_rejected_before_destructive_access() {
    Fixture fixture;
    const auto solo = fs::path(fixture.config().protected_directory) / L"S0000.sl2";
    seed(solo, v1);
    auto store = CoOpSaveStore::open(fixture.config());
    require(static_cast<bool>(store), "store open failed");
    require(CreateHardLinkW(fixture.primary().c_str(), solo.c_str(), nullptr) != FALSE, "hardlink fixture unavailable");
    for (const DWORD disposition : {CREATE_ALWAYS, TRUNCATE_EXISTING, OPEN_ALWAYS}) {
        auto opened = store.store->open_native(SaveFileKind::primary, {GENERIC_WRITE, 0, disposition, FILE_ATTRIBUTE_NORMAL});
        require(!opened && opened.status.error == SaveStoreError::hard_link && opened.status.system_error != 0 && !opened.handle, "native open followed a hardlink or lost Win32 failure");
        require(raw_read(solo) == v1, "hardlink seed was truncated before validation");
    }
    const auto query = store.store->query_native(SaveFileKind::primary);
    require(!query && query.status.system_error != 0, "native query lost physical rejection error");
    const auto removed = store.store->remove_native(SaveFileKind::primary);
    require(!removed && removed.system_error != 0, "native delete removed linked seed name or lost error");
    require(fs::exists(fixture.primary()) && raw_read(solo) == v1, "hardlink denial changed files");
    require(DeleteFileW(fixture.primary().c_str()) != FALSE, "fixture hardlink cleanup failed");
    seed(fixture.primary(), v2);
    require(CreateHardLinkW(fixture.backup().c_str(), solo.c_str(), nullptr) != FALSE, "backup link fixture failed");
    const auto copied = store.store->copy_native(SaveFileKind::primary, SaveFileKind::backup, false);
    require(!copied && copied.error == SaveStoreError::hard_link && copied.system_error != 0 && raw_read(solo) == v1, "native copy overwrote linked backup or lost error");
}

void native_copy_delete_and_explicit_recovery() {
    Fixture fixture;
    const auto solo = fs::path(fixture.config().protected_directory) / L"S0000.sl2";
    seed(solo, v1); seed(fixture.primary(), v1);
    auto store = CoOpSaveStore::open(fixture.config());
    require(static_cast<bool>(store), "store open failed");
    const auto missing = store.store->query_native(SaveFileKind::backup);
    require(!missing && missing.status.error == SaveStoreError::missing_file, "missing backup query did not stay missing");
    auto copy = store.store->copy_native(SaveFileKind::primary, SaveFileKind::backup, true);
    if (!copy) std::cerr << "Native copy error=" << static_cast<int>(copy.error) << " system=" << copy.system_error << '\n';
    require(copy && copy.backup_committed && raw_read(fixture.backup()) == v1, "native copy to new backup failed");
    auto refused = store.store->copy_native(SaveFileKind::primary, SaveFileKind::backup, true);
    require(!refused && refused.system_error == ERROR_FILE_EXISTS && raw_read(fixture.backup()) == v1, "copy fail-if-exists ignored");
    {
        auto opened = store.store->open_native(SaveFileKind::primary, {GENERIC_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL});
        require(static_cast<bool>(opened), "native write reopen failed"); NativeHandle handle{opened.handle}; DWORD count{};
        require(WriteFile(handle.value, v2.data(), static_cast<DWORD>(v2.size()), &count, nullptr) && count == v2.size(), "native update failed");
    }
    require(raw_read(fixture.backup()) == v1, "native primary update silently replaced backup");
    copy = store.store->copy_native(SaveFileKind::primary, SaveFileKind::backup, false);
    require(copy && raw_read(fixture.backup()) == v2, "native copy replace failed");
    require(static_cast<bool>(store.store->remove_native(SaveFileKind::primary)), "native primary deletion failed");
    require(!fs::exists(fixture.primary()), "deleted primary still exists");
    copy = store.store->copy_native(SaveFileKind::backup, SaveFileKind::primary, true);
    require(copy && copy.primary_committed && raw_read(fixture.primary()) == v2, "explicit native recovery failed");
    require(static_cast<bool>(store.store->remove_native(SaveFileKind::backup)), "native backup deletion failed");
    require(!store.store->query_native(SaveFileKind::backup) && raw_read(solo) == v1, "native cycle modified solo seed or fell back");
}

void native_sharing_and_leaf_pins_are_retained() {
    Fixture fixture; seed(fixture.primary(), v1); seed(fixture.backup(), v2);
    auto store = CoOpSaveStore::open(fixture.config()); require(static_cast<bool>(store), "store open failed");
    {
        const auto opened = store.store->open_native(SaveFileKind::primary, {GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL});
        require(static_cast<bool>(opened), "native reader failed"); NativeHandle file{opened.handle};
        auto writer = store.store->open_native(SaveFileKind::primary, {GENERIC_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL});
        require(!writer && writer.last_error == ERROR_SHARING_VIOLATION, "native sharing was silently weakened");
        require(!DeleteFileW(fixture.primary().c_str()) && !MoveFileExW(fixture.primary().c_str(), (fixture.root / L"moved").c_str(), 0), "returned handle allowed replacement of its leaf");
    }
    {
        const auto opened = store.store->open_native(SaveFileKind::backup, {GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL});
        require(static_cast<bool>(opened), "backup reader failed"); NativeHandle file{opened.handle};
        const auto copy = store.store->copy_native(SaveFileKind::primary, SaveFileKind::backup, false);
        require(!copy && copy.system_error == ERROR_SHARING_VIOLATION && copy.cleanup_error == 0 && copy.destination_may_be_modified, "copy ignored sharing or claimed failed copy guarantees preservation");
        require(raw_read(fixture.backup()) == v2, "failed copy changed existing backup");
    }
}

void native_unsupported_arguments_do_not_touch_files() {
    Fixture fixture; seed(fixture.primary(), v1);
    auto store = CoOpSaveStore::open(fixture.config()); require(static_cast<bool>(store), "store open failed");
    require(store.store->matches_binding(fixture.config()), "store failed to match its pinned roots");
    auto other_binding = fixture.config(); other_binding.protected_directory = fixture.config().coop_directory;
    require(!store.store->matches_binding(other_binding), "wrong protected root accepted as native binding");
    other_binding = fixture.config(); other_binding.extension = L"different";
    require(!store.store->matches_binding(other_binding), "wrong native namespace binding accepted");
    const NativeSaveOpenRequest base{GENERIC_WRITE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL};
    std::vector<NativeSaveOpenRequest> bad;
    auto value = base; value.desired_access = DELETE; bad.push_back(value);
    value = base; value.share_mode = FILE_SHARE_DELETE; bad.push_back(value);
    value = base; value.flags_and_attributes |= FILE_FLAG_OVERLAPPED; bad.push_back(value);
    value = base; value.flags_and_attributes |= FILE_FLAG_DELETE_ON_CLOSE; bad.push_back(value);
    value = base; value.security_attributes_present = true; bad.push_back(value);
    value = base; value.template_file_present = true; bad.push_back(value);
    value = base; value.creation_disposition = 42; bad.push_back(value);
    for (const auto& request : bad) {
        require(!store.store->open_native(SaveFileKind::primary, request), "unsupported native request accepted");
        require(raw_read(fixture.primary()) == v1, "invalid request truncated file");
    }
    const auto query = store.store->open_native(SaveFileKind::primary,
        {0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS});
    require(static_cast<bool>(query), "observed metadata-only open flags refused");
    require(CloseHandle(query.handle) != FALSE, "metadata handle close failed");
    require(!store.store->open_native(static_cast<SaveFileKind>(9), base), "invalid file kind accepted");
    require(!store.store->copy_native(SaveFileKind::primary, SaveFileKind::primary, false), "copy to same physical file accepted");
}

void short_path_alias_is_rejected_or_explicitly_skipped() {
    Fixture fixture;
    auto config = fixture.config();
    const auto needed = GetShortPathNameW(config.coop_directory.c_str(), nullptr, 0);
    if (needed == 0) throw Skip("8.3 name unavailable");
    std::wstring short_path(needed, L'\0');
    const auto length = GetShortPathNameW(config.coop_directory.c_str(), short_path.data(), needed);
    if (length == 0 || length >= needed) throw Skip("8.3 query unavailable");
    short_path.resize(length);
    if (short_path == config.coop_directory) throw Skip("8.3 aliases disabled for fixture directory");
    config.coop_directory = short_path;
    const auto result = CoOpSaveStore::open(config);
    require(!result && result.status.error == SaveStoreError::alias_path, "short-path alias accepted");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring_view(argv[1]) == L"--probe-lock") {
        const auto result = CoOpSaveStore::open({argv[2], argv[3], L"co2"});
        return !result && result.status.error == SaveStoreError::busy ? 0 : 4;
    }
    const std::array<std::pair<std::string_view, std::function<void()>>, 19> cases{{
        {"write_backup_restore_exact_bytes", write_backup_restore_exact_bytes},
        {"explicit_backup_replacement", explicit_backup_replacement},
        {"invalid_names_and_roots_fail_without_discovery", invalid_names_and_roots_fail_without_discovery},
        {"physical_tree_overlap_is_rejected", physical_tree_overlap_is_rejected},
        {"existing_primary_hardlink_never_truncates_protected_seed", existing_primary_hardlink_never_truncates_protected_seed},
        {"backup_hardlink_added_after_open_is_rejected", backup_hardlink_added_after_open_is_rejected},
        {"session_lock_is_exclusive_and_reusable", session_lock_is_exclusive_and_reusable},
        {"empty_truncated_missing_and_oversized_data_fail", empty_truncated_missing_and_oversized_data_fail},
        {"failed_commit_preserves_primary_and_removes_staging", failed_commit_preserves_primary_and_removes_staging},
        {"reopen_with_empty_primary_allows_explicit_restore", reopen_with_empty_primary_allows_explicit_restore},
        {"partial_pair_commit_is_reported_without_claiming_transaction", partial_pair_commit_is_reported_without_claiming_transaction},
        {"junction_root_is_rejected_or_explicitly_skipped", junction_root_is_rejected_or_explicitly_skipped},
        {"short_path_alias_is_rejected_or_explicitly_skipped", short_path_alias_is_rejected_or_explicitly_skipped},
        {"native_handles_preserve_partial_writes_and_position", native_handles_preserve_partial_writes_and_position},
        {"native_creation_dispositions_are_explicit", native_creation_dispositions_are_explicit},
        {"native_hardlinks_are_rejected_before_destructive_access", native_hardlinks_are_rejected_before_destructive_access},
        {"native_copy_delete_and_explicit_recovery", native_copy_delete_and_explicit_recovery},
        {"native_sharing_and_leaf_pins_are_retained", native_sharing_and_leaf_pins_are_retained},
        {"native_unsupported_arguments_do_not_touch_files", native_unsupported_arguments_do_not_touch_files},
    }};
    std::size_t failed{}, skipped{}, passed{};
    for (const auto& [name, test] : cases) {
        try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const Skip& error) { ++skipped; std::cout << "SKIP " << name << ": " << error.what() << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    std::cout << "SAVE_STORE_RESULT " << (failed ? "FAIL" : "PASS") << " cases=" << cases.size()
              << " passed=" << passed << " skipped=" << skipped << " failed=" << failed << '\n';
    return failed ? 1 : 0;
}
