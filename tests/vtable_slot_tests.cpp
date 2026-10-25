#include "vtable_slot.h"
#include <array>
#include <iostream>
#include <stdexcept>
using skcoop::actors::exchange_vtable_slot;
namespace {
unsigned calls{};
std::array<void*,3> observed{};
void first(void* a,void* b,void* c) { ++calls; observed={a,b,c}; }
void second(void* a,void* b,void* c) { calls+=2; observed={a,b,c}; }
void require(bool v) { if(!v)throw std::runtime_error("Owned vtable exchange invariant failed"); }
}
int main() {
    auto* memory=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!memory)return 1;
    try {
        auto* slot=static_cast<void* volatile*>(memory); *slot=reinterpret_cast<void*>(&first);
        DWORD old{}; require(VirtualProtect(memory,4096,PAGE_READONLY,&old)!=FALSE);
        require(exchange_vtable_slot(slot,reinterpret_cast<void*>(&first),reinterpret_cast<void*>(&second)));
        MEMORY_BASIC_INFORMATION page{}; require(VirtualQuery(memory,&page,sizeof(page))==sizeof(page) && page.Protect==PAGE_READONLY);
        using Call=void (*)(void*,void*,void*);
        const std::array<void*,3> arguments{memory,static_cast<unsigned char*>(memory)+8,reinterpret_cast<void*>(&calls)};
        reinterpret_cast<Call>(*slot)(arguments[0],arguments[1],arguments[2]);
        require(calls==2 && observed==arguments);
        require(!exchange_vtable_slot(slot,reinterpret_cast<void*>(&first),reinterpret_cast<void*>(&first)));
        require(*slot==reinterpret_cast<void*>(&second));
        require(exchange_vtable_slot(slot,reinterpret_cast<void*>(&second),reinterpret_cast<void*>(&first)));
        require(!exchange_vtable_slot(nullptr,reinterpret_cast<void*>(&first),reinterpret_cast<void*>(&second)));
        require(!exchange_vtable_slot(reinterpret_cast<void* volatile*>(static_cast<unsigned char*>(memory)+1),reinterpret_cast<void*>(&first),reinterpret_cast<void*>(&second)));
        require(!exchange_vtable_slot(slot,nullptr,reinterpret_cast<void*>(&second)));
        require(VirtualQuery(memory,&page,sizeof(page))==sizeof(page) && page.Protect==PAGE_READONLY);
        VirtualFree(memory,0,MEM_RELEASE); std::cout<<"PASS atomic ownership, three-argument forwarding and read-only restoration\n";
    } catch(const std::exception& e) { VirtualFree(memory,0,MEM_RELEASE); std::cerr<<e.what()<<'\n'; return 1; }
}
