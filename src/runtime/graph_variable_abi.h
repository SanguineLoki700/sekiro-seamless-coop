#pragma once
#include <cstddef>
#include <cstdint>
namespace skcoop::actors {
// Observed native variable handle: index, hkbCharacter, override table.
// Both native callers use stack storage. None of these helpers retains it.
struct alignas(8) GraphVariable {
    std::int32_t index{-1};
    std::uint32_t padding{};
    std::uintptr_t character{},overrides{};
};
static_assert(sizeof(GraphVariable)==24 && offsetof(GraphVariable,character)==8 && offsetof(GraphVariable,overrides)==16);
using MakeGraphVariable=void* (*)(void*,GraphVariable*,int);
using ReadGraphFloat=float (*)(const GraphVariable*);
using WriteGraphFloat=void (*)(const GraphVariable*,float); // XMM1 on Win64.
}
