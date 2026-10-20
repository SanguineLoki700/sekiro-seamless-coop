#include "runtime_loader.h"
#include <cstring>
#include <stdexcept>

namespace skcoop::launcher {
namespace {
template<typename Function> Function resolve(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name);
    if (!address) throw std::runtime_error("Runtime is missing a required ABI export.");
    Function function{};
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}
}
RuntimeLibrary::RuntimeLibrary(const std::filesystem::path& path) {
    const auto full = std::filesystem::absolute(path);
    module_ = LoadLibraryExW(full.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module_) throw std::runtime_error("Cannot load the selected diagnostic runtime DLL.");
    try {
        abi_version = resolve<skcoop_abi_version_fn>(module_, "skcoop_runtime_abi_version");
        health = resolve<skcoop_health_fn>(module_, "skcoop_runtime_health");
        capabilities = resolve<skcoop_capabilities_fn>(module_, "skcoop_runtime_capabilities");
        initialize = resolve<skcoop_initialize_fn>(module_, "skcoop_runtime_initialize");
        shutdown = resolve<skcoop_shutdown_fn>(module_, "skcoop_runtime_shutdown");
        if (abi_version() != SKCOOP_RUNTIME_ABI_VERSION) throw std::runtime_error("Runtime ABI version does not match this launcher.");
    } catch (...) {
        FreeLibrary(module_);
        module_ = nullptr;
        throw;
    }
}
RuntimeLibrary::~RuntimeLibrary() {
    if (module_) {
        shutdown();
        FreeLibrary(module_);
    }
}
}
