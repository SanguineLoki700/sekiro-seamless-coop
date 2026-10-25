#include "vtable_slot.h"
#include "controller_update_abi.h"
#include "graph_variable_abi.h"
#include <array>
#include <iostream>
#include <stdexcept>
using skcoop::actors::exchange_vtable_slot;
namespace {
unsigned calls{};
std::array<void*,2> observed{};
float observed_delta{};
float graph_value{};
void graph_set(const skcoop::actors::GraphVariable* h,float value) { if(h->index==213)graph_value=value; }
float graph_get(const skcoop::actors::GraphVariable* h) { return h->index==213?graph_value:0.f; }
void first(void* a,float delta,void* c) { ++calls; observed={a,c}; observed_delta=delta; }
void second(void* a,float delta,void* c) { calls+=2; observed={a,c}; observed_delta=delta; }
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
        const std::array<void*,2> arguments{memory,reinterpret_cast<void*>(&calls)};
        reinterpret_cast<skcoop::actors::ControllerUpdate>(*slot)(arguments[0],.125f,arguments[1]);
        require(calls==2 && observed==arguments && observed_delta==.125f);
        const skcoop::actors::NativeUpdateTime time{0x12345678,.016f,0};
        require(reinterpret_cast<const unsigned char*>(&time.delta)-reinterpret_cast<const unsigned char*>(&time)==8);
        require(skcoop::actors::valid_update_delta(time.delta) && !skcoop::actors::valid_update_delta(-1.f) && !skcoop::actors::valid_update_delta(.5f));
        const skcoop::actors::GraphVariable variable{213,0,0x1234,0x5678};
        const skcoop::actors::WriteGraphFloat set_graph=&graph_set;
        const skcoop::actors::ReadGraphFloat get_graph=&graph_get;
        set_graph(&variable,.75f);require(get_graph(&variable)==.75f && variable.character==0x1234 && variable.overrides==0x5678);
        require(!exchange_vtable_slot(slot,reinterpret_cast<void*>(&first),reinterpret_cast<void*>(&first)));
        require(*slot==reinterpret_cast<void*>(&second));
        require(exchange_vtable_slot(slot,reinterpret_cast<void*>(&second),reinterpret_cast<void*>(&first)));
        require(!exchange_vtable_slot(nullptr,reinterpret_cast<void*>(&first),reinterpret_cast<void*>(&second)));
        require(!exchange_vtable_slot(reinterpret_cast<void* volatile*>(static_cast<unsigned char*>(memory)+1),reinterpret_cast<void*>(&first),reinterpret_cast<void*>(&second)));
        require(!exchange_vtable_slot(slot,nullptr,reinterpret_cast<void*>(&second)));
        require(VirtualQuery(memory,&page,sizeof(page))==sizeof(page) && page.Protect==PAGE_READONLY);
        VirtualFree(memory,0,MEM_RELEASE); std::cout<<"PASS atomic ownership, XMM1 float forwarding, time-object layout and read-only restoration\n";
    } catch(const std::exception& e) { VirtualFree(memory,0,MEM_RELEASE); std::cerr<<e.what()<<'\n'; return 1; }
}
