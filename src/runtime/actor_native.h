#pragma once
#include "skcoop/launch_protocol.h"
namespace skcoop::actors {
// Only called after exact executable identity and isolated-save hooks passed.
// Defers all native work until the loader lock has been released.
bool start_native_demo(launch::SharedStatus*) noexcept;
}
