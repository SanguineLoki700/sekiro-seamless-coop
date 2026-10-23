#include "skcoop/launch_protocol.h"
#include <detours.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace fs = std::filesystem;
using namespace skcoop::launch;
constexpr wchar_t marker_variable[] = L"SKCOOP_RUNTIME_GUARD_MARKER";
constexpr DWORD dll_init_failed_status = 0xc0000142u;

// Every child receives an owned marker path in its private environment block.
// Executable TLS precedes C++ global initialization/main, but follows imported
// DLL initialization. Invalid bootstrap payloads must stop before this callback.
void NTAPI startup_tls(void*, DWORD reason, void*) noexcept {
    if (reason != DLL_PROCESS_ATTACH) return;
    wchar_t marker[32768]{};
    const DWORD count = GetEnvironmentVariableW(marker_variable, marker, static_cast<DWORD>(std::size(marker)));
    if (!count || count >= std::size(marker)) return;
    const HANDLE file = CreateFileW(marker, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        constexpr char bytes[] = "controlled executable TLS reached";
        DWORD written = 0;
        WriteFile(file, bytes, sizeof(bytes) - 1, &written, nullptr);
        CloseHandle(file);
    }
}
}

#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:skcoop_runtime_guard_tls")
#pragma const_seg(".CRT$XLB")
extern "C" const PIMAGE_TLS_CALLBACK skcoop_runtime_guard_tls = startup_tls;
#pragma const_seg()

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
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
    explicit View(void* value) noexcept : value_(value) {}
    ~View() { if (value_) UnmapViewOfFile(value_); }
    void* get() const noexcept { return value_; }
