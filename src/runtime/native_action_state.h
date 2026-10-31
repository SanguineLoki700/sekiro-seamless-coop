#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

namespace skcoop::runtime {
// Read-only result binding for the pinned 1.06 executable. Offsets come from
// that artifact's reflection/RTTI and were independently checked on live clones.
// These names classify engine OUTPUTS. They never select or enqueue an action.
struct NativeActionResult {
    bool bound{},attacking{},guarding{},idle{};
    float idle_seconds{};
    int category{std::numeric_limits<int>::min()};
};
class NativeActionStateReader {
public:
    using Address=std::uintptr_t;
    using Read=bool (*)(Address,void*,std::size_t);
    NativeActionResult observe(Address base,Address graph,Read read) noexcept {
        NativeActionResult result;
        read_=read;base_=base;budget_=1024*1024;
        try {
            if(!typed(graph,0x2c14788))return {};
            if(graph_!=graph){graph_=graph;canonical_=0;machine_count_=0;cache_={};cache_next_=0;}
            const auto map=get<Address>(graph+0x120),data=get<Address>(map);
            const auto count=get<int>(map+8),mask=get<int>(map+12);
            if(mask<0||mask>8191||((mask+1)&mask)||count<0||count>mask+1||!data)return {};
            std::array<Address,128> machines{};std::size_t size{};
            for(int i=0;i<=mask;++i){
                const auto slot=data+static_cast<Address>(i)*16,key=get<Address>(slot);
                if(!key||key==std::numeric_limits<Address>::max())continue;
                const auto node=get<Address>(slot+8);if(!node||!typed(node,0x2c47c90))continue;
                bool duplicate=false;for(std::size_t j=0;j<size;++j)duplicate|=machines[j]==node;
                if(duplicate)continue;
                if(size==machines.size())return {};
                machines[size++]=node;
            }
            bool changed=size!=machine_count_;
            for(std::size_t i=0;i<size&&!changed;++i)changed=machines[i]!=machines_[i];
            if(changed||!canonical_){
                canonical_=0;
                for(std::size_t i=0;i<size;++i)if(category_signature(machines[i])){
                    if(canonical_)return {}; // Ambiguous ownership within this graph.
                    canonical_=machines[i];
                }
                machines_=machines;machine_count_=size;
            }
            if(!canonical_)return {};
            const auto category=current(canonical_);
            if(!category.active)return {};
            result.category=category.id;
            const auto name=category.label();
            // The parent's active category gates children. During crossfades
            // old attack/guard children may remain active under a new Idle.
            if(name=="GroundAttack"&&category.id==72){result.bound=result.attacking=true;}
            else if(name=="DeflectGuard"&&category.id==83){
                result.bound=true;unsigned holds{};
                for(std::size_t i=0;i<size;++i)if(machines[i]!=canonical_){
                    const auto leaf=current(machines[i]);
                    if(leaf.active&&leaf.id==20&&leaf.label()=="DeflectGuardIdle")++holds;
                }
                if(holds>1)return {};
                result.guarding=holds==1; // Entry is not a maintained guard.
            }else if((name=="Idle"&&category.id==107)||(name=="Crouch"&&category.id==215)){
                result.bound=true;unsigned idles{};
                for(std::size_t i=0;i<size;++i)if(machines[i]!=canonical_){
                    const auto leaf=current(machines[i]);if(!leaf.active)continue;
                    if((category.id==107&&leaf.id==0&&leaf.label()=="StandIdle")||
                       (category.id==215&&leaf.id==73&&leaf.label()=="CrouchIdle")){
                        ++idles;result.idle_seconds=leaf.seconds;
                    }
                    if(category.id==215&&((leaf.id==224&&leaf.label()=="CrouchAttackToStand")||
                       (leaf.id==244&&leaf.label()=="CrouchAttackToStandRelease")))result.attacking=true;
                }
                if(idles>1)return {};
                result.idle=idles==1&&!result.attacking;
            }else if((name=="StandMove"&&category.id==97)||
                     (name=="StandMoveableAction"&&category.id==194))result.bound=true;
            // An unclassified category remains unknown, never a neutral result.
            if(get<Address>(graph+0x120)!=map||get<Address>(map)!=data||get<int>(map+12)!=mask||
               get<int>(map+8)!=count||!same_current(category))return {};
            return result;
        }catch(...){return {};}
    }
private:
    struct Invalid {};
    struct State {
        Address machine{};int id{};bool active{};float seconds{};
        std::array<char,128> name{};
        std::string_view label()const noexcept{return name.data();}
    };
    struct Cached {
        Address machine{},array{},entry{},name{};int count{},id{},index{};
        std::array<char,128> label{};
    };
    Address base_{},graph_{},canonical_{};
    Read read_{};std::size_t budget_{},cache_next_{};
    std::array<Address,128> machines_{};std::size_t machine_count_{};
    std::array<Cached,64> cache_{};
    template<class T>T get(Address address){
        T value{};
        if(!address||address+sizeof(T)<address||sizeof(T)>budget_||!read_||!read_(address,&value,sizeof(T)))throw Invalid{};
        budget_-=sizeof(T);return value;
    }
    bool typed(Address object,Address table){return object&&get<Address>(object)==base_+table;}
    std::array<char,128> label(Address address){
        address&=~Address{1};if(!address)throw Invalid{};
        std::array<char,128> result{};
        for(std::size_t i=0;i<result.size();++i){const auto ch=get<unsigned char>(address+i);
            if(!ch)return result;if(ch<32||ch>126)throw Invalid{};result[i]=static_cast<char>(ch);}
        throw Invalid{};
    }
    bool signature_member(Address array,int count,int id,std::string_view name){
        Address found{};
        for(int i=0;i<count;++i){const auto entry=get<Address>(array+static_cast<Address>(i)*8);
            if(!entry||!typed(entry,0x2c39e18)||get<int>(entry+0x70)!=id)continue;
            if(found)return false;found=entry;
        }
        return found&&std::string_view(label(get<Address>(found+0x68)).data())==name;
    }
    bool category_signature(Address machine){
        const auto array=get<Address>(machine+0xe0);const auto count=get<int>(machine+0xe8);
        if(!array||count<4||count>2048)return false;
        return signature_member(array,count,107,"Idle")&&signature_member(array,count,72,"GroundAttack")&&
            signature_member(array,count,83,"DeflectGuard")&&signature_member(array,count,215,"Crouch");
    }
    bool same_current(const State& state){return typed(state.machine,0x2c47c90)&&
        get<unsigned char>(state.machine+0xd8)==1&&get<int>(state.machine+0xd0)==state.id;}
    State current(Address machine){
        State result;result.machine=machine;
        if(!typed(machine,0x2c47c90))throw Invalid{};
        const auto active=get<unsigned char>(machine+0xd8);if(active>1)throw Invalid{};
        if(!active)return result;
        result.id=get<int>(machine+0xd0);
        const auto array=get<Address>(machine+0xe0);const auto count=get<int>(machine+0xe8);
        if(!array||count<1||count>2048)throw Invalid{};
        Cached* selected=nullptr;
        for(auto& item:cache_)if(item.machine==machine&&item.array==array&&item.count==count&&item.id==result.id){
            if(get<Address>(array+static_cast<Address>(item.index)*8)==item.entry&&typed(item.entry,0x2c39e18)&&
               get<int>(item.entry+0x70)==result.id&&get<Address>(item.entry+0x68)==item.name)selected=&item;
            break;
        }
        if(!selected){
            Address found{};int index{};
            for(int i=0;i<count;++i){const auto entry=get<Address>(array+static_cast<Address>(i)*8);
                if(!entry||!typed(entry,0x2c39e18)||get<int>(entry+0x70)!=result.id)continue;
                if(found)throw Invalid{};found=entry;index=i;
            }
            if(!found)throw Invalid{};
            auto& item=cache_[cache_next_++%cache_.size()];
            item={machine,array,found,get<Address>(found+0x68),count,result.id,index,{}};
            item.label=label(item.name);selected=&item;
        }
        result.name=selected->label;result.seconds=get<float>(machine+0x140);
        if(!std::isfinite(result.seconds)||result.seconds<0||result.seconds>1e7f||!same_current(result))throw Invalid{};
        result.active=true;return result;
    }
};
}
