#include "native_action_state.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <initializer_list>

using skcoop::runtime::NativeActionStateReader;
namespace {
using Address=std::uintptr_t;
constexpr Address image=0x140000000,origin=0x100000;
void check(bool v){if(!v)throw std::runtime_error("Native action result read invariant");}
struct Memory {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(256*1024);
    std::size_t next{64};static inline Memory* selected{};
    Address alloc(std::size_t size){const auto at=next;next=(next+size+15)&~std::size_t{15};check(next<bytes.size());return origin+at;}
    template<class T>void put(Address at,T value){check(at>=origin&&at-origin+sizeof(T)<=bytes.size());std::memcpy(bytes.data()+at-origin,&value,sizeof(T));}
    static bool read(Address at,void* out,std::size_t size){
        if(!selected||at<origin||size>selected->bytes.size()||at-origin>selected->bytes.size()-size)return false;
        std::memcpy(out,selected->bytes.data()+at-origin,size);return true;
    }
    Address text(const char* value){const auto size=std::strlen(value)+1,at=alloc(size);std::memcpy(bytes.data()+at-origin,value,size);return at;}
    Address machine(std::initializer_list<std::pair<int,const char*>> states,int current,bool active=true){
        const auto result=alloc(0x160),array=alloc(states.size()*8);std::size_t index{};
        put(result,image+0x2c47c90);put(result+0xd0,current);put(result+0xd8,static_cast<unsigned char>(active));
        put(result+0xe0,array);put(result+0xe8,static_cast<int>(states.size()));put(result+0x140,1.f);
        for(const auto& state:states){const auto entry=alloc(0x80);put(entry,image+0x2c39e18);
            put(entry+0x68,text(state.second));put(entry+0x70,state.first);put(array+index++*8,entry);}
        return result;
    }
};
struct Fixture {
    Memory memory;NativeActionStateReader reader;Address graph{},map{},entries{},category{},idle{},guard{},attack{},crouch{};
    Fixture(){
        Memory::selected=&memory;graph=memory.alloc(0x1b8);map=memory.alloc(16);entries=memory.alloc(16*8);
        memory.put(graph,image+0x2c14788);memory.put(graph+0x120,map);memory.put(map,entries);
        category=memory.machine({{107,"Idle"},{72,"GroundAttack"},{83,"DeflectGuard"},{215,"Crouch"},
            {97,"StandMove"},{194,"StandMoveableAction"},{115,"Init"}},107);
        idle=memory.machine({{0,"StandIdle"}},0);
        guard=memory.machine({{10,"StandToDeflectGuard"},{20,"DeflectGuardIdle"}},20,false);
        attack=memory.machine({{0,"GroundAttackCombo1"},{111,"GroundAttackCombo1Release"}},0,false);
        crouch=memory.machine({{73,"CrouchIdle"},{240,"CrouchMapEnter"},{224,"CrouchAttackToStand"},{244,"CrouchAttackToStandRelease"}},73,false);
        memory.put(map+8,5);memory.put(map+12,7);
        const Address nodes[]{category,idle,guard,attack,crouch};
        for(unsigned i=0;i<5;++i){memory.put(entries+i*16,Address{0x700000+i});memory.put(entries+i*16+8,nodes[i]);}
    }
    auto observe(){return reader.observe(image,graph,&Memory::read);}
    void select(Address node,int id,bool active=true){memory.put(node+0xd0,id);memory.put(node+0xd8,static_cast<unsigned char>(active));}
};
void result_gating(){
    Fixture f;auto result=f.observe();check(result.bound&&result.idle&&!result.attacking&&!result.guarding&&result.idle_seconds==1.f);
    f.select(f.attack,0);f.select(f.guard,20); // Stale crossfade children must not create phantom actions.
    result=f.observe();check(result.bound&&result.idle&&!result.attacking&&!result.guarding);
    f.select(f.category,72);result=f.observe();check(result.bound&&result.attacking&&!result.guarding&&!result.idle);
    f.select(f.category,83);f.select(f.guard,10);result=f.observe();check(result.bound&&!result.guarding&&!result.idle&&!result.attacking);
    f.select(f.guard,20);result=f.observe();check(result.bound&&result.guarding&&!result.idle&&!result.attacking);
    f.select(f.category,194);result=f.observe();check(result.bound&&!result.guarding&&!result.idle&&!result.attacking);
    f.select(f.category,107);result=f.observe();check(result.bound&&result.idle&&!result.attacking&&!result.guarding);
    f.select(f.category,115);check(!f.observe().bound); // Unknown is never neutral.
}
void entry_and_cache(){
    Fixture f;f.select(f.idle,0,false);f.select(f.category,215);f.select(f.crouch,240);
    auto result=f.observe();check(result.bound&&!result.idle&&!result.attacking);
    f.select(f.crouch,73);result=f.observe();check(result.bound&&result.idle);
    f.select(f.crouch,224);result=f.observe();check(result.bound&&result.attacking&&!result.idle);
    f.select(f.crouch,244);check(f.observe().attacking);
    // A cached result must still validate its StateInfo pointer/type and name.
    Address array{};Memory::read(f.crouch+0xe0,&array,sizeof(array));Address entry{};Memory::read(array+3*8,&entry,sizeof(entry));
    f.memory.put(entry,Address{0});check(!f.observe().bound);
}
void malformed_and_ambiguous(){
    {Fixture f;f.memory.put(f.graph,Address{0});check(!f.observe().bound);}
    {Fixture f;f.memory.put(f.map+12,8192);check(!f.observe().bound);}
    {Fixture f;f.memory.put(f.category+0xe8,2049);check(!f.observe().bound);}
    {Fixture f;f.memory.put(f.category+0xd8,static_cast<unsigned char>(2));check(!f.observe().bound);}
    {Fixture f;f.memory.put(f.idle+0x140,std::numeric_limits<float>::quiet_NaN());check(!f.observe().bound);}
    {Fixture f;check(f.observe().bound);const auto duplicate=f.memory.machine({{107,"Idle"},{72,"GroundAttack"},{83,"DeflectGuard"},{215,"Crouch"}},107);
     f.memory.put(f.entries+5*16,Address{0x900000});f.memory.put(f.entries+5*16+8,duplicate);f.memory.put(f.map+8,6);check(!f.observe().bound);}
    {Fixture f;check(f.observe().bound);f.memory.put(f.entries+8,Address{0});check(!f.observe().bound);}
    {Fixture f;check(f.observe().bound);f.memory.put(f.graph+0x120,Address{1});check(!f.observe().bound);}
}
}
int main(){try{result_gating();entry_and_cache();malformed_and_ambiguous();
    std::cout<<"PASS simulated exact-layout read/result gating, crossfades, recovery, cache invalidation and malformed graphs; no game run\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
