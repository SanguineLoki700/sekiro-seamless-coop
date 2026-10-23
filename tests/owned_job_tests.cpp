#include "owned_job.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using skcoop::launcher::OwnedJob;

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
class Handle {
public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() { if (valid()) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool valid() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_{};
};
std::filesystem::path self() {
    std::wstring text(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, text.data(), static_cast<DWORD>(text.size()));
    require(length && length < text.size(), "Cannot resolve owned fixture executable.");
    text.resize(length);
    return text;
}
class Child {
public:
    explicit Child(std::wstring arguments, bool suspended = false) {
        const auto path = self();
        auto command = L"\"" + path.wstring() + L"\" " + arguments;
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        require(CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | (suspended ? CREATE_SUSPENDED : 0), nullptr, path.parent_path().c_str(), &startup, &process_) != FALSE,
            "Cannot create owned job fixture process.");
    }
    ~Child() {
        if (process_.hProcess) {
            if (WaitForSingleObject(process_.hProcess, 0) != WAIT_OBJECT_0) {
                TerminateProcess(process_.hProcess, 99);
                WaitForSingleObject(process_.hProcess, 5000);
            }
            CloseHandle(process_.hThread);
            CloseHandle(process_.hProcess);
        }
    }
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    HANDLE process() const { return process_.hProcess; }
    void resume() { require(ResumeThread(process_.hThread) == 1, "Fixture initial thread was not suspended once."); }
private:
    PROCESS_INFORMATION process_{};
};

struct Shared { volatile LONG child_pid; };
class View {
public:
    explicit View(HANDLE mapping) : value_(static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Shared)))) {
        require(value_ != nullptr, "Cannot map owned-job test status.");
    }
    ~View() { UnmapViewOfFile(value_); }
    Shared* get() const { return value_; }
private:
    Shared* value_{};
};

int test_host(const wchar_t* mapping_name, const wchar_t* event_name) {
    Handle mapping(OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, mapping_name));
    Handle event(OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name));
    require(mapping.valid() && event.valid(), "Cannot open owned-job fixture status objects.");
    View shared(mapping.get());
    OwnedJob job;
    Child child(L"--child", true);
    job.assign(child.process());
    child.resume();
    InterlockedExchange(&shared.get()->child_pid, static_cast<LONG>(GetProcessId(child.process())));
    require(SetEvent(event.get()) != FALSE, "Cannot announce guarded fixture child.");
    // The test parent abruptly terminates this owned helper. No C++ destructor
    // executes in that path; only the kernel job-close guard can stop its child.
    WaitForSingleObject(child.process(), INFINITE);
    return 0;
}

void closing_guard_terminates_only_assigned_child() {
    Child unrelated(L"--child");
    Child guarded(L"--child", true);
    {
        OwnedJob job;
        bool rejected{};
        try { job.assign(INVALID_HANDLE_VALUE); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Invalid child assignment accepted.");
        job.assign(guarded.process());
        guarded.resume();
        require(WaitForSingleObject(guarded.process(), 0) == WAIT_TIMEOUT, "Guarded fixture did not remain alive.");
    }
    require(WaitForSingleObject(guarded.process(), 5000) == WAIT_OBJECT_0, "Closing the guard left an orphan child.");
    require(WaitForSingleObject(unrelated.process(), 0) == WAIT_TIMEOUT, "Closing the guard terminated an unrelated fixture.");
}

void abrupt_parent_termination_does_not_leave_orphan() {
    const auto name = L"Local\\SkcoopOwnedJob-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), (name + L"-mapping").c_str()));
    require(mapping.valid() && GetLastError() != ERROR_ALREADY_EXISTS, "Owned-job status mapping was not newly created.");
    Handle ready(CreateEventW(nullptr, TRUE, FALSE, (name + L"-ready").c_str()));
    require(ready.valid() && GetLastError() != ERROR_ALREADY_EXISTS, "Owned-job ready event was not newly created.");
    View shared(mapping.get());
    InterlockedExchange(&shared.get()->child_pid, 0);
    Child unrelated(L"--child");
    Child host(L"--host \"" + name + L"-mapping\" \"" + name + L"-ready\"");
    const HANDLE waits[]{ready.get(), host.process()};
    require(WaitForMultipleObjects(2, waits, FALSE, 10000) == WAIT_OBJECT_0, "Owned helper exited/timed out before guarding its child.");
    const auto child_pid = static_cast<DWORD>(InterlockedCompareExchange(&shared.get()->child_pid, 0, 0));
    require(child_pid != 0, "Guarded child identity missing.");
    Handle child(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, child_pid));
    require(child.valid() && WaitForSingleObject(child.get(), 0) == WAIT_TIMEOUT, "Guarded child missing before host termination.");
    require(TerminateProcess(host.process(), 91) != FALSE && WaitForSingleObject(host.process(), 5000) == WAIT_OBJECT_0,
        "Could not terminate the exact owned helper.");
    require(WaitForSingleObject(child.get(), 5000) == WAIT_OBJECT_0, "Abrupt host termination left an orphan child.");
    require(WaitForSingleObject(unrelated.process(), 0) == WAIT_TIMEOUT, "Host termination killed an unrelated fixture.");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try {
        if (argc == 2 && std::wstring_view(argv[1]) == L"--child") { Sleep(INFINITE); return 0; }
        if (argc == 4 && std::wstring_view(argv[1]) == L"--host") return test_host(argv[2], argv[3]);
        require(argc == 1, "Unexpected owned-job test arguments.");
        closing_guard_terminates_only_assigned_child();
        std::cout << "PASS closing_guard_terminates_only_assigned_child\n";
        abrupt_parent_termination_does_not_leave_orphan();
        std::cout << "PASS abrupt_parent_termination_does_not_leave_orphan\n";
        std::cout << "OWNED_JOB_RESULT PASS cases=2 failed=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OWNED_JOB_RESULT FAIL: " << error.what() << '\n';
        return 1;
    }
}