private:
    void* value_;
};
class OwnedChild {
public:
    explicit OwnedChild(PROCESS_INFORMATION child) noexcept : process(child.hProcess), thread(child.hThread), pid(child.dwProcessId) {}
    ~OwnedChild() {
        if (WaitForSingleObject(process.get(), 0) != WAIT_OBJECT_0) {
            TerminateProcess(process.get(), ERROR_PROCESS_ABORTED);
            WaitForSingleObject(process.get(), 5000);
        }
    }
    DWORD exit_code() const {
        const DWORD wait = WaitForSingleObject(process.get(), 5000);
        require(wait == WAIT_OBJECT_0, "Owned runtime fixture timed out or wait failed.");
        DWORD value = STILL_ACTIVE;
        require(GetExitCodeProcess(process.get(), &value) != FALSE, "Cannot read fixture exit status.");
        return value;
    }
    Handle process, thread;
    DWORD pid;
};
fs::path own_executable() {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    require(length && length < std::size(path), "Cannot identify owned runtime test executable.");
    return fs::canonical(path);
}
class Tree {
public:
    Tree() : parent(fs::canonical(fs::temp_directory_path())) {
        for (unsigned i = 0; i < 100; ++i) {
            const auto candidate = parent / (L"skcoop-runtime-guard-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(i));
            if (CreateDirectoryW(candidate.c_str(), nullptr)) { root = candidate; break; }
            require(GetLastError() == ERROR_ALREADY_EXISTS, "Cannot create runtime guard fixture directory.");
        }
        require(!root.empty(), "Runtime guard fixture names exhausted.");
        // This is a copy of this test program only. No supplied executable path
        // can become the target, and no real game binary is copied or launched.
        require(CopyFileW(own_executable().c_str(), renamed().c_str(), TRUE) != FALSE,
            "Cannot copy owned fixture executable.");
    }
    ~Tree() {
        DeleteFileW(marker().c_str());
        DeleteFileW(renamed().c_str());
        RemoveDirectoryW(root.c_str());
    }
    fs::path marker() const { return root / L"startup.marker"; }
    fs::path renamed() const { return root / L"sekiro.exe"; }
    void clear_marker() const {
        if (!DeleteFileW(marker().c_str())) require(GetLastError() == ERROR_FILE_NOT_FOUND, "Cannot clear owned TLS marker.");
    }
    fs::path parent, root;
};
std::wstring quote(std::wstring_view text) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t c : text) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
std::string detours_path(const fs::path& path) {
    const auto text = path.wstring();
    const int size = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    require(size > 0, "Cannot size runtime DLL path for Detours.");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    BOOL substituted = FALSE;
    require(WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.c_str(), -1, bytes.data(), size, nullptr, &substituted) > 0 && !substituted,
        "Detours cannot represent runtime DLL path exactly.");
    return bytes;
}
std::vector<wchar_t> child_environment(const fs::path& marker) {
    wchar_t* inherited = GetEnvironmentStringsW();
    require(inherited != nullptr, "Cannot copy fixture environment.");
    std::vector<std::wstring> variables;
    try {
        for (const wchar_t* cursor = inherited; *cursor;) {
            std::wstring text(cursor);
            cursor += text.size() + 1;
            const auto split = text.find(L'=', text[0] == L'=' ? 1 : 0);
            if (split == std::wstring::npos || CompareStringOrdinal(text.data(), static_cast<int>(split), marker_variable,
                static_cast<int>(std::size(marker_variable) - 1), TRUE) != CSTR_EQUAL)
                variables.push_back(std::move(text));
        }
    } catch (...) { FreeEnvironmentStringsW(inherited); throw; }
    FreeEnvironmentStringsW(inherited);
    variables.push_back(std::wstring(marker_variable) + L"=" + marker.wstring());
    std::sort(variables.begin(), variables.end(), [](const auto& a, const auto& b) {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    std::vector<wchar_t> result;
    for (const auto& text : variables) { result.insert(result.end(), text.begin(), text.end()); result.push_back(L'\0'); }
    result.push_back(L'\0');
    return result;
}
template <std::size_t Size> void text(wchar_t (&destination)[Size], const fs::path& source) {
    const auto value = source.wstring();
    require(!value.empty() && value.size() < Size, "Fixture path is too long for runtime payload.");
    std::copy(value.begin(), value.end(), destination);
}
void load_from_main(const fs::path& executable, const fs::path& runtime, const Tree& tree, bool expect_refusal) {
    tree.clear_marker();
    STARTUPINFOW startup{sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION child{};
    auto command = quote(executable.wstring()) + L" --guard-load " + quote(runtime.wstring()) +
        (expect_refusal ? L" refused" : L" allowed");
    auto environment = child_environment(tree.marker());
    require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, environment.data(), tree.root.c_str(), &startup, &child) != FALSE,
        "Cannot create owned diagnostic LoadLibrary fixture.");
    OwnedChild process(child);
    require(process.exit_code() == 0, "Diagnostic LoadLibrary acceptance/refusal differed from expectation.");
    require(fs::is_regular_file(tree.marker()), "Controlled executable TLS did not record successful startup.");
}

enum class Mutation { missing, truncated, size, version, pid, host, zero_nonce, nonce_mismatch, reserved, status_reserved, identity };
void reject_before_startup(const fs::path& runtime, const Tree& tree, Mutation mutation) {
    tree.clear_marker();
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedStatus), nullptr));
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    require(mapping.get() && event.get(), "Cannot create runtime guard IPC.");
    View view(MapViewOfFile(mapping.get(), FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SharedStatus)));
    require(view.get() != nullptr, "Cannot map runtime guard IPC.");
    auto* status = static_cast<SharedStatus*>(view.get());
    std::memset(status, 0, sizeof(*status));
    status->struct_size = sizeof(*status);
    status->version = protocol_version;
    status->host_pid = GetCurrentProcessId();
    status->phase = static_cast<LONG>(Phase::payload_prepared);
    for (std::size_t i = 0; i < std::size(status->nonce); ++i) status->nonce[i] = static_cast<std::uint8_t>(i + 1);

    auto command = quote(tree.renamed().wstring()) + L" --guard-child";
    auto environment = child_environment(tree.marker());
    const auto dll = detours_path(runtime);
    STARTUPINFOW startup{sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION child{};
    require(DetourCreateProcessWithDllExW(tree.renamed().c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, environment.data(), tree.root.c_str(),
        &startup, &child, dll.c_str(), nullptr) != FALSE, "Cannot inject shipping runtime into owned fixture.");
    OwnedChild process(child);
    status->child_pid = child.dwProcessId;

    Payload payload{};
    payload.struct_size = sizeof(payload);
    payload.version = protocol_version;
    payload.host_pid = status->host_pid;
    payload.child_pid = status->child_pid;
    std::memcpy(payload.nonce, status->nonce, sizeof(payload.nonce));
    text(payload.exe_path, tree.renamed());
    text(payload.solo_roaming, tree.root / L"solo");
    text(payload.coop_roaming, tree.root / L"coop");
    text(payload.account, fs::path(L"12345678901234567"));
    require(DuplicateHandle(GetCurrentProcess(), mapping.get(), process.process.get(), &payload.mapping_handle,
        FILE_MAP_READ | FILE_MAP_WRITE, FALSE, 0) != FALSE &&
        DuplicateHandle(GetCurrentProcess(), event.get(), process.process.get(), &payload.ready_event, EVENT_MODIFY_STATE, FALSE, 0) != FALSE,
        "Cannot duplicate runtime guard IPC into child.");
    // Structurally valid data never authenticates this executable as Sekiro:
    // its image/hash are deliberately unsupported, and no test bypass exists.
    if (mutation == Mutation::size) --payload.struct_size;
    if (mutation == Mutation::version) ++payload.version;
    if (mutation == Mutation::pid) ++payload.child_pid;
    if (mutation == Mutation::host) payload.host_pid = 0;
    if (mutation == Mutation::zero_nonce) std::memset(payload.nonce, 0, sizeof(payload.nonce));
    if (mutation == Mutation::nonce_mismatch) payload.nonce[0] ^= 0xff;
    if (mutation == Mutation::reserved) payload.reserved[0] = 1;
    if (mutation == Mutation::status_reserved) status->reserved[0] = 1;
    if (mutation != Mutation::missing) {
        const DWORD count = static_cast<DWORD>(sizeof(payload) - (mutation == Mutation::truncated ? 1u : 0u));
        require(DetourCopyPayloadToProcess(process.process.get(), payload_guid, &payload, count) != FALSE,
            "Cannot write malformed runtime fixture payload.");
    }
    require(ResumeThread(process.thread.get()) != static_cast<DWORD>(-1), "Cannot resume runtime guard fixture.");
    require(process.exit_code() == dll_init_failed_status,
        "Invalid runtime launch did not exit specifically with STATUS_DLL_INIT_FAILED.");
    require(!fs::exists(tree.marker()), "Rejected runtime payload reached executable TLS/startup.");
    require(InterlockedCompareExchange64(&status->hooks_mask, 0, 0) == 0,
        "Rejected runtime launch reported an installed hook transaction.");
    require(InterlockedCompareExchange(&status->phase, 0, 0) != static_cast<LONG>(Phase::ready),
        "Rejected runtime launch reported READY.");
    if (mutation == Mutation::identity) {
        require(WaitForSingleObject(event.get(), 0) == WAIT_OBJECT_0 &&
            InterlockedCompareExchange(&status->phase, 0, 0) == static_cast<LONG>(Phase::failed) &&
            InterlockedCompareExchange(&status->fatal_error, 0, 0) == ERROR_INVALID_IMAGE_HASH,
            "Well-shaped unsupported fixture did not report executable identity refusal.");
    }
}
}

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc == 2 && std::wstring_view(argv[1]) == L"--guard-child") return 0;
    if (argc == 4 && std::wstring_view(argv[1]) == L"--guard-load") {
        SetLastError(ERROR_SUCCESS);
        const HMODULE runtime = LoadLibraryW(argv[2]);
        const DWORD error = GetLastError();
        const bool refusal = std::wstring_view(argv[3]) == L"refused";
        if (runtime) FreeLibrary(runtime);
        return refusal ? (!runtime && error == ERROR_DLL_INIT_FAILED ? 0 : 2) : (runtime ? 0 : 3);
    }
    try {
        require(argc == 2, "Expected only the shipping runtime DLL path.");
        const fs::path runtime = fs::canonical(argv[1]);
        require(_wcsicmp(runtime.filename().c_str(), L"skcoop_runtime.dll") == 0 && fs::is_regular_file(runtime),
            "Runtime guard tests accept only the shipping runtime DLL name.");
        Tree tree;
        load_from_main(own_executable(), runtime, tree, false);
        load_from_main(tree.renamed(), runtime, tree, true);
        unsigned passed = 2;
        for (const auto mutation : {Mutation::missing, Mutation::truncated, Mutation::size, Mutation::version,
            Mutation::pid, Mutation::host, Mutation::zero_nonce, Mutation::nonce_mismatch, Mutation::reserved,
            Mutation::status_reserved, Mutation::identity}) {
            reject_before_startup(runtime, tree, mutation);
            ++passed;
        }
        std::cout << passed << '/' << passed << " runtime launch guard cases passed; no game executable used.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Runtime launch guard failure: " << error.what() << '\n';
        return 1;
    }
}
