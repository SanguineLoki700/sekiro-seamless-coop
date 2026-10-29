#include "actor_native.h"
#include "actor_demo.h"
#include "remote_actor.h"
#include "lan_payload.h"
#include "skcoop/lan/peer.hpp"
#include "region_queries.h"
#include "locomotion_input.h"
#include "vtable_slot.h"
#include "controller_update_abi.h"
#include "graph_variable_abi.h"
#include "native_call_scope.h"
#include "action_receiver.h"
#include "action_native_contract.h"
#include "skcoop/actors/controller_scenario.hpp"
#include "skcoop/actors/native_input_latch.hpp"
#include "skcoop/actors/native_command_mailbox.hpp"
#include "skcoop/actors/native_input_projection.hpp"
#include <detours.h>
#include <TlHelp32.h>
#include <bcrypt.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <numbers>
#include <limits>
#include <cstring>
#include <stdexcept>

namespace skcoop::actors {
namespace {
std::uintptr_t base{};
launch::SharedStatus* status{};
Demo demo;
RemoteActor remote_actor;
bool lan_mode{},local_loaded{};
bool native_commands_mode{};
void adopt_native_command_actor(Token);
launch::Payload lan_launch{};
lan::Peer* network{}; // Pinned with the runtime. Never join/destroy under the loader lock.
lan::View network_view{};
Token source_world{},source_primary{},source_registry{};
ULONGLONG offered_at{};
void fail_demo(Error e) noexcept { if(lan_mode)remote_actor.fail(e);else demo.fail(e);if(network)network->request_stop(); }
volatile LONG entered{};
DWORD native_thread{};
bool armed{};
ULONGLONG armed_at{};
bool pause_key_down{};
std::uint64_t creation_calls{}, world_callbacks{}, callback_total_us{}, callback_max_us{}, factory_us{}, max_interval_us{}, previous_tick{};
std::uint64_t locomotion_writes{},zero_commands{};
std::uint64_t controller_callbacks{},controller_context_errors{};
std::uint64_t source_captures{},source_nonzero{},action_finalizes{},invalid_deltas{};
void* volatile bound_controller{};
void* volatile bound_source{};
Token bound_actor{};
bool input_enabled{};
ULONGLONG input_deadline{};
LocomotionInput captured_input{};
ULONGLONG captured_at{};
bool captured_valid{};
bool graph_locomotion{};
std::uint64_t graph_writes{},graph_nonzero{};
LARGE_INTEGER frequency{};
thread_local RegionQueries regions;
bool actions_mode{},combat_preview{};
SRWLOCK action_ingress=SRWLOCK_INIT;
bool action_consumer_active{}; // protected by action_ingress
struct InputFailure { Error error; };
std::uint64_t clock_ticks() noexcept { LARGE_INTEGER n{}; QueryPerformanceCounter(&n); return static_cast<std::uint64_t>(n.QuadPart); }
std::uint64_t microseconds(std::uint64_t ticks) noexcept {
    return ticks*1000000/static_cast<std::uint64_t>(frequency.QuadPart);
}
using Update = void (*)(void*, void*);
Update original_update{};
ControllerUpdate original_controller_update{};
ControllerUpdate original_source_update{};
constexpr std::uintptr_t creator_vtable = 0x2a952a8;
constexpr std::uintptr_t consumer = 0xc0f7a0;
std::uintptr_t guest_controller_table() noexcept { return demo.native_pad_reference?0x29d4b30:0x29d2ae0; }

bool readable(std::uintptr_t p, std::size_t n, bool executable=false) noexcept {
    if (!p || !n || p+n < p) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (!regions.query(p,m) ||
        m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        p+n > reinterpret_cast<std::uintptr_t>(m.BaseAddress)+m.RegionSize) return false;
    const DWORD access=m.Protect & 0xff;
    return executable ? (m.Type == MEM_IMAGE && reinterpret_cast<std::uintptr_t>(m.AllocationBase)==base &&
        (access==PAGE_EXECUTE_READ || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY)) :
        (access==PAGE_READONLY || access==PAGE_READWRITE || access==PAGE_WRITECOPY ||
         access==PAGE_EXECUTE_READ || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY);
}
template<class T> T read(std::uintptr_t p) {
    if (!readable(p,sizeof(T))) throw std::runtime_error("Unreadable native relationship");
    T value{}; std::memcpy(&value,reinterpret_cast<const void*>(p),sizeof(T)); return value;
}
std::uintptr_t pointer(std::uintptr_t p) { return read<std::uintptr_t>(p); }
bool typed(std::uintptr_t p, std::uintptr_t table) { return p && pointer(p)==base+table; }
bool model(std::uintptr_t p) {
    const auto component=pointer(p+0x50), root=pointer(p+0x1ff8);
    if (!component || !root) return false;
    if (pointer(component+0x10)!=p) throw std::runtime_error("Component owner changed");
    const auto node=pointer(root+0x68);
    return node && readable(node+0x80,32);
}
Vector native_vector(std::uintptr_t rva, Token p) {
    alignas(16) Vector v{};
    using Getter=void* (*)(void*,float*);
    reinterpret_cast<Getter>(base+rva)(reinterpret_cast<void*>(p),v.data());
    return v;
}
class NativeGame final : public RemoteGame {
public:
    void* creator{};
    Frame frame{};
    bool sample(Frame& out) override {
        const auto world=pointer(base+0x3d7a1e0);
        if (!world) return false;
        if (!typed(world,0x2a30058) || pointer(world+0x3148)!=reinterpret_cast<std::uintptr_t>(creator) ||
            !typed(reinterpret_cast<std::uintptr_t>(creator),creator_vtable)) throw std::runtime_error("World/creator changed");
        const auto primary=pointer(world+0x88);
        if (!primary) return false;
        const auto count=read<std::int32_t>(world+0x40);
        const auto array=pointer(world+0x48);
        if (!typed(primary,0x2a2b338) || count!=6 || !array || pointer(array)!=primary ||
            !typed(pointer(primary+0x58),0x29d4b30)) throw std::runtime_error("Player registry/type changed");
        frame={world,primary,array,pointer(array+0x38),read<std::uint32_t>(primary+0x78),model(primary),{},{},GetTickCount64()};
        if (frame.primary_ready) {
            frame.position=native_vector(0x9f0870,primary);
            frame.forward=native_vector(0x9eeb40,primary);
        }
        out=frame; return true;
    }
    Token create() override {
        // Recheck sources at the exact game-thread creation boundary.
        const auto world=frame.world, p=frame.primary, c=reinterpret_cast<std::uintptr_t>(creator);
        if (read<std::uint32_t>(c+0x40) || read<std::uint32_t>(c+0x44) ||
            pointer(world+0x88)!=p || pointer(frame.registry)!=p || frame.field_handle>0xffff)
            throw std::runtime_error("Creator not idle");
        for (unsigned i=1;i<6;++i) if (pointer(frame.registry+i*0x38)) throw std::runtime_error("Additional slot occupied");
        const auto data_owner=pointer(base+0x3d67408), source_owner=pointer(base+0x3d5aac0);
        if (!data_owner || !source_owner) throw std::runtime_error("Missing factory sources");
        const auto data=data_owner+0x31e0, source=pointer(source_owner+8);
        if (!source || data!=pointer(p+0x21f8) || source!=pointer(p+0x2000)) throw std::runtime_error("Factory sources changed");
        const auto argument=read<std::uint32_t>(p+0x70);
        // The successful experiment covered this descriptor family only.
        if (argument!=0 || frame.field_handle!=0xa000) throw std::runtime_error("Unsupported spawn descriptor");
        struct alignas(16) Parameters {
            std::uint32_t source_handle, manipulator;
            std::uintptr_t source, unused, data;
            std::uint32_t flags;
            std::uint8_t padding[12];
        };
        static_assert(sizeof(Parameters)==48 && offsetof(Parameters,data)==24);
        // Kind1 uses the game's full 0x450-byte allocation and Pad constructor.
        // Never replace a base object's vtable with the larger derived class.
        const Parameters params{frame.field_handle&0x7ff,demo.native_pad_reference?1u:0u,source,0,data,0,{}};
        using Create=void* (*)(void*,std::uint32_t,const void*);
        regions.clear(); // The factory can allocate/remap native objects.
        const auto start=clock_ticks(); ++creation_calls;
        const auto result=reinterpret_cast<std::uintptr_t>(reinterpret_cast<Create>(base+0xa49460)(
            reinterpret_cast<void*>(world),argument,&params));
        factory_us=microseconds(clock_ticks()-start);
        regions.clear();
        const auto guest=pointer(frame.registry+0x38);
        if (!guest || guest!=result || !typed(guest,0x2a2b338) ||
            !typed(pointer(guest+0x58),guest_controller_table()) || pointer(pointer(guest+0x58)+0x300)!=guest ||
            pointer(world+0x88)!=p || pointer(frame.registry)!=p)
            throw std::runtime_error("Factory result/registry mismatch");
        if(native_commands_mode)adopt_native_command_actor(guest);
        return guest;
    }
    bool actor_ready(Token p) override {
        if (!typed(p,0x2a2b338) || !typed(pointer(p+0x58),guest_controller_table())) throw std::runtime_error("Guest type changed");
        return model(p);
    }
    Vector position(Token p) override { return native_vector(0x9f0870,p); }
    void place(Token p, const Vector& value) override {
        alignas(16) Vector aligned=value;
        using Setter=void (*)(void*,const float*);
        reinterpret_cast<Setter>(base+0x9f6320)(reinterpret_cast<void*>(p),aligned.data());
    }
    void command_locomotion(Token actor,bool enabled) override {
        if(graph_locomotion) { apply_graph_locomotion(actor,enabled); return; }
        const auto source=reinterpret_cast<void*>(pointer(frame.primary+0x58));
        if(actor!=bound_actor || source!=InterlockedCompareExchangePointer(&bound_source,nullptr,nullptr))captured_valid=false;
        bound_actor=actor; input_enabled=enabled; input_deadline=GetTickCount64()+250;
        InterlockedExchangePointer(&bound_source,source);
        InterlockedExchangePointer(&bound_controller,reinterpret_cast<void*>(pointer(actor+0x58)));
        // Release on the world callback too, so a stopped controller cannot keep
        // a previous input latched. Active input is applied at its native phase.
        if(!enabled)apply_locomotion(actor,false);
    }
    void disarm() noexcept { InterlockedExchangePointer(&bound_controller,nullptr); InterlockedExchangePointer(&bound_source,nullptr); bound_actor=0; input_enabled=false; captured_valid=false; }
    GraphVariable graph_variable(Token actor,unsigned ordinal,int expected) {
        const auto module=pointer(pointer(actor+0x1ff8)+0x28);
        if(!typed(module,0x2a790a8) || pointer(module+8)!=actor || read<std::uint64_t>(module+0xaf0)!=91)
            throw InputFailure{Error::input_owner};
        const auto graph=pointer(module+0x10);
        if(!graph || !readable(graph,0xa0) || read<int>(module+0x980+ordinal*4)!=expected)
            throw InputFailure{Error::input_value};
        GraphVariable result{};
        const auto returned=reinterpret_cast<MakeGraphVariable>(base+0xffe740)(reinterpret_cast<void*>(graph),&result,expected);
        if(returned!=&result || result.index!=expected || result.character!=pointer(graph+0x28) ||
            result.overrides!=graph+0x98 || !readable(result.character,0xa0))throw InputFailure{Error::input_owner};
        return result;
    }
    void apply_graph_locomotion(Token actor,bool enabled) {
        const auto primary=frame.primary;
        if(actor==primary || pointer(frame.world+0x88)!=primary || pointer(frame.registry)!=primary ||
            pointer(frame.registry+0x38)!=actor || !typed(actor,0x2a2b338))throw InputFailure{Error::input_owner};
        const auto destination=pointer(actor+0x58),source=pointer(primary+0x58);
        if(!typed(destination,0x29d2ae0) || !typed(source,0x29d4b30) ||
            pointer(destination+0x300)!=actor || pointer(source+0x300)!=primary)throw InputFailure{Error::input_owner};
        const auto guest_speed=graph_variable(actor,2,213),guest_angle=graph_variable(actor,1,209);
        const auto source_speed=graph_variable(primary,2,213),source_angle=graph_variable(primary,1,209);
        if(source_speed.character==guest_speed.character || source_speed.overrides==guest_speed.overrides)
            throw InputFailure{Error::input_owner};
        const auto set=reinterpret_cast<WriteGraphFloat>(base+0xff3570);
        const auto get=reinterpret_cast<ReadGraphFloat>(base+0xff3250);
        // Release before reading new source data; validation failure cannot latch
        // the previous frame's movement on an otherwise valid owned graph.
        set(&guest_speed,0);set(&guest_angle,0);graph_writes+=2;
        for(const auto offset:{0x20u,0x40u}) {
            MEMORY_BASIC_INFORMATION m{};
            if(!readable(destination+offset,16) || !regions.query(destination+offset,m) ||
                ((m.Protect&0xff)!=PAGE_READWRITE && (m.Protect&0xff)!=PAGE_WRITECOPY))throw InputFailure{Error::input_range};
        }
        const Vector zero{};std::memcpy(reinterpret_cast<void*>(destination+0x20),zero.data(),16);
        if(!enabled) { ++zero_commands;return; }
        const float speed=get(&source_speed),angle=get(&source_angle);
        if(!std::isfinite(speed)||speed<0||speed>2.1f||!std::isfinite(angle)||std::abs(angle)>360)
            throw InputFailure{Error::input_value};
        LocomotionInput orientation{};orientation.rotation_target=read<Vector>(source+0x40);
        if(!relative_rotation(orientation.rotation_target,native_vector(0x9eec10,actor),orientation.rotation_delta) ||
            !valid_locomotion_input(orientation))throw InputFailure{Error::input_value};
        std::memcpy(reinterpret_cast<void*>(destination+0x20),orientation.rotation_delta.data(),16);
        std::memcpy(reinterpret_cast<void*>(destination+0x40),orientation.rotation_target.data(),16);
        const float bounded_speed=std::min(speed,1.f); // Walking/jog reference only.
        set(&guest_speed,bounded_speed);set(&guest_angle,angle);graph_writes+=2;
        const auto observed=get(&guest_speed);
        if(!std::isfinite(observed)||std::abs(observed-bounded_speed)>.01f) { set(&guest_speed,0);++graph_writes;throw InputFailure{Error::input_value}; }
        if(bounded_speed>.001f)++graph_nonzero;
    }
    lan::Motion local_motion() {
        lan::Motion motion;motion.epoch=1;motion.descriptor=frame.field_handle;
        const auto rotation=native_vector(0x9eec10,frame.primary);
        std::copy_n(frame.position.begin(),3,motion.position.begin());std::copy_n(rotation.begin(),3,motion.rotation.begin());
        const auto speed=graph_variable(frame.primary,2,213),angle=graph_variable(frame.primary,1,209);
        const auto get=reinterpret_cast<ReadGraphFloat>(base+0xff3250);
        const float value=get(&speed);motion.angle=get(&angle);
        if(!std::isfinite(value)||value<0||value>2.1f)throw InputFailure{Error::input_value};
        motion.speed=std::min(value,1.f);
        if(!lan::valid(motion))throw InputFailure{Error::input_value};return motion;
    }
    void remote_motion(Token actor,const lan::Motion& motion) override {
        const auto primary=frame.primary;
        if(actor==primary || pointer(frame.world+0x88)!=primary || pointer(frame.registry)!=primary ||
           pointer(frame.registry+0x38)!=actor || !typed(actor,0x2a2b338))throw InputFailure{Error::input_owner};
        const auto destination=pointer(actor+0x58),source=pointer(primary+0x58);
        if(!typed(destination,0x29d2ae0)||!typed(source,0x29d4b30)||pointer(destination+0x300)!=actor||pointer(source+0x300)!=primary)
            throw InputFailure{Error::input_owner};
        const auto speed=graph_variable(actor,2,213),angle=graph_variable(actor,1,209),owner=graph_variable(primary,2,213);
        if(speed.character==owner.character||speed.overrides==owner.overrides)throw InputFailure{Error::input_owner};
        const auto set=reinterpret_cast<WriteGraphFloat>(base+0xff3570);
        set(&speed,0);set(&angle,0);graph_writes+=2;
        for(const auto offset:{0x20u,0x40u}) {
            MEMORY_BASIC_INFORMATION page{};
            if(!readable(destination+offset,16)||!regions.query(destination+offset,page)||
               ((page.Protect&0xff)!=PAGE_READWRITE&&(page.Protect&0xff)!=PAGE_WRITECOPY))throw InputFailure{Error::input_range};
        }
        const Vector zero{};std::memcpy(reinterpret_cast<void*>(destination+0x20),zero.data(),16);
        if(!motion.epoch){++zero_commands;return;} // Internal release, not a wire frame.
        if(!lan::valid(motion))throw InputFailure{Error::input_value};
        LocomotionInput orientation{};std::copy(motion.rotation.begin(),motion.rotation.end(),orientation.rotation_target.begin());
        if(!relative_rotation(orientation.rotation_target,native_vector(0x9eec10,actor),orientation.rotation_delta)||!valid_locomotion_input(orientation))
            throw InputFailure{Error::input_value};
        std::memcpy(reinterpret_cast<void*>(destination+0x20),orientation.rotation_delta.data(),16);
        std::memcpy(reinterpret_cast<void*>(destination+0x40),orientation.rotation_target.data(),16);
        set(&speed,motion.speed);set(&angle,motion.angle);graph_writes+=2;
        const auto observed=reinterpret_cast<ReadGraphFloat>(base+0xff3250)(&speed);
        if(!std::isfinite(observed)||std::abs(observed-motion.speed)>.01f){set(&speed,0);++graph_writes;throw InputFailure{Error::input_value};}
        if(motion.speed>.001f)++graph_nonzero;
    }
    void capture_locomotion(void* controller,void* component) {
        const auto p=frame.primary, source=reinterpret_cast<std::uintptr_t>(controller);
        if(pointer(frame.world+0x88)!=p || pointer(p+0x58)!=source || pointer(p+0x50)!=reinterpret_cast<std::uintptr_t>(component) ||
            !typed(source,0x29d4b30) || pointer(source+0x300)!=p)throw InputFailure{Error::input_owner};
        const LocomotionInput input{read<Vector>(source+0x10),read<Vector>(source+0x20),read<Vector>(source+0x40),read<Vector>(source+0xc0)};
        if(!valid_locomotion_input(input))throw InputFailure{Error::input_value};
        captured_input=input; captured_at=GetTickCount64(); captured_valid=true; ++source_captures;
        if(std::hypot(input.direction[0],input.direction[1],input.direction[2])>.001f)++source_nonzero;
    }
    void apply_locomotion(Token actor,bool enabled,float delta=-1.f) {
        // Derived from retained native PadManipulator 7f1710, corroborated by
        // base reset 7e3510. Only the owned additional base manipulator is written.
        const auto primary=frame.primary;
        if(actor==primary || pointer(frame.world+0x88)!=primary || pointer(frame.registry+0x38)!=actor ||
            !typed(actor,0x2a2b338))throw std::runtime_error("Locomotion actor links changed");
        const auto destination=pointer(actor+0x58), source=pointer(primary+0x58);
        if(destination==source || !typed(destination,0x29d2ae0) || !typed(source,0x29d4b30) ||
            pointer(destination+0x300)!=actor || pointer(source+0x300)!=primary)
            throw InputFailure{Error::input_owner};
        const auto root=pointer(actor+0x1ff8), primary_root=pointer(primary+0x1ff8);
        const auto action=pointer(root+0x80), primary_action=pointer(primary_root+0x80);
        if(!action || action==primary_action || !typed(action,0x2a729d8))throw InputFailure{Error::input_owner};
        MEMORY_BASIC_INFORMATION action_page{};
        if(!readable(action,0x1b8) || !regions.query(action,action_page) ||
            ((action_page.Protect&0xff)!=PAGE_READWRITE && (action_page.Protect&0xff)!=PAGE_WRITECOPY))
            throw InputFailure{Error::input_range};
        using SetMoving=void (*)(void*,unsigned);
        const auto set_moving=reinterpret_cast<SetMoving>(base+0xb2bf40);
        const auto finalize=[&] {
            if(valid_update_delta(delta)) {
                const NativeUpdateTime time{base+0x2948018,delta,0};
                using Finalize=void (*)(void*,const NativeUpdateTime*);
                reinterpret_cast<Finalize>(base+0xb2c190)(reinterpret_cast<void*>(action),&time);
                ++action_finalizes;
            }
        };
        const std::uintptr_t fields[]{0x10,0x20,0x40,0xc0};
        for(const auto offset:fields) {
            MEMORY_BASIC_INFORMATION m{};
            if(!readable(destination+offset,16) || !regions.query(destination+offset,m) ||
                ((m.Protect&0xff)!=PAGE_READWRITE && (m.Protect&0xff)!=PAGE_WRITECOPY))
                throw InputFailure{Error::input_range};
        }
        const Vector zero{};
        // Release prior movement first, including if source validation then fails.
        for(const auto offset:{0x10u,0x20u,0xc0u})
            std::memcpy(reinterpret_cast<void*>(destination+offset),zero.data(),16);
        set_moving(reinterpret_cast<void*>(action),0);
        // Preserve accumulated movement time across active frames. A release
        // outside the input callback cannot reuse a dead stack time object.
        const auto release_time=[&] {
            if(valid_update_delta(delta))finalize();
            else { const float zero_time=0; std::memcpy(reinterpret_cast<void*>(action+0x16c),&zero_time,sizeof(zero_time)); }
        };
        if(!enabled || !captured_valid || GetTickCount64()-captured_at>250) { release_time(); ++zero_commands; return; }
        auto input=captured_input;
        if(!relative_rotation(input.rotation_target,native_vector(0x9eec10,actor),input.rotation_delta)) {
            release_time(); throw InputFailure{Error::input_value};
        }
        if(!valid_locomotion_input(input)) { release_time(); throw InputFailure{Error::input_value}; }
        const Vector* values[]{&input.direction,&input.rotation_delta,&input.rotation_target,&input.direction_copy};
        for(std::size_t i=0;i<std::size(fields);++i)
            std::memcpy(reinterpret_cast<void*>(destination+fields[i]),values[i]->data(),16);
        const bool moving=std::hypot(input.direction[0],input.direction[1],input.direction[2])>.001f;
        set_moving(reinterpret_cast<void*>(action),moving?1u:0u);
        finalize();
        ++locomotion_writes;
    }
};
NativeGame game;
#include "action_native.inc"
#include "native_controller_driver.inc"
void publish() noexcept {
    InterlockedExchange(&status->actor_phase,static_cast<LONG>(lan_mode?remote_actor.phase:demo.phase));
    InterlockedExchange(&status->actor_error,static_cast<LONG>(lan_mode?remote_actor.error:demo.error));
    InterlockedExchange64(&status->actor_creation_calls,static_cast<LONG64>(creation_calls));
    InterlockedExchange64(&status->actor_ticks,static_cast<LONG64>(lan_mode?remote_actor.ticks:demo.ticks));
    InterlockedExchange64(&status->actor_pose_writes,static_cast<LONG64>(lan_mode?remote_actor.pose_writes:demo.pose_writes));
    InterlockedExchange(&status->actor_motion_paused,(lan_mode?remote_actor.paused:demo.motion_paused)?1:0);
    InterlockedExchange64(&status->actor_creation_attempts,static_cast<LONG64>(lan_mode?remote_actor.creation_calls:demo.creation_calls));
    InterlockedExchange64(&status->actor_world_callbacks,static_cast<LONG64>(world_callbacks));
    InterlockedExchange64(&status->actor_callback_total_us,static_cast<LONG64>(callback_total_us));
    InterlockedExchange64(&status->actor_callback_max_us,static_cast<LONG64>(callback_max_us));
    InterlockedExchange64(&status->actor_factory_us,static_cast<LONG64>(factory_us));
    InterlockedExchange64(&status->actor_max_frame_interval_us,static_cast<LONG64>(max_interval_us));
    InterlockedExchange64(&status->actor_locomotion_writes,static_cast<LONG64>(locomotion_writes));
    InterlockedExchange64(&status->actor_zero_commands,static_cast<LONG64>(zero_commands));
    InterlockedExchange64(&status->actor_controller_callbacks,static_cast<LONG64>(controller_callbacks));
    InterlockedExchange64(&status->actor_controller_context_errors,static_cast<LONG64>(controller_context_errors));
    InterlockedExchange64(&status->actor_source_captures,static_cast<LONG64>(source_captures));
    InterlockedExchange64(&status->actor_source_nonzero,static_cast<LONG64>(source_nonzero));
    InterlockedExchange64(&status->actor_action_finalizes,static_cast<LONG64>(action_finalizes));
    InterlockedExchange64(&status->actor_invalid_deltas,static_cast<LONG64>(invalid_deltas));
    InterlockedExchange64(&status->actor_observed_displacement_mm,static_cast<LONG64>(lan_mode?remote_actor.displacement_mm:demo.observed_displacement_mm));
    InterlockedExchange64(&status->actor_graph_writes,static_cast<LONG64>(graph_writes));
    InterlockedExchange64(&status->actor_graph_nonzero,static_cast<LONG64>(graph_nonzero));
    if(lan_mode){
        InterlockedExchange(&status->lan_wait,static_cast<LONG>(remote_actor.waiting));
        InterlockedExchange64(&status->lan_stale_frames,static_cast<LONG64>(remote_actor.stale_frames));
    }
}
void lan_step(void* creator) {
    network->try_read(network_view);
    if(remote_actor.phase==Phase::stopped||remote_actor.phase==Phase::failed){if(actions_mode)guarded_retire_actions();network->request_stop();return;}
    DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if(foreground==GetCurrentProcessId()){
        if(!armed&&(GetAsyncKeyState(VK_F8)&0x8000)){armed=true;armed_at=GetTickCount64();}
        const bool down=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
        if(down&&!pause_key_down)remote_actor.paused=!remote_actor.paused;pause_key_down=down;
        if(GetAsyncKeyState(VK_F10)&0x8000)remote_actor.stop_requested=true;
    }else pause_key_down=false;
    if(!armed){
        if(actions_mode)receive_actions({},false);
        if(remote_actor.stop_requested){remote_actor.phase=Phase::stopped;network->request_stop();}
        return; // No player/graph reads during the title screen or new-game scenes.
    }
    game.creator=creator;Frame frame{};const bool ready=game.sample(frame)&&frame.primary_ready;
    if(ready&&armed){
        if(!local_loaded){local_loaded=true;source_world=frame.world;source_primary=frame.primary;source_registry=frame.registry;}
        else if(source_world!=frame.world||source_primary!=frame.primary||source_registry!=frame.registry){remote_actor.fail(Error::changed_world);network->request_stop();return;}
        if(actions_mode)cache_action_source(frame);
        if(GetTickCount64()-offered_at>=50){
            auto motion=game.local_motion();
            if(actions_mode&&!capture_snapshot(motion.actions))return;
            if(network->try_offer(motion)){
                offered_at=GetTickCount64();
                if(actions_mode)capture_offered(motion.actions.latest);
            }
        }
    }else if(!ready&&local_loaded){remote_actor.phase=Phase::stopped;remote_actor.error=Error::changed_world;network->request_stop();return;}
    const auto incoming=network_view.remote.sample(GetTickCount64());
    remote_actor.tick(game,frame,ready,armed,network_view.status.state,incoming);
    if(actions_mode)receive_actions(frame,ready&&remote_actor.phase==Phase::following&&
        !remote_actor.paused&&incoming&&incoming->fresh&&network_view.status.state==lan::State::connected);
    if(remote_actor.phase==Phase::stopped||remote_actor.phase==Phase::failed)network->request_stop();
}
void step(void* creator) noexcept {
    try {
        if(native_commands_mode){native_command_step(creator);return;}
        if(lan_mode){lan_step(creator);return;}
        if (demo.phase==Phase::stopped || demo.phase==Phase::failed) return;
        DWORD foreground_pid{};
        GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
        if (!armed) {
            if (foreground_pid!=GetCurrentProcessId() || !(GetAsyncKeyState(VK_F8)&0x8000)) return;
            armed=true; armed_at=GetTickCount64();
        }
        if (foreground_pid==GetCurrentProcessId()) {
            const bool down=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
            if (down && !pause_key_down) demo.motion_paused=!demo.motion_paused;
            pause_key_down=down;
            if (GetAsyncKeyState(VK_F10)&0x8000) demo.stop_requested=true;
        } else pause_key_down=false;
        if (!demo.creation_calls && GetTickCount64()-armed_at>10000) { fail_demo(Error::binding); return; }
        game.creator=creator; demo.tick(game);
        if(demo.phase==Phase::stopped || demo.phase==Phase::failed)game.disarm();
    }
    catch (const InputFailure& e) { if(native_commands_mode){native_commands::fault();native_commands::neutralize();publish_native_commands();}if(actions_mode)guarded_retire_actions();fail_demo(e.error); game.disarm(); }
    catch (...) { if(native_commands_mode){native_commands::fault();native_commands::neutralize();publish_native_commands();}if(actions_mode)guarded_retire_actions();fail_demo(Error::binding); game.disarm(); }
}
void guarded_step(void* creator) noexcept {
    // The worker discards pre-spawn/menu history under this same gate. Switching
    // to an active receiver and its fresh baseline is one atomic hand-off.
    if(actions_mode&&!TryAcquireSRWLockExclusive(&action_ingress))return;
    __try { step(creator); }
    __except(EXCEPTION_EXECUTE_HANDLER) { if(native_commands_mode){native_commands::fault();native_commands::neutralize();publish_native_commands();}if(actions_mode)guarded_retire_actions();fail_demo(Error::native_fault); game.disarm(); }
    if(actions_mode)ReleaseSRWLockExclusive(&action_ingress);
}
void controller_step(void* controller,float delta,void* component) noexcept {
    try {
        ++controller_callbacks;
        if(!bound_actor || pointer(bound_actor+0x50)!=reinterpret_cast<std::uintptr_t>(component) ||
            pointer(bound_actor+0x58)!=reinterpret_cast<std::uintptr_t>(controller)) {
            ++controller_context_errors; fail_demo(Error::input_owner); game.disarm(); return;
        }
        if(!valid_update_delta(delta)) { ++invalid_deltas; game.apply_locomotion(bound_actor,false); return; }
        game.apply_locomotion(bound_actor,input_enabled && GetTickCount64()<=input_deadline,delta);
    } catch(const InputFailure& e) { fail_demo(e.error); game.disarm(); }
    catch(...) { fail_demo(Error::binding); game.disarm(); }
}
void guarded_controller_step(void* controller,float delta,void* component) noexcept {
    __try { controller_step(controller,delta,component); }
    __except(EXCEPTION_EXECUTE_HANDLER) { fail_demo(Error::native_fault); game.disarm(); }
}
void controller_update(void* controller,float delta,void* component) {
    original_controller_update(controller,delta,component);
    if(controller!=InterlockedCompareExchangePointer(&bound_controller,nullptr,nullptr) ||
        InterlockedCompareExchange(&entered,1,0)!=0)return;
    const auto start=clock_ticks();
    regions.begin(); guarded_controller_step(controller,delta,component); regions.end();
    const auto duration=microseconds(clock_ticks()-start);
    callback_total_us+=duration; callback_max_us=std::max(callback_max_us,duration);
    publish(); InterlockedExchange(&entered,0);
}
void source_step(void* controller,void* component) noexcept {
    try { game.capture_locomotion(controller,component); }
    catch(const InputFailure& e) { fail_demo(e.error); game.disarm(); }
    catch(...) { fail_demo(Error::binding); game.disarm(); }
}
void guarded_source_step(void* controller,void* component) noexcept {
    __try { source_step(controller,component); }
    __except(EXCEPTION_EXECUTE_HANDLER) { fail_demo(Error::native_fault); game.disarm(); }
}
void source_update(void* controller,float delta,void* component) {
    original_source_update(controller,delta,component);
    if(controller!=InterlockedCompareExchangePointer(&bound_source,nullptr,nullptr) ||
        InterlockedCompareExchange(&entered,1,0)!=0)return;
    const auto start=clock_ticks();
    regions.begin(); guarded_source_step(controller,component); regions.end();
    const auto duration=microseconds(clock_ticks()-start);
    callback_total_us+=duration; callback_max_us=std::max(callback_max_us,duration);
    publish(); InterlockedExchange(&entered,0);
}
void update(void* creator, void* frame) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (caller==base+0xa50a31 && InterlockedCompareExchange(&entered,1,0)==0) {
        const auto start=clock_ticks(); ++world_callbacks;
        if (previous_tick) max_interval_us=std::max(max_interval_us,microseconds(start-previous_tick));
        previous_tick=start;
        const auto thread=GetCurrentThreadId();
        if (!native_thread) native_thread=thread;
        if (native_thread!=thread) fail_demo(Error::binding);
        else { regions.begin(); guarded_step(creator); regions.end(); }
        const auto duration=microseconds(clock_ticks()-start);
        callback_total_us+=duration; callback_max_us=std::max(callback_max_us,duration);
        publish();
        InterlockedExchange(&entered,0);
    }
    original_update(creator,frame);
}
// Digests of 16-byte entry anchors retained from the exact, live target.
// These detect mapping/decryption/competing-hook mismatches, not full-function integrity.
struct Anchor {std::uintptr_t rva; const char* digest;};
constexpr Anchor anchors[]{
    {consumer,"5fde78be3252ecfca1c4143700632282584b08b75a54cfba353d37adb04a60d4"},
    {0xa49460,"991a45851ffef6489fbb8955d680f424c1cf752079c2bc76f2e505a98827526f"},
    {0x9f0870,"56adb9feaddc38abdd4a2e2a2ce735fcf2136f20ccc8c4b58cd335005f5065bb"},
    {0x9eeb40,"8e783b451d7df60c638925306c10b7e42fd10c83f94fd9d0fc5cee184b118802"},
    {0x9eec10,"28962beb35cd012561e0e23e1d7fa1445deba0bb35656de49d2d2cdc8be434b7"},
    {0x9f6320,"40d2bce47fa15ca88b903bf018e28b259446104de897b2e51d0c9f39bbaf4923"},
    {0x7e3de0,"28f844b5972c241e804231b0e41b93e13696e59b98dde74f19029386db5da0c5"},
    {0xb2bf40,"7e7e6d4f07320116567792377469ff2cdfaa9abe230eb61a966eda546daf7523"},
    {0xb2c190,"f068456947a65bdfe685853cf78b386c90f6d32471ff6ecbe28e096fe159f94c"},
    {0x7ef120,"2daffff079e5d1f821e4d889c46ae5086df5270825d6af4543e72822e809af22"}};
constexpr Anchor pad_reference_anchors[]{
    {0x7f8480,"10a9addc69ce31a7ec3ae769be728d2a41253e85cbaf354ba385daf05f379ddb"},
    {0x7ed3a0,"dabb8600e9f55694e6c0d6a7174efa3f3618629b7da38e1db611d70331d0cd12"}};
constexpr Anchor graph_anchors[]{
    {0xffe740,"c560c7def08a438384bc376a0aa7b0024a1e830484f0fcb071a7a9b1dc9046d4"},
    {0xff3250,"cb2bcfec2f35e465f0d46daa920fc7ceb3a15e122335da88fa8963bd1efa297d"},
    {0xff3570,"6e4e5585f3f31ab53c2424583b8f8c7b94ce214341f4a757080f2716a9f7dd9d"}};
bool anchor_matches(const Anchor& a) noexcept {
    if (!readable(base+a.rva,16,true)) return false;
    std::array<unsigned char,32> digest{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(base+a.rva),16,
        digest.data(),static_cast<ULONG>(digest.size()))<0) return false;
    constexpr char hex[]="0123456789abcdef";
    for (std::size_t i=0;i<digest.size();++i)
        if (a.digest[i*2]!=hex[digest[i]>>4] || a.digest[i*2+1]!=hex[digest[i]&15]) return false;
    return true;
}
bool action_anchors_match() noexcept {
    constexpr Anchor bindings[]{
        {0x13f92c0,"ff27b642d6a61268a58266e0fcd6945bbf18553ddf27641e2403b86c77cc3ac8"},
        {0x13fbfc0,"089af5e2538783f6c13673b6ca55fe43e31f9bb12619ada01f47d7dc042c77ef"},
        {0xff1d10,"aa54c6f0f5ceb800c466ca7e686382fe17e0ece4ba3aadf97d868e3fc63b19cd"},
        {0xaebeb0,"0fdd63ed1a29f88a19d3d9c0c335efb328d9520aa8b29a2f642dbda075e25e95"}
    };
    for(const auto& binding:bindings)if(!anchor_matches(binding))return false;
    return true;
}
bool anchors_match() noexcept {
    for (const auto& a:anchors) {
        if((!demo.native_locomotion || graph_locomotion) && (a.rva==0x7e3de0 || a.rva==0xb2bf40 || a.rva==0xb2c190 || a.rva==0x7ef120))continue;
        if(!anchor_matches(a))return false;
    }
    if(demo.native_pad_reference)for(const auto& a:pad_reference_anchors)if(!anchor_matches(a))return false;
    if(graph_locomotion)for(const auto& a:graph_anchors)if(!anchor_matches(a))return false;
    if(actions_mode&&!action_anchors_match())return false;
    return true;
}
DWORD WINAPI worker(void*) noexcept {
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0) { fail_demo(Error::binding); publish(); return 0; }
    base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto start=GetTickCount64();
    while (!anchors_match() || (native_commands_mode&&!native_command_anchors_match())) {
        if (GetTickCount64()-start>=180000) { fail_demo(Error::hook_timeout); publish(); return 0; }
        Sleep(250);
    }
    if(lan_mode){try{
        lan::SessionConfig c;c.role=lan_launch.lan_role==1?lan::Role::host:lan::Role::guest;
        c.ticket=*lan::parse_ticket(lan_launch.lan_ticket);c.participant=lan::random_participant();c.build.purpose=lan::Purpose::native_avatar;
        if(lan_launch.lan_profile)c.build.adapter=lan::independent_adapter;
        std::copy_n(lan_launch.executable_sha256,32,c.build.game.begin());std::copy_n(lan_launch.scenario_sha256,32,c.build.scenario.begin());
        network=new lan::Peer(std::move(c),combat_preview?"127.0.0.1":"0.0.0.0");SecureZeroMemory(lan_launch.lan_ticket,sizeof(lan_launch.lan_ticket));
    }catch(...){fail_demo(Error::binding);publish();return 0;}}
    auto* slot=reinterpret_cast<void* volatile*>(base+creator_vtable+0x48);
    if (!readable(reinterpret_cast<std::uintptr_t>(slot),sizeof(void*)) ||
        *slot!=reinterpret_cast<void*>(base+consumer)) { fail_demo(Error::hook_conflict); publish(); return 0; }
    original_update=reinterpret_cast<Update>(base+consumer);
    // Prevent callback work until page protection and hook ownership are settled.
    InterlockedExchange(&entered,1);
    if(native_commands_mode&&!install_native_command_hooks()) {
        fail_demo(Error::hook_conflict);publish();InterlockedExchange(&entered,0);return 0;
    }
    if(actions_mode&&!install_action_hooks()){
        fail_demo(Error::hook_conflict);publish();InterlockedExchange(&entered,0);return 0;
    }
    if(!exchange_vtable_slot(slot,reinterpret_cast<void*>(original_update),reinterpret_cast<void*>(&update))) {
        if(native_commands_mode)install_native_command_hooks(true);
        if(actions_mode)install_action_hooks(true);
        fail_demo(Error::hook_conflict); publish(); InterlockedExchange(&entered,0); return 0;
    }
    if(demo.native_locomotion && !graph_locomotion) {
        auto* controller_slot=reinterpret_cast<void* volatile*>(base+0x29d2ae0+0x48);
        original_controller_update=reinterpret_cast<ControllerUpdate>(base+0x7e3de0);
        if(!exchange_vtable_slot(controller_slot,reinterpret_cast<void*>(original_controller_update),reinterpret_cast<void*>(&controller_update))) {
            exchange_vtable_slot(slot,reinterpret_cast<void*>(&update),reinterpret_cast<void*>(original_update));
            fail_demo(Error::hook_conflict); publish(); InterlockedExchange(&entered,0); return 0;
        }
        auto* source_slot=reinterpret_cast<void* volatile*>(base+0x29d4b30+0x48);
        original_source_update=reinterpret_cast<ControllerUpdate>(base+0x7ef120);
        if(!exchange_vtable_slot(source_slot,reinterpret_cast<void*>(original_source_update),reinterpret_cast<void*>(&source_update))) {
            exchange_vtable_slot(controller_slot,reinterpret_cast<void*>(&controller_update),reinterpret_cast<void*>(original_controller_update));
            exchange_vtable_slot(slot,reinterpret_cast<void*>(&update),reinterpret_cast<void*>(original_update));
            fail_demo(Error::hook_conflict); publish(); InterlockedExchange(&entered,0); return 0;
        }
    }
    InterlockedOr64(&status->hooks_mask,(demo.native_locomotion && !graph_locomotion)?14:2);
    if(actions_mode)InterlockedOr64(&status->hooks_mask,16);
    if(native_commands_mode)InterlockedOr64(&status->hooks_mask,32);
    InterlockedExchange(&entered,0);
    if(lan_mode){
        // Publish socket readiness even while the title screen has no world
        // callbacks. This lets the notebook discover the room before loading.
        for(;;){const auto view=network->read();const auto& n=view.status;
            if(actions_mode){
                if(n.action_fault!=lan::ActionFault::none)action_fault(n.action_fault);
                AcquireSRWLockExclusive(&action_ingress);
                if(!action_consumer_active){
                    const auto idle=network->read();
                    if(idle.actions.latest())network->try_ack_actions(idle.actions.latest()->packet_sequence);
                }
                ReleaseSRWLockExclusive(&action_ingress);
            }
            InterlockedExchange(&status->lan_state,static_cast<LONG>(n.state));
            InterlockedExchange(&status->lan_error,static_cast<LONG>(n.failure));
            InterlockedExchange(&status->lan_system_error,static_cast<LONG>(n.system_error));
            InterlockedExchange64(&status->lan_frames_sent,static_cast<LONG64>(n.frames_sent));
            InterlockedExchange64(&status->lan_frames_received,static_cast<LONG64>(n.frames_received));
            InterlockedExchange64(&status->lan_rejected,static_cast<LONG64>(n.rejected));
            if(n.state==lan::State::failed||n.state==lan::State::lost||n.state==lan::State::stopped)break;
            Sleep(50);
        }
    }
    // The callback and immutable original remain pinned until process exit.
    // Stopping the bounded demo disables all actor reads/writes, without freeing actors.
    return 0;
}
} // namespace
bool start_native_demo(launch::SharedStatus* shared,launch::ActorTest mode,const launch::Payload* config) noexcept {
    if (status || !shared) return false;
    lan_mode=mode==launch::ActorTest::lan;
    native_commands_mode=mode==launch::ActorTest::native_commands;
    if(native_commands_mode&&!config)return false;
    if(native_commands_mode)native_command_configure(*config);
    if(lan_mode){if(!config||!valid_lan_payload(*config))return false;lan_launch=*config;}
    status=shared;
    actions_mode=lan_mode&&lan_launch.lan_profile!=0;
    combat_preview=lan_mode&&lan_launch.lan_profile==2;
    graph_locomotion=mode==launch::ActorTest::graph_locomotion||lan_mode;
    demo.native_locomotion=mode==launch::ActorTest::locomotion || graph_locomotion;
    demo.native_pad_reference=mode==launch::ActorTest::pad_reference||native_commands_mode;
    InterlockedExchange(&status->actor_phase,static_cast<LONG>(Phase::waiting));
    HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);
    if (!thread) { status=nullptr; return false; }
    CloseHandle(thread); return true;
}
} // namespace skcoop::actors
