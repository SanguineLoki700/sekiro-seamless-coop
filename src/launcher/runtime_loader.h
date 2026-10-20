#pragma once
#include "skcoop/runtime_api.h"
#include <Windows.h>
#include <filesystem>

namespace skcoop::launcher {
class RuntimeLibrary {
public:
    explicit RuntimeLibrary(const std::filesystem::path& path);
    ~RuntimeLibrary();
    RuntimeLibrary(const RuntimeLibrary&) = delete;
    RuntimeLibrary& operator=(const RuntimeLibrary&) = delete;
    skcoop_abi_version_fn abi_version{};
    skcoop_health_fn health{};
    skcoop_capabilities_fn capabilities{};
    skcoop_initialize_fn initialize{};
    skcoop_shutdown_fn shutdown{};
private:
    HMODULE module_{};
};
}
