#pragma once

namespace skcoop::runtime {

// Called only by DllMain at the primordial, suspended-child bootstrap boundary.
// A diagnostic LoadLibrary without a launch payload installs no hooks. A Sekiro
// process without that payload is rejected rather than run with solo saves.
[[nodiscard]] bool launch_bootstrap_attach() noexcept;

// Snapshot only during process termination. Never unload this runtime or destroy
// its save controller while the installed callbacks/native handles can be live.
void launch_bootstrap_detach() noexcept;

// Describes this process's early save bootstrap, not gameplay/actor support.
[[nodiscard]] bool launch_bootstrap_active() noexcept;

} // namespace skcoop::runtime
