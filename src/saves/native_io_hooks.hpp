#pragma once

#include "skcoop/save_native_policy.hpp"
#include "skcoop/save_store.hpp"

namespace skcoop::saves {

struct NativeIoBinding {
    SavePolicyConfig policy;
    SaveProcessContext process;
    std::wstring account;
    // Caller owns this store, constructed for exactly the bound cooperative
    // account directory and protected solo tree. It outlives hooks AND handles.
    CoOpSaveStore* store{};
};

enum class NativeIoHookError {
    none, already_installed, busy, invalid_binding, directory_validation_failed,
    transaction_failed, allocation_failure,
};

struct NativeIoCounters {
    std::uint64_t redirected{};
    std::uint64_t denied{};
    std::uint64_t passthrough{};
    std::uint64_t internal_bypass{};
    std::uint64_t appdata_redirects{};
    std::uint64_t directory_queries{};
    std::uint64_t copy_failures{};
    std::uint64_t destination_may_be_modified{};
};

struct NativeIoHookStatus {
    NativeIoHookError error{NativeIoHookError::none};
    std::uint32_t system_error{};
    bool installed{};
    NativeIoCounters counters;
    [[nodiscard]] explicit operator bool() const noexcept { return error == NativeIoHookError::none; }
};

// Fixture-stage bootstrap boundary: caller guarantees a quiescent primordial
// process, before application TLS/initializers/save use. Only the current thread
// is enlisted in Detours. No running-process attach or concurrent unload claim.
// The caller independently establishes executable/account facts in the binding.
// Binding is copied; private directories must already exist and be pinned by
// the matching store. No import, creation of profiles or solo fallback occurs.
// State intentionally survives process teardown when hooks remain installed;
// do not unload the module or destroy the store with live hooks/handles.
[[nodiscard]] NativeIoHookStatus install_native_io_hooks(const NativeIoBinding&) noexcept;
// Explicit uninstall is allowed only after callers are quiescent. It does not
// close native file handles or release the caller's store. Not used at process
// termination from DllMain; process teardown releases process-owned resources.
[[nodiscard]] NativeIoHookStatus uninstall_native_io_hooks() noexcept;
[[nodiscard]] NativeIoHookStatus native_io_hook_status() noexcept;

} // namespace skcoop::saves
