#include "preflight.h"
#include "process.h"
#include "runtime_loader.h"
#include <Windows.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using namespace skcoop::launcher;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<typename Callable> void require_rejected(Callable&& callable, const char* message) {
    bool rejected = false;
    try { callable(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
class TempDirectory {
public:
    TempDirectory() {
        const auto root = fs::temp_directory_path();
        path = fs::absolute(root / (L"skcoop smoke 日本 " + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
        require(fs::create_directory(path), "Cannot create owned smoke-test temporary directory.");
    }
    ~TempDirectory() {
        std::error_code ignored;
        for (const auto& file : files) fs::remove(file, ignored);
        fs::remove(path, ignored);
    }
    fs::path file(const wchar_t* name) {
        const auto result = path / name;
        files.push_back(result);
        return result;
    }
    fs::path path;
    std::vector<fs::path> files;
};
void write(const fs::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(output), "Cannot write owned fixture.");
}
void write(const fs::path& path, const std::string& text) {
    write(path, std::vector<unsigned char>(text.begin(), text.end()));
}
void put16(std::vector<unsigned char>& bytes, std::size_t at, unsigned value) {
    bytes.at(at) = static_cast<unsigned char>(value);
    bytes.at(at + 1) = static_cast<unsigned char>(value >> 8);
}
void put32(std::vector<unsigned char>& bytes, std::size_t at, unsigned value) {
    put16(bytes, at, value);
    put16(bytes, at + 2, value >> 16);
}
std::vector<unsigned char> pe_fixture() {
    std::vector<unsigned char> bytes(512);
    bytes[0] = 'M'; bytes[1] = 'Z';
    put32(bytes, 0x3c, 0x80);
    put32(bytes, 0x80, 0x4550);
    put16(bytes, 0x84, IMAGE_FILE_MACHINE_AMD64);
    put16(bytes, 0x86, 1);
    put16(bytes, 0x94, 240);
    put16(bytes, 0x96, IMAGE_FILE_EXECUTABLE_IMAGE);
    put16(bytes, 0x98, IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    put32(bytes, 0x98 + 108, 16);
    return bytes;
}
std::string profile(const std::string& hash, bool hooks, bool saves) {
    return "[meta]\nformat_version=1\n[profile:test]\nsha256=" + hash +
        "\npe_machine=amd64\nnative_hooks_verified=" + (hooks ? "true" : "false") +
        "\nsaves_verified=" + (saves ? "true\n" : "false\n");
}
int runtime_child(const fs::path& runtime_path) {
    RuntimeLibrary runtime(runtime_path);
    require(runtime.abi_version() == SKCOOP_RUNTIME_ABI_VERSION, "ABI mismatch.");
    require(runtime.health(nullptr) == SKCOOP_INVALID_ARGUMENT, "Null health output accepted.");
    skcoop_health health{sizeof(skcoop_health)};
    require(runtime.health(&health) == SKCOOP_OK && health.state == SKCOOP_STATE_LOADED,
        "DllMain must not initialize diagnostic or gameplay work.");
    skcoop_capabilities caps{sizeof(skcoop_capabilities)};
    require(runtime.capabilities(&caps) == SKCOOP_OK, "Capabilities unavailable.");
    require(caps.mask == SKCOOP_CAP_DIAGNOSTICS && caps.native_hooks == SKCOOP_NOT_IMPLEMENTED &&
        caps.save_isolation == SKCOOP_NOT_IMPLEMENTED && caps.multiplayer == SKCOOP_NOT_IMPLEMENTED,
        "Scaffold overstates gameplay capabilities.");
    require(runtime.initialize(nullptr) == SKCOOP_INVALID_ARGUMENT, "Null initialization accepted.");
    skcoop_init_options options{sizeof(skcoop_init_options), SKCOOP_RUNTIME_ABI_VERSION + 1, SKCOOP_MODE_DIAGNOSTIC, 0};
    require(runtime.initialize(&options) == SKCOOP_ABI_MISMATCH, "Wrong ABI initialization accepted.");
    options.abi_version = SKCOOP_RUNTIME_ABI_VERSION;
    options.mode = SKCOOP_MODE_GAMEPLAY;
    require(runtime.initialize(&options) == SKCOOP_UNIMPLEMENTED, "Gameplay initialization must be refused.");
    require(runtime.health(&health) == SKCOOP_OK && health.state == SKCOOP_STATE_LOADED, "Rejected init changed runtime state.");
    options.mode = SKCOOP_MODE_DIAGNOSTIC;
    options.reserved = 1;
    require(runtime.initialize(&options) == SKCOOP_INVALID_ARGUMENT, "Reserved initialization flags accepted.");
    options.reserved = 0;
    options.struct_size -= 4;
    require(runtime.initialize(&options) == SKCOOP_INVALID_ARGUMENT, "Wrong struct size accepted.");
    options.struct_size = sizeof(options);
    for (int cycle = 0; cycle < 2; ++cycle) {
        require(runtime.initialize(&options) == SKCOOP_OK, "Explicit diagnostic init failed.");
        require(runtime.initialize(&options) == SKCOOP_OK, "Diagnostic init must be idempotent.");
        require(runtime.health(&health) == SKCOOP_OK && health.state == SKCOOP_STATE_DIAGNOSTIC_READY, "Diagnostic state missing.");
        require(runtime.shutdown() == SKCOOP_OK && runtime.shutdown() == SKCOOP_OK, "Shutdown must be idempotent.");
        require(runtime.health(&health) == SKCOOP_OK && health.state == SKCOOP_STATE_LOADED, "Shutdown state missing.");
    }
    return 0;
}
int smoke_parent(const fs::path& runtime, const fs::path& launcher) {
    TempDirectory temp;
    const auto abc = temp.file(L"hash vector.txt");
    write(abc, std::string("abc"));
    require(sha256_file(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 known vector failed.");
    const auto valid = temp.file(L"native fixture 日本.exe");
    auto bytes = pe_fixture();
    write(valid, bytes);
    const auto identity = inspect_executable(valid);
    require(identity.machine == IMAGE_FILE_MACHINE_AMD64 && identity.file_size == 512 && identity.sha256.size() == 64, "Native PE fixture rejected.");
    const auto malformed = temp.file(L"rejected fixture.exe");
    auto bad = bytes;
    put16(bad, 0x84, IMAGE_FILE_MACHINE_I386);
    write(malformed, bad);
    require_rejected([&] { inspect_executable(malformed); }, "32-bit executable accepted.");
    bad = bytes;
    put16(bad, 0x96, IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL);
    write(malformed, bad);
    require_rejected([&] { inspect_executable(malformed); }, "DLL admitted as game executable.");
    bad = bytes;
    put32(bad, 0x98 + 112 + IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR * 8, 0x1000);
    write(malformed, bad);
    require_rejected([&] { inspect_executable(malformed); }, "Managed image accepted.");
    bad = bytes;
    put32(bad, 0x3c, 0xfffffff0);
    write(malformed, bad);
    require_rejected([&] { inspect_executable(malformed); }, "Out-of-file PE header accepted.");
    write(malformed, std::vector<unsigned char>(8));
    require_rejected([&] { inspect_executable(malformed); }, "Truncated DOS header accepted.");
    const auto empty = parse_profiles("[meta]\nformat_version=1\n");
    require(!verified_profile(identity, empty), "Empty allowlist approves an executable.");
    require(!verified_profile(identity, parse_profiles(profile(identity.sha256, false, true))), "Unverified hooks accepted.");
    require(!verified_profile(identity, parse_profiles(profile(identity.sha256, true, false))), "Unverified saves accepted.");
    const auto approved = parse_profiles(profile(identity.sha256, true, true));
    require(verified_profile(identity, approved).has_value(), "Exact verified profile not matched.");
    auto changed_identity = identity;
    changed_identity.sha256[0] = changed_identity.sha256[0] == 'a' ? 'b' : 'a';
    require(!verified_profile(changed_identity, approved), "Mismatching hash accepted.");
    require_rejected([&] { parse_profiles(profile("*", true, true)); }, "Wildcard profile accepted.");
    require_rejected([&] { parse_profiles(profile(identity.sha256, true, true) + "saves_verified=true\n"); }, "Duplicate profile key accepted.");
    require_rejected([&] { parse_profiles(profile(identity.sha256, true, true) + "allow_any_build=true\n"); }, "Unknown profile key accepted.");
    const auto ini = temp.file(L"closed profiles.ini");
    write(ini, std::string("[meta]\nformat_version=1\n"));
    require(run_controlled_child(launcher, {L"--inspect", valid.wstring(), L"--profiles", ini.wstring()}) == 0, "Launcher inspection failed.");
    require(run_controlled_child(launcher, {L"--launch", valid.wstring(), L"--profiles", ini.wstring()}) == 20, "Default launch guard failed.");
    write(ini, profile(identity.sha256, true, true));
    require(run_controlled_child(launcher, {L"--launch", valid.wstring(), L"--profiles", ini.wstring()}) == 21, "Profile flags enabled unimplemented gameplay.");
    const auto save_ini = temp.file(L"save profile 日本.ini");
    write(save_ini, std::string("[SAVE]\nsave_file_extension=co2\ndestination_mode=separate_profile\ncoop_profile_root=\n"));
    require(run_controlled_child(launcher, {L"--inspect-save-profile", save_ini.wstring()}) == 0, "Save settings inspection failed.");
    write(save_ini, std::string("[SAVE]\nsave_file_extension=sl2\ndestination_mode=separate_profile\ncoop_profile_root=\n"));
    require(run_controlled_child(launcher, {L"--inspect-save-profile", save_ini.wstring()}) == 1, "Vanilla save extension accepted by launcher.");
    require(run_controlled_child(launcher, {L"--inspect-save-profile", temp.file(L"missing.ini").wstring()}) == 1, "Missing save settings accepted.");
    const auto unicode_runtime = temp.file(L"runtime teste 日本.dll");
    require(CopyFileW(fs::absolute(runtime).c_str(), unicode_runtime.c_str(), TRUE), "Cannot prepare Unicode runtime fixture.");
    const auto child_exe = executable_directory() / L"skcoop_windows_smoke.exe";
    require(run_controlled_child(child_exe, {L"--child", unicode_runtime.wstring()}) == 0, "Controlled runtime child failed.");
    std::cout << "Windows smoke passed: SHA-256, PE rejection, exact profiles, save settings, launch refusal, Unicode child and runtime ABI.\n";
    return 0;
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring_view(argv[1]) == L"--child") return runtime_child(argv[2]);
        if (argc != 5 || std::wstring_view(argv[1]) != L"--runtime" || std::wstring_view(argv[3]) != L"--launcher")
            throw std::runtime_error("Use --runtime <our dll> --launcher <our exe>, or internal --child <our dll>.");
        return smoke_parent(argv[2], argv[4]);
    } catch (const std::exception& error) {
        std::cerr << "Windows smoke failed: " << error.what() << '\n';
        return 1;
    }
}
