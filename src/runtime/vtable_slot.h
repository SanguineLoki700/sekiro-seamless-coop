#pragma once
#include <Windows.h>
#include <cstdint>
namespace skcoop::actors {
// Compare/exchange one aligned slot, restore its page access, never overwrite a
// competing hook. Callback modules must stay pinned while calls can be in flight.
inline bool exchange_vtable_slot(void* volatile* slot,void* expected,void* replacement) noexcept {
    if(!slot || (reinterpret_cast<std::uintptr_t>(slot)&7) || !expected || !replacement)return false;
    DWORD old{};
    if(!VirtualProtect(const_cast<void**>(slot),sizeof(void*),PAGE_READWRITE,&old))return false;
    const auto previous=InterlockedCompareExchangePointer(slot,replacement,expected);
    DWORD ignored{};
    if(!VirtualProtect(const_cast<void**>(slot),sizeof(void*),old,&ignored)) {
        if(previous==expected)InterlockedCompareExchangePointer(slot,expected,replacement);
        VirtualProtect(const_cast<void**>(slot),sizeof(void*),old,&ignored);
        return false;
    }
    return previous==expected;
}
}
