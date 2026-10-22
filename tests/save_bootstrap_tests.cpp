#include "skcoop/save_bootstrap.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr WCHAR account[] = L"12345678901234567";
void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
std::filesystem::path physical_directory(const WCHAR* path) {
    const auto handle = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    require(handle != INVALID_HANDLE_VALUE, "Cannot resolve test parent handle");
    WCHAR actual[32768]{};
    const DWORD length = GetFinalPathNameByHandleW(handle, actual, static_cast<DWORD>(sizeof(actual) / sizeof(actual[0])),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    const BOOL closed = CloseHandle(handle);
    require(length > 4 && length < sizeof(actual) / sizeof(actual[0]) && closed, "Cannot resolve physical test parent");
    return std::filesystem::path(std::wstring_view(actual).starts_with(L"\\\\?\\") ? actual + 4 : actual);
}
std::vector<unsigned char> bytes(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    require(static_cast<bool>(input), "Cannot read fixture sentinel");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void create_sentinel(const std::filesystem::path& file, const std::array<unsigned char, 512>& seed) {
    const auto handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(handle != INVALID_HANDLE_VALUE, "Cannot exclusively create fixture sentinel");
    DWORD written = 0;
    const BOOL ok = WriteFile(handle, seed.data(), static_cast<DWORD>(seed.size()), &written, nullptr);
    const BOOL closed = CloseHandle(handle);
    require(ok && written == seed.size() && closed, "Cannot write fixture sentinel");
}
struct Tree {
    std::filesystem::path root;
    [[nodiscard]] std::filesystem::path solo() const { return root / L"solo" / L"Sekiro" / account; }
    [[nodiscard]] std::filesystem::path coop() const { return root / L"coop" / L"Sekiro" / account; }
    Tree() {
        WCHAR temporary[32768]{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(sizeof(temporary) / sizeof(temporary[0])), temporary);
        require(length != 0 && length < sizeof(temporary) / sizeof(temporary[0]),
            "Cannot find test temporary directory");
        const auto parent = physical_directory(temporary);
        for (unsigned i = 0; i < 32; ++i) {
            root = parent / (L"skcoop-bootstrap-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(i));
            if (CreateDirectoryW(root.c_str(), nullptr)) break;
            require(GetLastError() == ERROR_ALREADY_EXISTS, "Cannot create fixture test tree");
            root.clear();
        }
        require(!root.empty(), "Cannot choose unique fixture directory");
        require(CreateDirectoryW((root / L"solo").c_str(), nullptr) &&
            CreateDirectoryW((root / L"coop").c_str(), nullptr), "Cannot create fixture namespaces");
        require(CreateDirectoryW((root / L"solo" / L"Sekiro").c_str(), nullptr) &&
            CreateDirectoryW((root / L"coop" / L"Sekiro").c_str(), nullptr) &&
            CreateDirectoryW(solo().c_str(), nullptr) && CreateDirectoryW(coop().c_str(), nullptr),
            "Cannot create fixture account directories");
    }
    ~Tree() {
        // Explicit files/directories only: no recursive delete of a computed path.
        DeleteFileW((solo() / L"S0000.sl2").c_str());
        DeleteFileW((solo() / L"S0000.sl2.bak").c_str());
        DeleteFileW((coop() / L"S0000.co2").c_str());
        DeleteFileW((coop() / L"S0000.co2.bak").c_str());
        DeleteFileW((coop() / L".skcoop-store.lock").c_str());
        RemoveDirectoryW(solo().c_str());
        RemoveDirectoryW(coop().c_str());
        RemoveDirectoryW((root / L"solo" / L"Sekiro").c_str());
        RemoveDirectoryW((root / L"coop" / L"Sekiro").c_str());
        RemoveDirectoryW((root / L"solo").c_str());
        RemoveDirectoryW((root / L"coop").c_str());
        RemoveDirectoryW(root.c_str());
    }
};
}

int wmain(int argc, wchar_t** argv) {
    // This isolated test process owns its children. Avoid loader-error dialogs
    // for the deliberate DllMain(FALSE) cases; no desktop settings are changed.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try {
        std::filesystem::path fixture, bootstrap;
        if (argc == 3) { fixture = argv[1]; bootstrap = argv[2]; }
        else if (argc == 5 && std::wstring_view(argv[1]) == L"--fixture" && std::wstring_view(argv[3]) == L"--bootstrap") {
            fixture = argv[2]; bootstrap = argv[4];
        } else throw std::runtime_error("Expected fixture and bootstrap DLL paths");
        fixture = std::filesystem::canonical(fixture);
        bootstrap = std::filesystem::canonical(bootstrap);
        require(_wcsicmp(fixture.filename().c_str(), SKCOOP_BOOTSTRAP_FIXTURE_NAME) == 0, "Fixture target name rejected");
        require(_wcsicmp(bootstrap.filename().c_str(), SKCOOP_BOOTSTRAP_DLL_NAME) == 0, "Bootstrap target name rejected");
        Tree tree;
        std::array<unsigned char, 512> seed{};
        for (size_t i = 0; i < seed.size(); ++i) seed[i] = static_cast<unsigned char>((i * 37u + 11u) & 255u);
        create_sentinel(tree.solo() / L"S0000.sl2", seed);
        create_sentinel(tree.solo() / L"S0000.sl2.bak", seed);
        const std::vector<unsigned char> expected(seed.begin(), seed.end());
        unsigned passed = 0;
        const auto run = [&](uint32_t flags) {
            skcoop_bootstrap_run_result result{};
            result.struct_size = sizeof(result);
            require(skcoop_run_bootstrap_fixture(fixture.c_str(), bootstrap.c_str(), tree.root.c_str(), flags, 10000, &result),
                "Bootstrap host failed to observe/clean its owned fixture");
            require(result.process_observed && !result.timed_out && result.host_error == ERROR_SUCCESS,
                "Fixture process result incomplete");
            require(bytes(tree.solo() / L"S0000.sl2") == expected &&
                bytes(tree.solo() / L"S0000.sl2.bak") == expected, "Solo sentinel changed");
            require(std::filesystem::is_empty(tree.coop()), "Deny-only bootstrap unexpectedly wrote coop files");
            return result;
        };
        const auto normal = run(SKCOOP_BOOTSTRAP_TEST_NORMAL);
        require(normal.ready_signaled && normal.process_exit_code == 0 && normal.status.phase == SKCOOP_BOOTSTRAP_READY,
            "Bootstrap did not become ready");
        require(normal.status.checkpoint_mask == 7 && normal.status.installed_hooks == 1 &&
            normal.status.denied_opens == 3 && normal.status.unexpected_results == 0, "Missing startup interception");
        require(normal.status.ready_sequence > 0 && normal.status.ready_sequence < normal.status.tls_sequence &&
            normal.status.tls_sequence < normal.status.global_sequence &&
            normal.status.global_sequence < normal.status.main_sequence, "Bootstrap was not before TLS/global/main");
        ++passed;
        for (const uint32_t flags : {SKCOOP_BOOTSTRAP_TEST_NO_PAYLOAD, SKCOOP_BOOTSTRAP_TEST_TRUNCATED_PAYLOAD,
            SKCOOP_BOOTSTRAP_TEST_BAD_VERSION, SKCOOP_BOOTSTRAP_TEST_BAD_NONCE}) {
            const auto result = run(flags);
            require(result.process_exit_code == 0xc0000142u && result.status.checkpoint_mask == 0 &&
                result.status.denied_opens == 0 && result.status.installed_hooks == 0,
                "Invalid configuration reached executable startup code");
            ++passed;
        }
        const auto aborted = run(SKCOOP_BOOTSTRAP_TEST_ABORT_TRANSACTION);
        require(aborted.process_exit_code == 0xc0000142u && aborted.status.phase == SKCOOP_BOOTSTRAP_FAILED &&
            aborted.status.transaction_aborted == 1 && aborted.status.installed_hooks == 0 &&
            aborted.status.checkpoint_mask == 0 && aborted.status.denied_opens == 0,
            "Intermediate attach failure did not abort before startup");
        ++passed;
        for (const bool seeded : {false, true}) {
            Tree native;
            create_sentinel(native.solo() / L"S0000.sl2", seed);
            create_sentinel(native.solo() / L"S0000.sl2.bak", seed);
            if (seeded) create_sentinel(native.coop() / L"S0000.co2", seed);
            skcoop_bootstrap_run_result result{};
            result.struct_size = sizeof(result);
            require(skcoop_run_bootstrap_fixture(fixture.c_str(), bootstrap.c_str(), native.root.c_str(),
                SKCOOP_BOOTSTRAP_TEST_NATIVE_PROFILE, 10000, &result), "Native-profile host failed");
            if (result.process_exit_code != 0 || result.status.phase != SKCOOP_BOOTSTRAP_READY)
                std::cerr << "Native fixture: exit=" << result.process_exit_code << " phase=" << result.status.phase
                    << " error=" << result.status.system_error << " checkpoints=" << result.status.checkpoint_mask << '\n';
            require(result.process_observed && result.ready_signaled && result.process_exit_code == 0 &&
                result.status.phase == SKCOOP_BOOTSTRAP_READY &&
                result.status.profile_mode == SKCOOP_BOOTSTRAP_NATIVE_PROFILE, "Native-profile fixture did not complete");
            require(result.status.checkpoint_mask == 15 && result.status.installed_hooks == 1 &&
                result.status.unexpected_results == 0 && result.status.native_redirected > 10 &&
                // The sole expected selected failure is GetFileAttributes after deleting the primary.
                result.status.native_denied == 1 && result.status.native_copy_failures == 0 &&
                result.status.native_destination_may_be_modified == 0, "Native hooks did not route the complete fixture");
            require(result.status.ready_sequence < result.status.tls_sequence &&
                result.status.tls_sequence < result.status.global_sequence &&
                result.status.global_sequence < result.status.main_sequence &&
                result.status.main_sequence < result.status.io_sequence, "Native startup order is wrong");
            auto cooperative = seeded ? expected : std::vector<unsigned char>(136, 0);
            for (const auto [offset, phase] : {std::pair<size_t, unsigned char>{16, static_cast<unsigned char>(SKCOOP_BOOTSTRAP_TLS)},
                {64, static_cast<unsigned char>(SKCOOP_BOOTSTRAP_GLOBAL)}, {128, static_cast<unsigned char>(SKCOOP_BOOTSTRAP_MAIN)}}) {
                const unsigned char pattern[8] = {'S', 'K', 'C', 'O', 'O', 'P', phase, 0xa5};
                std::copy(std::begin(pattern), std::end(pattern), cooperative.begin() + static_cast<std::ptrdiff_t>(offset));
            }
            require(bytes(native.coop() / L"S0000.co2") == cooperative &&
                bytes(native.coop() / L"S0000.co2.bak") == cooperative,
                "Native partial writes/copy/recovery changed untouched bytes or size");
            require(bytes(native.solo() / L"S0000.sl2") == expected &&
                bytes(native.solo() / L"S0000.sl2.bak") == expected, "Native profile changed solo sentinels");
            ++passed;
        }
        for (const bool hard_link : {false, true}) {
            Tree invalid;
            create_sentinel(invalid.solo() / L"S0000.sl2", seed);
            create_sentinel(invalid.solo() / L"S0000.sl2.bak", seed);
            if (hard_link) {
                require(CreateHardLinkW((invalid.coop() / L"S0000.co2").c_str(),
                    (invalid.solo() / L"S0000.sl2").c_str(), nullptr), "Cannot prepare physical hardlink rejection");
            } else require(RemoveDirectoryW(invalid.coop().c_str()), "Cannot prepare missing account rejection");
            skcoop_bootstrap_run_result result{};
            result.struct_size = sizeof(result);
            require(skcoop_run_bootstrap_fixture(fixture.c_str(), bootstrap.c_str(), invalid.root.c_str(),
                SKCOOP_BOOTSTRAP_TEST_NATIVE_PROFILE, 10000, &result), "Physical rejection host failed");
            require(result.process_exit_code == 0xc0000142u && result.status.phase == SKCOOP_BOOTSTRAP_FAILED &&
                result.status.checkpoint_mask == 0 && result.status.installed_hooks == 0,
                "Invalid physical profile reached TLS/global/main");
            require(bytes(invalid.solo() / L"S0000.sl2") == expected &&
                bytes(invalid.solo() / L"S0000.sl2.bak") == expected, "Physical rejection changed solo bytes");
            ++passed;
        }
        std::cout << "Bootstrap fixture: " << passed << "/" << passed
            << " passed; ready < TLS < global < main; native partial I/O and solo sentinels verified.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Bootstrap fixture failure: " << error.what() << '\n';
        return 1;
    }
}
