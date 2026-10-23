#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <stdexcept>

namespace skcoop::launcher {

// Lifetime guard for a process created suspended by this launcher. Assignment
// must succeed before its initial thread is resumed. The noninheritable,
// unnamed job handle stays only in the parent: closing the launcher also kills
// its assigned child. Existing or externally launched processes are never
// assigned or terminated through this class.
class OwnedJob {
public:
    OwnedJob() : handle_(CreateJobObjectW(nullptr, nullptr)) {
        if (!handle_) throw std::runtime_error("Cannot create the owned-child lifetime guard.");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(handle_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            CloseHandle(handle_);
            handle_ = nullptr;
            throw std::runtime_error("Cannot configure the owned-child lifetime guard.");
        }
    }
    ~OwnedJob() { if (handle_) CloseHandle(handle_); }
    OwnedJob(const OwnedJob&) = delete;
    OwnedJob& operator=(const OwnedJob&) = delete;
    OwnedJob(OwnedJob&&) = delete;
    OwnedJob& operator=(OwnedJob&&) = delete;

    void assign(HANDLE owned_suspended_process) {
        if (!owned_suspended_process || owned_suspended_process == INVALID_HANDLE_VALUE ||
            !AssignProcessToJobObject(handle_, owned_suspended_process))
            throw std::runtime_error("Cannot guard the owned suspended child; launch is refused.");
    }
private:
    HANDLE handle_{};
};

} // namespace skcoop::launcher
