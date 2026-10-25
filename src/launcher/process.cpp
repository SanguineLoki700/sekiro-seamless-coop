#include "launcher_process.h"
#include <Windows.h>
#include <stdexcept>

namespace skcoop::launcher {
std::filesystem::path executable_directory() {
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) throw std::runtime_error("Cannot resolve the diagnostic executable directory.");
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}
std::wstring quote_argument(const std::wstring& argument) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto character : argument) {
        if (character == L'\\') { ++slashes; continue; }
        if (character == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
        } else {
            result.append(slashes, L'\\');
        }
        slashes = 0;
        result.push_back(character);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}
int run_controlled_child(const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments, unsigned long timeout_ms) {
    const auto full = std::filesystem::absolute(executable);
    std::wstring command = quote_argument(full.wstring());
    for (const auto& argument : arguments) command += L" " + quote_argument(argument);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(full.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, full.parent_path().c_str(), &startup, &process))
        throw std::runtime_error("Cannot start the controlled diagnostic child.");
    CloseHandle(process.hThread);
    const auto wait = WaitForSingleObject(process.hProcess, timeout_ms);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hProcess);
        throw std::runtime_error("Controlled diagnostic child timed out or could not be observed.");
    }
    DWORD exit_code = 1;
    const auto ok = GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    if (!ok || exit_code > 255) throw std::runtime_error("Controlled diagnostic child failed unexpectedly.");
    return static_cast<int>(exit_code);
}
}
