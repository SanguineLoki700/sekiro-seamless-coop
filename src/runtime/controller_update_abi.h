#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace skcoop::actors {
// Win64 positional argument 2 is a float in XMM1; argument 3 is in R8.
// Observed at PadManipulator update 7ef120, not inferred from pseudocode.
using ControllerUpdate = void (*)(void*,float,void*);
struct alignas(16) NativeUpdateTime {
    std::uintptr_t vtable;
    float delta;
    std::uint32_t reserved{};
};
static_assert(sizeof(NativeUpdateTime)==16 && offsetof(NativeUpdateTime,delta)==8);
inline bool valid_update_delta(float value) noexcept {
    return std::isfinite(value) && value>=0 && value<=.25f;
}
}
