#include "skcoop/launch_protocol.h"
#include <detours.h>
#include <algorithm>
#include <array>
#include <cstddef>
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
    DWORD replace_fixture_image_size(DWORD replacement) const {
        // Only this tree's copy of our test program is changed. Giving its PE
        // the supported image size lets the shipping DLL reach the digest
        // refusal without any runtime test bypass or a game executable.
        Handle file(CreateFileW(renamed().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        require(file.get() != INVALID_HANDLE_VALUE, "Cannot open owned PE fixture for bounded edit.");
        LARGE_INTEGER size{};
        require(GetFileSizeEx(file.get(), &size) && size.QuadPart >= static_cast<LONGLONG>(sizeof(IMAGE_DOS_HEADER)),
            "Owned PE fixture is truncated.");
        IMAGE_DOS_HEADER dos{};
        DWORD count = 0;
        require(ReadFile(file.get(), &dos, sizeof(dos), &count, nullptr) && count == sizeof(dos) &&
            dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= 64 && dos.e_lfanew <= 4096 &&
            static_cast<LONGLONG>(dos.e_lfanew) + static_cast<LONGLONG>(sizeof(IMAGE_NT_HEADERS64)) <= size.QuadPart,
            "Owned PE fixture has invalid bounded DOS/NT headers.");
        LARGE_INTEGER position{};
        position.QuadPart = dos.e_lfanew;
        IMAGE_NT_HEADERS64 nt{};
        require(SetFilePointerEx(file.get(), position, nullptr, FILE_BEGIN) &&
            ReadFile(file.get(), &nt, sizeof(nt), &count, nullptr) && count == sizeof(nt) &&
            nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
            nt.FileHeader.SizeOfOptionalHeader == sizeof(IMAGE_OPTIONAL_HEADER64) &&
            nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC,
            "Owned PE fixture is not native AMD64.");
        const DWORD previous = nt.OptionalHeader.SizeOfImage;
        position.QuadPart += offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
            offsetof(IMAGE_OPTIONAL_HEADER64, SizeOfImage);
        require(SetFilePointerEx(file.get(), position, nullptr, FILE_BEGIN) &&
            WriteFile(file.get(), &replacement, sizeof(replacement), &count, nullptr) &&
            count == sizeof(replacement) && FlushFileBuffers(file.get()),
            "Cannot update owned fixture image size.");
        return previous;
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

enum class Mutation { missing, truncated, size, version, previous_version, pid, host, zero_nonce, nonce_mismatch,
    reserved, status_reserved, actor_mode, actor_padding, actor_status, actor_metrics,
    trace_position, trace_resolved, trace_held, trace_flags, previous_status_version, previous_status_size,
    failed_input_epoch,failed_input_flags,failed_input_padding,failed_input_intent,failed_input_pad,failed_input_consumer,
    identity, digest, pe, path };
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
    if (mutation == Mutation::pe || mutation == Mutation::path || mutation == Mutation::digest) {
        constexpr char supported_hash[] =
            "637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856";
        const auto digit = [](char c) { return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
        for (std::size_t i = 0; i < std::size(payload.executable_sha256); ++i)
            payload.executable_sha256[i] = static_cast<std::uint8_t>(
                (digit(supported_hash[2 * i]) << 4) | digit(supported_hash[2 * i + 1]));
    }
    if (mutation == Mutation::digest) payload.executable_sha256[0] ^= 1;
    if (mutation == Mutation::path) text(payload.exe_path, tree.root / L"different" / L"sekiro.exe");
    require(DuplicateHandle(GetCurrentProcess(), mapping.get(), process.process.get(), &payload.mapping_handle,
        FILE_MAP_READ | FILE_MAP_WRITE, FALSE, 0) != FALSE &&
        DuplicateHandle(GetCurrentProcess(), event.get(), process.process.get(), &payload.ready_event, EVENT_MODIFY_STATE, FALSE, 0) != FALSE,
        "Cannot duplicate runtime guard IPC into child.");
    // Structurally valid data never authenticates this executable as Sekiro:
    // every identity case has an unsupported path, PE, or digest. There is no
    // test bypass and no payload combines all supported identity fields.
    if (mutation == Mutation::size) --payload.struct_size;
    if (mutation == Mutation::version) ++payload.version;
    if (mutation == Mutation::previous_version) --payload.version;
    if (mutation == Mutation::previous_status_version) --status->version;
    if (mutation == Mutation::previous_status_size) status->struct_size-=sizeof(NativeFailedInputDiagnostic);
    if (mutation == Mutation::pid) ++payload.child_pid;
    if (mutation == Mutation::host) payload.host_pid = 0;
    if (mutation == Mutation::zero_nonce) std::memset(payload.nonce, 0, sizeof(payload.nonce));
    if (mutation == Mutation::nonce_mismatch) payload.nonce[0] ^= 0xff;
    if (mutation == Mutation::reserved) payload.reserved[0] = 1;
    if (mutation == Mutation::status_reserved) status->reserved[0] = 1;
    if (mutation == Mutation::actor_mode) payload.actor_test = static_cast<ActorTest>(99);
    if (mutation == Mutation::actor_padding) payload.actor_padding = 1;
    if (mutation == Mutation::actor_status) status->actor_ticks = 1;
    if (mutation == Mutation::actor_metrics) status->actor_factory_us = 1;
    // Even unused slots and data without a validity flag must start empty.
    // Put each independent mutation at the tail to verify the complete array.
    auto& trace = status->native_controller_trace[native_trace_capacity-1];
    if (mutation == Mutation::trace_position) trace.position_mm[2] = -1;
    if (mutation == Mutation::trace_resolved) trace.resolved_milli[2] = 1;
    if (mutation == Mutation::trace_held) trace.held_mask = 1;
    if (mutation == Mutation::trace_flags) trace.observation_flags = native_trace_tail_complete|native_trace_action_bound|
        native_trace_attacking|native_trace_guarding|native_trace_action_idle;
    auto& failed_input=status->native_controller_failed_input;
    if(mutation==Mutation::failed_input_epoch)failed_input.epoch=1;
    if(mutation==Mutation::failed_input_flags)failed_input.flags=16; // Unknown flags also refuse startup.
    if(mutation==Mutation::failed_input_padding)failed_input.padding=1;
    if(mutation==Mutation::failed_input_intent)failed_input.intent_micro[3]=-1;
    if(mutation==Mutation::failed_input_pad)failed_input.after_pad_micro[3]=1;
    if(mutation==Mutation::failed_input_consumer)failed_input.at_consumer_micro[3]=-1;
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
    if (mutation == Mutation::trace_position || mutation == Mutation::trace_resolved ||
        mutation == Mutation::trace_held || mutation == Mutation::trace_flags ||
        mutation == Mutation::previous_status_version || mutation == Mutation::previous_status_size ||
        mutation == Mutation::failed_input_epoch || mutation == Mutation::failed_input_flags ||
        mutation == Mutation::failed_input_padding || mutation == Mutation::failed_input_intent ||
        mutation == Mutation::failed_input_pad || mutation == Mutation::failed_input_consumer) {
        require(InterlockedCompareExchange(&status->phase, 0, 0) == static_cast<LONG>(Phase::payload_prepared) &&
            WaitForSingleObject(event.get(), 0) == WAIT_TIMEOUT,
            "Dirty native trace reached identity/bootstrap instead of the initial status guard.");
    }
    if (mutation == Mutation::identity || mutation == Mutation::digest ||
        mutation == Mutation::pe || mutation == Mutation::path) {
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
        for (const auto mutation : {Mutation::missing, Mutation::truncated, Mutation::size, Mutation::version, Mutation::previous_version,
            Mutation::pid, Mutation::host, Mutation::zero_nonce, Mutation::nonce_mismatch, Mutation::reserved,
            Mutation::status_reserved, Mutation::actor_mode, Mutation::actor_padding, Mutation::actor_status, Mutation::actor_metrics,
            Mutation::previous_status_version,Mutation::previous_status_size,Mutation::failed_input_epoch,Mutation::failed_input_flags,
            Mutation::failed_input_padding,Mutation::failed_input_intent,Mutation::failed_input_pad,Mutation::failed_input_consumer,
            Mutation::trace_position, Mutation::trace_resolved, Mutation::trace_held, Mutation::trace_flags, Mutation::identity}) {
            reject_before_startup(runtime, tree, mutation);
            ++passed;
        }
        // A declared known hash and matching path cannot authenticate a
        // different mapped PE image. The fixture still has its original PE.
        reject_before_startup(runtime, tree, Mutation::pe);
        ++passed;
        constexpr DWORD supported_image_size = 0x42d2000;
        const DWORD fixture_image_size = tree.replace_fixture_image_size(supported_image_size);
        require(fixture_image_size != supported_image_size, "Owned fixture unexpectedly has the supported game image size.");
        // Prove the edited fixture still starts and reaches TLS. Loading the
        // DLL from main without a launch payload must continue to be refused.
        load_from_main(tree.renamed(), runtime, tree, true);
        ++passed;
        // Path and PE checks now pass, but this nonzero digest differs by one
        // byte from the supported hash and must refuse startup before hooks.
        reject_before_startup(runtime, tree, Mutation::digest);
        ++passed;
        // The edited image size and declared digest now match, but the payload
        // names a different module path. Never send all three matching fields.
        reject_before_startup(runtime, tree, Mutation::path);
        ++passed;
        require(tree.replace_fixture_image_size(fixture_image_size) == supported_image_size,
            "Owned fixture image size changed unexpectedly before restoration.");
        std::cout << passed << '/' << passed << " runtime launch guard cases passed; no game executable used.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Runtime launch guard failure: " << error.what() << '\n';
        return 1;
    }
}
