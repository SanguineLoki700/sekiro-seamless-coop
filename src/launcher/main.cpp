#include "preflight.h"
#include "launcher_process.h"
#include "runtime_loader.h"
#include "steam_install.h"
#include "protected_launch.h"
#include "skcoop/save_settings.hpp"
#include <iostream>
#include <stdexcept>
#include <fstream>

namespace {
void usage() {
    std::wcout << L"Sekiro co-op experimental native test tools (multiplayer acceptance pending)\n"
        L"  --inspect <exe> [--profiles <ini>]  Read PE headers and SHA-256; never launch\n"
        L"  --diagnose [--runtime <dll>]       Load the selected diagnostic DLL locally\n"
        L"  --inspect-save-profile [ini]      Validate settings; no save/game access\n"
        L"  --self-test                       Run only our controlled smoke-test child\n"
        L"  --find-game                       Detect official Steam installation; no launch\n"
        L"  --check-launch <ini>              Check protected integration configuration\n"
        L"  --direct-start <ini>              Protected direct test; real Steam client required\n"
        L"  --prepare-direct <role> <file>    Developer preparation; GUI handles this normally\n"
        L"  --steam-start [ini] <Steam exe>   Explicit protected test, via Steam only\n"
        L"  --launch <exe> [--profiles <ini>]  Preflight, then refuse (not implemented)\n";
}
}
int wmain(int argc, wchar_t** argv) {
    using namespace skcoop::launcher;
    try {
        if (argc == 1 || (argc == 2 && std::wstring_view(argv[1]) == L"--help")) { usage(); return 0; }
        const std::wstring command = argv[1];
        const auto folder = executable_directory();
        if (command == L"--find-game" && argc == 2) {
            const auto install = inspect_steam_install();
            const auto identity = inspect_executable(install.executable);
            std::wcout << L"Steam AppID: 814380\nExecutable: " << install.executable.wstring()
                << L"\nBuild: " << std::wstring(install.build_id.begin(),install.build_id.end())
                << L"\nSHA-256: " << std::wstring(identity.sha256.begin(),identity.sha256.end())
                << L"\nInspection only. No process created.\n";
            return 0;
        }
        if (command == L"--check-launch") {
            if (argc != 3) throw std::runtime_error("Use --check-launch <protected launch ini>.");
            return check_protected_launch(argv[2]);
        }
        if(command==L"--direct-start") {
            if(argc!=3)throw std::runtime_error("Use --direct-start <prepared launch ini>.");
            return direct_protected_launch(argv[2]);
        }
        if(command==L"--prepare-direct") {
            if(argc!=4)throw std::runtime_error("Use --prepare-direct <host|guest> <private room file>.");
            const std::wstring_view role=argv[2];if(role!=L"host"&&role!=L"guest")throw std::runtime_error("Invalid role.");
            std::ifstream file(std::filesystem::path(argv[3]),std::ios::binary);std::string code;
            std::getline(file,code);if(!code.empty()&&code.back()=='\r')code.pop_back();
            if(!file||code.size()>159||file.peek()!=EOF)throw std::runtime_error("Invalid private room file.");
            std::wcout<<prepare_direct_lan(folder,role==L"host"?1u:2u,code).wstring()<<L'\n';return 0;
        }
        if (command == L"--steam-start") {
            if (argc == 3) return steam_protected_launch(folder / L"config" / L"launch.ini",argv[2]);
            if (argc == 4) return steam_protected_launch(argv[2],argv[3]);
            throw std::runtime_error("Steam must supply exactly one original Sekiro executable after the optional launch configuration.");
        }
        if (command == L"--inspect-save-profile") {
            if (argc != 2 && argc != 3) throw std::runtime_error("Invalid save profile arguments.");
            const auto path = argc == 3 ? std::filesystem::path(argv[2]) : folder / L"config" / L"save_profile.ini";
            const auto settings = skcoop::saves::read_save_settings(path);
            std::wcout << L"Save profile syntax: valid\nExtension: " << settings.extension
                << L"\nDestination mode: "
                << (settings.mode == skcoop::saves::DestinationMode::separate_profile ? L"separate_profile" : L"same_account_directory")
                << L"\nProfile root: " << (settings.coop_profile_root.empty() ? L"not configured" : L"configured")
                << L"\nNative save interception: available only in the explicit protected integration launch\nNo save or game process was opened.\n";
            return 0;
        }
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
                << L"\nMultiplayer: " << caps.multiplayer << L"\n(0 = inactive in this diagnostic process). No game was started.\n";
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
            std::wcerr << L"Launch refused: gameplay and multiplayer are unavailable. Protected integration tests use the separate Steam entry. No process was created.\n";
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
