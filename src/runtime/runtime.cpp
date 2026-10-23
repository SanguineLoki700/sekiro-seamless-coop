#include "skcoop/runtime_api.h"
#include "launch_bootstrap.h"
#include <Windows.h>
#include <atomic>

namespace {
HMODULE module_handle = nullptr;
std::atomic<uint32_t> runtime_state{SKCOOP_STATE_LOADED};
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        module_handle = module;
        return skcoop::runtime::launch_bootstrap_attach() ? TRUE : FALSE;
    }
    if (reason == DLL_PROCESS_DETACH && reserved != nullptr)
        skcoop::runtime::launch_bootstrap_detach();
    return TRUE;
}

extern "C" SKCOOP_API uint32_t skcoop_runtime_abi_version(void) {
    return SKCOOP_RUNTIME_ABI_VERSION;
}

extern "C" SKCOOP_API uint32_t skcoop_runtime_health(skcoop_health* output) {
    if (!output || output->struct_size != sizeof(skcoop_health)) return SKCOOP_INVALID_ARGUMENT;
    *output = {sizeof(skcoop_health), SKCOOP_RUNTIME_ABI_VERSION, runtime_state.load(), 0};
    return module_handle ? SKCOOP_OK : SKCOOP_UNIMPLEMENTED;
}

extern "C" SKCOOP_API uint32_t skcoop_runtime_capabilities(skcoop_capabilities* output) {
    if (!output || output->struct_size != sizeof(skcoop_capabilities)) return SKCOOP_INVALID_ARGUMENT;
    *output = {sizeof(skcoop_capabilities), SKCOOP_RUNTIME_ABI_VERSION,
        SKCOOP_CAP_DIAGNOSTICS, SKCOOP_NOT_IMPLEMENTED, SKCOOP_NOT_IMPLEMENTED, SKCOOP_NOT_IMPLEMENTED};
    return SKCOOP_OK;
}

extern "C" SKCOOP_API uint32_t skcoop_runtime_initialize(const skcoop_init_options* options) {
    if (!options || options->struct_size != sizeof(skcoop_init_options) || options->reserved != 0)
        return SKCOOP_INVALID_ARGUMENT;
    if (options->abi_version != SKCOOP_RUNTIME_ABI_VERSION) return SKCOOP_ABI_MISMATCH;
    if (options->mode == SKCOOP_MODE_GAMEPLAY) return SKCOOP_UNIMPLEMENTED;
    if (options->mode != SKCOOP_MODE_DIAGNOSTIC) return SKCOOP_INVALID_ARGUMENT;
    runtime_state.store(SKCOOP_STATE_DIAGNOSTIC_READY);
    return SKCOOP_OK;
}

extern "C" SKCOOP_API uint32_t skcoop_runtime_shutdown(void) {
    if (skcoop::runtime::launch_bootstrap_active()) return SKCOOP_UNIMPLEMENTED;
    runtime_state.store(SKCOOP_STATE_LOADED);
    return SKCOOP_OK;
}
