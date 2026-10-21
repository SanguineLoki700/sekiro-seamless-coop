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
struct Fixture {
    fs::path parent = fs::temp_directory_path().lexically_normal();
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
    const std::array<std::pair<std::string_view, std::function<void()>>, 13> cases{{
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
