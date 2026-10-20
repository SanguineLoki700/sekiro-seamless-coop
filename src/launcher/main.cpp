#include "preflight.h"
#include "process.h"
#include "runtime_loader.h"
#include <iostream>
#include <stdexcept>

namespace {
void usage() {
    std::wcout << L"Sekiro co-op 0.0.1-bootstrap diagnostic scaffold (gameplay is unavailable)\n"
        L"  --inspect <exe> [--profiles <ini>]  Read PE headers and SHA-256; never launch\n"
        L"  --diagnose [--runtime <dll>]       Load the selected diagnostic DLL locally\n"
        L"  --self-test                       Run only our controlled smoke-test child\n"
        L"  --launch <exe> [--profiles <ini>]  Preflight, then refuse (not implemented)\n";
}
}
int wmain(int argc, wchar_t** argv) {
    using namespace skcoop::launcher;
    try {
        if (argc == 1 || (argc == 2 && std::wstring_view(argv[1]) == L"--help")) { usage(); return 0; }
        const std::wstring command = argv[1];
        const auto folder = executable_directory();
        if (command == L"--self-test" && argc == 2) {
            const auto result = run_controlled_child(folder / L"skcoop_windows_smoke.exe",
                {L"--runtime", (folder / L"skcoop_runtime.dll").wstring(), L"--launcher", (folder / L"skcoop_launcher.exe").wstring()});
            std::wcout << (result == 0 ? L"Diagnostic self-test passed. No game process was started.\n" : L"Diagnostic self-test failed.\n");
            return result;
        }
        if (command == L"--diagnose") {
            auto path = folder / L"skcoop_runtime.dll";
            if (argc == 4 && std::wstring_view(argv[2]) == L"--runtime") path = argv[3];
            else if (argc != 2) throw std::runtime_error("Invalid --diagnose arguments.");
            RuntimeLibrary runtime(path);
            skcoop_capabilities caps{sizeof(skcoop_capabilities)};
            if (runtime.capabilities(&caps) != SKCOOP_OK) throw std::runtime_error("Cannot read runtime capabilities.");
            const skcoop_init_options options{sizeof(skcoop_init_options), SKCOOP_RUNTIME_ABI_VERSION, SKCOOP_MODE_DIAGNOSTIC, 0};
            if (runtime.initialize(&options) != SKCOOP_OK) throw std::runtime_error("Diagnostic initialization failed.");
            std::wcout << L"Runtime ABI: " << runtime.abi_version() << L"\nDiagnostic initialization: ready\n"
                << L"Native hooks: " << caps.native_hooks << L"\nSave isolation: " << caps.save_isolation
                << L"\nMultiplayer: " << caps.multiplayer << L"\n(0 = unimplemented). No game was started.\n";
            return 0;
        }
        if (command == L"--inspect" || command == L"--launch") {
            if (argc != 3 && argc != 5) throw std::runtime_error("An executable path is required.");
            auto profiles_path = folder / L"config" / L"game_profiles.ini";
            if (argc == 5) {
                if (std::wstring_view(argv[3]) != L"--profiles") throw std::runtime_error("Unknown inspection option.");
                profiles_path = argv[4];
            }
            const auto identity = inspect_executable(argv[2]);
            const auto profiles = read_profiles(profiles_path);
            const auto approved = verified_profile(identity, profiles);
            std::wcout << L"Path: " << identity.path.wstring() << L"\nPE machine: AMD64\nSize: " << identity.file_size << L" bytes\nSHA-256: ";
            std::wcout << std::wstring(identity.sha256.begin(), identity.sha256.end()) << L'\n';
            std::wcout << L"Verified profile: " << (approved ? L"yes" : L"no") << L"\n";
            if (command == L"--inspect") {
                std::wcout << L"Inspection only. No DLL was loaded and no game process was started.\n";
                return 0;
            }
            if (!approved) {
                std::wcerr << L"Launch refused: exact hash plus verified native hooks and saves are required. No process was created.\n";
                return 20;
            }
            std::wcerr << L"Launch refused: this build has no gameplay bootstrap, native hooks, save isolation, or multiplayer. No process was created.\n";
            return 21;
        }
        usage();
        return 2;
    } catch (const std::exception& error) {
        const std::string message = error.what();
        std::wcerr << L"Error: " << std::wstring(message.begin(), message.end()) << L'\n';
        return 1;
    }
}
