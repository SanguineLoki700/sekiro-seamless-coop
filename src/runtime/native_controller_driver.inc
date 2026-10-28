// Included in actor_native.cpp's private namespace. Experimental, exact-build
// input adapter. See docs/NATIVE_CONTROLLER.md for evidence and open obligations.
// This path neither calls the legacy event driver nor writes graph variables.
#include "native_controller_bindings.inc"
namespace native_commands {
constexpr ButtonMask supported=button_mask(CommandButton::attack)|button_mask(CommandButton::guard);
struct Binding {Token world{},primary{},registry{},actor{},controller{},component{},behavior{},requests{},root{};
    std::uint32_t actor_handle{},primary_handle{};};
struct PadScope {
    bool guest{},usable{};
    int logical{-1},raw_code{-1};
    PlayerInput supplied,captured;
    NativeStickAxes axes;
    std::array<float,2> source_axes{};
    unsigned source_axes_seen{};
    std::optional<NativeInputSource> identity;
};
thread_local PadScope* pad_scope{};
struct ParameterScope {Token actor{},character{},overrides{};bool gate_seen{},speed_seen{},angle_seen{};float speed{},angle{};};
thread_local ParameterScope* parameter_scope{};
std::atomic<Token> owned_controller{},owned_actor{},primary_controller{};
std::atomic<DWORD> owner_thread{};
std::atomic<bool> aborted{};
std::atomic<bool> retired{};
Binding guest,source;
PlayerInput primary_input;
PlayerCommandCapture primary_capture{1,supported};
std::uint64_t primary_at{},guest_at{},sample_sequence{},consumed_at{},consumed_sequence{};
PlayerInput resolved_input;
bool consumer_valid{},held_observed{},resolver_completed{},provider_disabled{true},negative_passed{};
unsigned native_held_seen{};
ButtonMask native_held{};
std::optional<NativeInputSource> produced;
std::optional<ControllerScenario> scenario;
PlayerCommandReceiver receiver{supported};
NativeInputLatch latch{1};
CommandScope command_scope;
std::uint64_t generation{1},load_at{},neutral_at{},last_world_at{},negative_at{};
std::uint64_t native_queries{},native_resolvers{},native_held_queries{},native_pressed_queries{};
int state_code{};
Token state_character{},state_values{},state_entries{},state_storage{};
int state_slot{-1};
// No action-result enum is inferred from a button level or provider writeback.
constexpr bool action_output_bound=false;

using RawButton=std::uint8_t (*)(void*,int);
using LogicalMap=int (*)(int);
using Axis=float (*)(void*);
using Flag=std::uint8_t (*)(void*);
using Parameters=void (*)(void*);
using IsAdditional=std::uint8_t (*)(void*);
using Held=float (*)(void*,int);
using Pressed=std::uint8_t (*)(void*,int,int);
ControllerUpdate real_pad{};
RawButton real_button{};
LogicalMap real_map{};
std::array<Axis,6> real_axes{};
std::array<Flag,5> real_flags{};
Parameters real_parameters{};
IsAdditional real_additional{};
Held real_held{};
Pressed real_pressed{};
WriteGraphFloat real_graph_set{};

void fault() noexcept {aborted.store(true,std::memory_order_release);}
void retire() noexcept {
    fault();retired.store(true,std::memory_order_release);
    owned_controller.store(0,std::memory_order_release);owned_actor.store(0,std::memory_order_release);
    primary_controller.store(0,std::memory_order_release);
}
bool on_thread() noexcept {return owner_thread.load(std::memory_order_acquire)==GetCurrentThreadId();}
bool relationships(const Binding& b,bool graph_ready=false) {
    if(!b.actor||!b.world||!b.registry||pointer(base+0x3d7a1e0)!=b.world||
       pointer(b.world+0x88)!=b.primary||pointer(b.world+0x48)!=b.registry||
       pointer(b.registry)!=b.primary||read<int>(b.world+0x40)!=6||
       (b.actor!=b.primary&&pointer(b.registry+0x38)!=b.actor)||
       !typed(b.actor,0x2a2b338)||pointer(b.actor+0x58)!=b.controller||
       !typed(b.controller,0x29d4b30)||pointer(b.controller+0x300)!=b.actor||
       pointer(b.actor+0x50)!=b.component||!typed(b.component,0x29d5508)||
       pointer(b.component+0x10)!=b.actor||pointer(b.component+0x120)!=b.controller||
       pointer(b.component+0x358)||read<std::uint32_t>(b.actor+0x78)!=b.actor_handle||
       read<std::uint32_t>(b.primary+0x78)!=b.primary_handle)return false;
    const auto action=pointer(b.component+0x50),root=pointer(b.actor+0x1ff8);
    if(!typed(action,0x29d1148)||pointer(action+8)!=b.component||!root||root!=b.root||
       pointer(root+0x28)!=b.behavior||!typed(b.behavior,0x2a790a8)||pointer(b.behavior+8)!=b.actor||
       pointer(root+0x80)!=b.requests||!typed(b.requests,0x2a729d8))return false;
    return !graph_ready||(model(b.actor)&&pointer(b.behavior+0x10)&&
        pointer(pointer(b.behavior+0x10)+0x28));
}
Binding bind_actor(const Frame& f,Token actor) {
    Binding b{f.world,f.primary,f.registry,actor,pointer(actor+0x58),pointer(actor+0x50)};
    const auto root=pointer(actor+0x1ff8);b.root=root;b.behavior=pointer(root+0x28);b.requests=pointer(root+0x80);
    b.actor_handle=read<std::uint32_t>(actor+0x78);b.primary_handle=read<std::uint32_t>(f.primary+0x78);
    if(!relationships(b))throw InputFailure{Error::input_owner};
    return b;
}
bool verify(const Binding& b,bool ready=false) noexcept {
    try {return relationships(b,ready);}catch(...){return false;}
}
bool guarded_verify(const Binding& b,bool ready=false) noexcept {
    bool result=false;
    __try {result=verify(b,ready);}__except(EXCEPTION_EXECUTE_HANDLER){result=false;}
    return result;
}
void neutralize() noexcept {latch.suspend();produced.reset();consumer_valid=false;}

// Read native results with their actual metadata type. The observed Stand state
// is diagnostic; an unknown state number is never classified as attack/guard.
int read_state() {
    const auto graph=pointer(guest.behavior+0x10),character=pointer(graph+0x28);
    const auto values=pointer(character+0x98),map=pointer(values+0x100)+0x28;
    const auto slots=pointer(map),storage=pointer(pointer(values+0x118)+0x18);
    if(character!=state_character||values!=state_values||slots!=state_entries||storage!=state_storage){
        const auto mask=read<int>(map+12);
        if(mask<1||mask>8191||((mask+1)&mask)!=0||!slots)throw InputFailure{Error::input_range};
        int slot=-1;
        for(int i=0;i<=mask;++i){const auto item=slots+i*16;
            if(read<std::int64_t>(item)==384){if(slot!=-1)throw InputFailure{Error::input_value};slot=read<int>(item+8);}}
        const auto metadata=pointer(pointer(values+0xd0)+0x28);
        if(slot<0||slot>4095||read<std::int16_t>(metadata+384*6+4)!=3)throw InputFailure{Error::input_value};
        state_character=character;state_values=values;state_entries=slots;state_storage=storage;state_slot=slot;
    }
    return read<int>(storage+state_slot*4);
}
void read_outputs(NativePlayerState& result) {
    if(!relationships(guest,true))return;
    const auto position=game.position(guest.actor);
    if(!valid_position(position))throw InputFailure{Error::input_value};
    result.valid=result.ready=true;result.position={position[0],position[1],position[2]};
    if(consumer_valid){result.consumed_input_generation=generation;result.consumed_frame_sequence=consumed_sequence;}
    state_code=read_state();
}
class Adapter final:public NativeControllerAdapter {
public:
    bool capture_primary(PlayerInput& result)noexcept override {
        const auto now=GetTickCount64();
        if(!on_thread()||!primary_at||now<primary_at||now-primary_at>100)return false;
        result=primary_input;return true;
    }
    bool apply_guest(const CommandDelivery& delivery)noexcept override {
        if(!on_thread()||aborted.load())return false;
        if(provider_disabled)return true; // Deliberate provider-off negative control.
        return latch.offer(delivery,GetTickCount64())==NativeInputOffer::accepted;
    }
    void neutralize_guest()noexcept override {if(on_thread())neutralize();else fault();}
    bool observe_guest(NativePlayerState& result)noexcept override {
        result={};if(!on_thread())return false;
        try {read_outputs(result);return result.valid;}catch(...){fault();return false;}
    }
} adapter;
bool guarded_observe(NativePlayerState& result)noexcept {
    bool okay=false;
    __try {okay=adapter.observe_guest(result);}__except(EXCEPTION_EXECUTE_HANDLER){fault();}
    return okay;
}

// Associate consumer evidence only after the original parameter resolver has
// run AND the native behavior queried held levels on this actor's own module.
// Latch metadata or a read-back of supplied fields alone cannot satisfy this.
void finish_consumer() noexcept {
    if(!provider_disabled&&produced&&resolver_completed&&native_held_seen==3&&
       GetTickCount64()-guest_at<=100){
        resolved_input.held=native_held;
        consumed_sequence=produced->frame_sequence;consumed_at=GetTickCount64();
        consumer_valid=true;held_observed=true;++sample_sequence;
    }
}
int map_button(int logical) {
    const auto result=real_map(logical);
    if(pad_scope){pad_scope->logical=logical;pad_scope->raw_code=result;}
    return result;
}
ButtonMask logical_bit(int logical) noexcept {
    return logical==0?button_mask(CommandButton::attack):(logical==2?button_mask(CommandButton::guard):0);
}
std::uint8_t raw_button(void* context,int code) {
    auto* scope=pad_scope;
    if(!scope||retired.load())return real_button(context,code);
    const bool mapped=code==scope->raw_code;const auto bit=mapped?logical_bit(scope->logical):0;
    if(scope->guest){++native_queries;return scope->usable&&bit&&(scope->supplied.held&bit)?1u:0u;}
    const auto result=real_button(context,code);
    if(mapped&&bit&&result)scope->captured.held|=bit;
    return result;
}
template<std::size_t Index> float axis(void* context) {
    auto* scope=pad_scope;
    if(!scope||retired.load())return real_axes[Index](context);
    if(scope->guest){++native_queries;if(!scope->usable)return 0;
        if constexpr(Index<2)return scope->axes.raw[Index];
        if constexpr(Index<4)return scope->axes.processed[Index-2];
        return 0;
    }
    const auto result=real_axes[Index](context);
    if constexpr(Index<2){scope->source_axes[Index]=result;scope->source_axes_seen|=1u<<Index;}
    return result;
}
template<std::size_t Index> std::uint8_t flag(void* context) {
    return pad_scope&&pad_scope->guest&&!retired.load()?0:real_flags[Index](context);
}
void pad_before(PadScope& scope,void* controller,void* component,float delta) noexcept {
    if(!on_thread()||!valid_update_delta(delta)){fault();return;}
    try {
        const auto& b=scope.guest?guest:source;
        if(reinterpret_cast<Token>(controller)!=b.controller||reinterpret_cast<Token>(component)!=b.component||!relationships(b)){
            fault();return;
        }
        scope.usable=!aborted.load();
        if(scope.guest){
            scope.supplied=scope.usable&&!provider_disabled?latch.begin_update(GetTickCount64()):PlayerInput{};
            const auto projected=project_native_stick(scope.supplied.movement,read<float>(b.controller+0x94));
            if(!projected){scope.usable=false;fault();return;}scope.axes=*projected;
            scope.identity=scope.usable&&!provider_disabled?latch.source_identity():std::nullopt;
            produced.reset();consumer_valid=false;held_observed=false;resolver_completed=false;
            native_held_seen=0;native_held=0;guest_at=GetTickCount64();
        }
    }catch(...){scope.usable=false;fault();}
}
void guarded_pad_before(PadScope& scope,void* controller,void* component,float delta) noexcept {
    __try {pad_before(scope,controller,component,delta);}__except(EXCEPTION_EXECUTE_HANDLER){scope.usable=false;fault();}
}
void pad_after(const PadScope& scope) noexcept {
    try {
        if(!scope.usable)return;
        if(scope.guest){produced=scope.identity;return;}
        if(scope.source_axes_seen!=3){fault();return;}
        auto movement=movement_from_native_stick(scope.source_axes,read<float>(source.controller+0x94));
        if(!movement){fault();return;}
        primary_input={*movement,scope.captured.held};primary_at=GetTickCount64();++source_captures;
        // Retain edges at native update frequency, before any future network
        // offer. This local trial never sends primary input to the guest.
        if(!primary_capture.capture(primary_input,primary_at))fault();
    }catch(...){fault();}
}
void guarded_pad_after(const PadScope& scope) noexcept {
    __try {pad_after(scope);}__except(EXCEPTION_EXECUTE_HANDLER){fault();}
}
void pad_update(void* controller,float delta,void* component) {
    const auto address=reinterpret_cast<Token>(controller);
    const bool is_guest=address==owned_controller.load(std::memory_order_acquire)&&address;
    const bool is_source=address==primary_controller.load(std::memory_order_acquire)&&address&&on_thread();
    if(!is_guest&&!is_source){real_pad(controller,delta,component);return;}
    if(is_guest&&!on_thread()){retire();real_pad(controller,delta,component);return;}
    regions.begin();const bool valid_binding=!retired.load()&&guarded_verify(is_guest?guest:source);regions.end();
    if(!valid_binding){retire();real_pad(controller,delta,component);return;}
    if(pad_scope)fault();
    PadScope scope;scope.guest=is_guest;
    regions.begin();guarded_pad_before(scope,controller,component,delta);regions.end();
    invoke_native_scoped(pad_scope,scope,real_pad,controller,delta,component);
    if(on_thread()){regions.begin();guarded_pad_after(scope);regions.end();}
}

void parameters_after(const ParameterScope& scope) noexcept {
    try {
        if(!relationships(guest,true))return;
        const auto direction=read<Vector>(guest.controller+0x10);
        if(!valid_position(direction))throw InputFailure{Error::input_value};
        const auto intensity=std::hypot(direction[0],direction[2]);
        if(intensity>1.001f)throw InputFailure{Error::input_value};
        resolved_input.movement={};
        if(intensity>.00001f)resolved_input.movement={{direction[0]/intensity,direction[2]/intensity},std::min(intensity,1.f)};
        // Returning from B49D30, or rereading controller+10, does not prove the
        // formerly skipped branch ran. Require its scoped predicate and actual
        // original engine writes into this guest's own behavior parameters.
        resolver_completed=scope.gate_seen&&scope.speed_seen&&scope.angle_seen;
        if(resolver_completed)++native_resolvers;finish_consumer();
    }catch(...){fault();}
}
void guarded_parameters_after(const ParameterScope& scope) noexcept {
    __try {parameters_after(scope);}__except(EXCEPTION_EXECUTE_HANDLER){fault();}
}
bool prepare_parameters(ParameterScope& scope) noexcept {
    try {
        if(!relationships(guest,true))return false;
        const auto graph=pointer(guest.behavior+0x10);
        scope.actor=guest.actor;scope.character=pointer(graph+0x28);scope.overrides=graph+0x98;
        return true;
    }catch(...){return false;}
}
bool guarded_prepare_parameters(ParameterScope& scope) noexcept {
    bool okay=false;
    __try {okay=prepare_parameters(scope);}__except(EXCEPTION_EXECUTE_HANDLER){okay=false;}
    return okay;
}
void parameters(void* behavior) {
    const bool selected=!retired.load()&&on_thread()&&reinterpret_cast<Token>(behavior)==guest.behavior&&guest.actor;
    if(!selected){real_parameters(behavior);return;}
    ParameterScope scope;
    regions.begin();const bool okay=guarded_prepare_parameters(scope);regions.end();
    if(!okay){retire();real_parameters(behavior);return;}
    // Only this original resolver call may treat our fully constructed PlayerIns
    // as locally controlled. Never change WorldChrMan's primary pointer.
    invoke_native_scoped(parameter_scope,scope,real_parameters,behavior);
    regions.begin();guarded_parameters_after(scope);regions.end();
}
std::uint8_t is_additional(void* actor) {
    if(parameter_scope&&!retired.load()&&reinterpret_cast<Token>(actor)==parameter_scope->actor){parameter_scope->gate_seen=true;return 0;}
    return real_additional(actor);
}
void observe_graph_set(const GraphVariable* variable,float value) {
    real_graph_set(variable,value);
    // These are native calls observed after the original setter, never calls
    // issued by this adapter, and never reinterpreted as player commands.
    if(!parameter_scope||retired.load())return;
    if(variable->character!=parameter_scope->character||variable->overrides!=parameter_scope->overrides)return;
    if(!std::isfinite(value)){fault();return;}
    if(variable->index==213){parameter_scope->speed_seen=true;parameter_scope->speed=value;}
    if(variable->index==209){parameter_scope->angle_seen=true;parameter_scope->angle=value;}
}
float held(void* module,int index) {
    const auto result=real_held(module,index);
    if(!retired.load()&&on_thread()&&reinterpret_cast<Token>(module)==guest.requests&&guest.actor&&(index==0||index==2)){
        ++native_held_queries;
        if(!std::isfinite(result)||result<0){fault();return result;}
        const auto seen=index==0?1u:2u;native_held_seen|=seen;
        const auto bit=logical_bit(index);if(result>0)native_held|=bit;else native_held&=~bit;
        finish_consumer();
    }
    return result;
}
std::uint8_t pressed(void* module,int kind,int index) {
    const auto result=real_pressed(module,kind,index);
    if(!retired.load()&&on_thread()&&reinterpret_cast<Token>(module)==guest.requests&&guest.actor)++native_pressed_queries;
    return result;
}
} // namespace native_commands

void native_command_configure(const launch::Payload& config) noexcept {
    using namespace native_commands;
    std::copy_n(config.nonce,16,command_scope.session.begin());
    for(unsigned i=0;i<16;++i)command_scope.connection[i]=static_cast<std::uint8_t>(config.nonce[i]^0xa5);
    command_scope.player=2;command_scope.input_generation=generation;
}
void adopt_native_command_actor(Token actor) {
    using namespace native_commands;
    if(owned_actor.load()||actor==game.frame.primary)throw InputFailure{Error::input_owner};
    owner_thread.store(GetCurrentThreadId(),std::memory_order_release);
    source=bind_actor(game.frame,game.frame.primary);guest=bind_actor(game.frame,actor);
    if(source.controller==guest.controller||source.requests==guest.requests||source.behavior==guest.behavior)
        throw InputFailure{Error::input_owner};
    primary_controller.store(source.controller,std::memory_order_release);
    owned_actor.store(actor,std::memory_order_release);
    owned_controller.store(guest.controller,std::memory_order_release);
}
void publish_native_commands() noexcept {
    using namespace native_commands;
    if(aborted.load()){
        InterlockedExchange(&status->native_controller_phase,static_cast<LONG>(ControllerScenarioPhase::failed));
        InterlockedExchange(&status->native_controller_fault,static_cast<LONG>(ControllerScenarioFault::command_failure));return;
    }
    if(!scenario)return;
    const auto& report=scenario->report();
    InterlockedExchange(&status->native_controller_phase,static_cast<LONG>(report.phase));
    InterlockedExchange(&status->native_controller_fault,static_cast<LONG>(report.fault));
    InterlockedExchange(&status->native_controller_state_code,state_code);
    InterlockedExchange(&status->native_controller_output_bound,action_output_bound?1:0);
    InterlockedExchange64(&status->native_controller_commands,static_cast<LONG64>(report.commands));
    InterlockedExchange64(&status->native_controller_consumed,static_cast<LONG64>(report.consumed_commands));
    InterlockedExchange64(&status->native_controller_guard_ms,static_cast<LONG64>(report.guard_held_ms));
    InterlockedExchange64(&status->native_controller_displacement_mm,static_cast<LONG64>(std::llround(report.displacement*1000.)));
    InterlockedExchange(&status->native_controller_negative_passed,negative_passed?1:0);
    InterlockedExchange64(&status->native_controller_input_queries,static_cast<LONG64>(native_queries));
    InterlockedExchange64(&status->native_controller_resolvers,static_cast<LONG64>(native_resolvers));
    InterlockedExchange64(&status->native_controller_held_queries,static_cast<LONG64>(native_held_queries));
    InterlockedExchange64(&status->native_controller_pressed_queries,static_cast<LONG64>(native_pressed_queries));
}
void trace_native_command_state(std::uint64_t now) noexcept {
    using namespace native_commands;
    if(!scenario)return;
    const auto phase=static_cast<LONG>(scenario->report().phase);
    const auto count=InterlockedCompareExchange(&status->native_controller_trace_count,0,0);
    if(count>0){const auto& last=status->native_controller_trace[count-1];
        if(last.phase==phase&&last.state_code==state_code&&last.input_generation==static_cast<LONG64>(generation))return;}
    if(count<0||static_cast<std::size_t>(count)>=launch::native_trace_capacity){
        InterlockedExchange(&status->native_controller_trace_overflow,1);fault();neutralize();return;
    }
    auto& item=status->native_controller_trace[count];
    item.phase=phase;item.state_code=state_code;item.input_generation=static_cast<LONG64>(generation);
    item.frame_sequence=consumer_valid?static_cast<LONG64>(consumed_sequence):0;
    item.elapsed_ms=static_cast<LONG64>(now-load_at);
    // Slots are immutable after this publication barrier. The host samples
    // the count first; short state transitions survive its slower UI polling.
    InterlockedExchange(&status->native_controller_trace_count,count+1);
}
void native_command_step(void* creator) {
    using namespace native_commands;
    const auto now=GetTickCount64();
    if(owned_actor.load()&&last_world_at&&now-last_world_at>250){fault();neutralize();}
    last_world_at=now;
    if(aborted.load()){neutralize();fail_demo(Error::input_owner);publish_native_commands();return;}
    DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    const bool focused=foreground==GetCurrentProcessId();
    if(owned_actor.load()&&!focused){fault();neutralize();fail_demo(Error::input_owner);return;}
    if(demo.phase==Phase::failed||demo.phase==Phase::stopped){neutralize();return;}
    if(scenario&&terminal(scenario->report().phase)&&negative_passed){neutralize();publish_native_commands();return;}
    game.creator=creator;
    Frame frame{};const bool loaded=game.sample(frame)&&frame.primary_ready;
    if(!loaded){if(owned_actor.load()){fault();neutralize();fail_demo(Error::changed_world);}else{load_at=0;neutral_at=0;}return;}
    if(!focused)return;
    if(!load_at)load_at=now;
    // Allow the Continue transition to finish before automatic native creation.
    if(!owned_actor.load()&&now-load_at<2000)return;
    demo.tick(game);
    if(demo.phase!=Phase::following)return;
    PlayerInput primary;
    if(!adapter.capture_primary(primary)){if(now-load_at>5000)fault();return;}
    if(!scenario&&primary!=PlayerInput{}){neutral_at=0;return;}
    if(!neutral_at)neutral_at=now;
    if(!scenario&&now-neutral_at<500)return;
    if(!scenario){negative_at=now;scenario.emplace(generation,now,ControllerScenarioMode::diagnostic_unbound);}
    ControllerScenarioObservation observation;
    if(!guarded_observe(observation.guest)){fault();neutralize();return;}
    observation.ownership_verified=true;observation.action_output_bound=action_output_bound;
    observation.sample_sequence=world_callbacks;observation.observed_ms=now;observation.lifetime=1;
    const auto position=game.position(source.actor);
    observation.primary_position={position[0],position[1],position[2]};observation.primary_input=primary;
    observation.consumer_observed=consumer_valid&&consumed_at<=now&&now-consumed_at<=100;
    observation.held_state_observed=held_observed;observation.resolved_input=resolved_input;
    observation.pose_setter_calls=demo.pose_writes;observation.direct_graph_writes=graph_writes;
    observation.other_driver_commands=locomotion_writes; // Legacy driver must stay off.
    trace_native_command_state(now);
    const auto output=scenario->tick(now,observation);
    if(output.neutralize)adapter.neutralize_guest();
    if(output.command){
        if(receiver.observe(command_scope,*output.command,now)!=CommandReceive::accepted){fault();neutralize();return;}
        const auto delivery=receiver.take(now);
        if(!delivery||!adapter.apply_guest(*delivery)){fault();neutralize();return;}
    }
    if(provider_disabled&&terminal(scenario->report().phase)){
        if(scenario->report().fault!=ControllerScenarioFault::consumer_timeout){fault();neutralize();publish_native_commands();return;}
        negative_passed=true;provider_disabled=false;++generation;command_scope.input_generation=generation;
        latch=NativeInputLatch{generation};receiver=PlayerCommandReceiver{supported};consumer_valid=false;
        scenario.emplace(generation,now,ControllerScenarioMode::diagnostic_unbound);
    }
    publish_native_commands();
}

bool native_command_anchors_match() noexcept {
    for(const auto& entry:native_commands::entries){
        if(!readable(base+entry.rva,16,true))return false;
        std::array<unsigned char,32> digest{};
        if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(base+entry.rva),16,
            digest.data(),static_cast<ULONG>(digest.size()))<0)return false;
        constexpr char hex[]="0123456789abcdef";
        for(std::size_t i=0;i<digest.size();++i)
            if(entry.digest[i*2]!=hex[digest[i]>>4]||entry.digest[i*2+1]!=hex[digest[i]&15])return false;
    }
    return true;
}
bool install_native_command_hooks(bool detach=false) noexcept {
    using namespace native_commands;
    if(!detach&&!native_command_anchors_match())return false;
    std::array<HANDLE,1024> threads{};std::size_t count{};
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot==INVALID_HANDLE_VALUE)return false;
    THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
    bool okay=Thread32First(snapshot,&entry)!=FALSE;
    while(okay){
        if(entry.th32OwnerProcessID==GetCurrentProcessId()&&entry.th32ThreadID!=GetCurrentThreadId()){
            const auto thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
            if(!thread){okay=false;break;}
            if(count==threads.size()){CloseHandle(thread);okay=false;break;}threads[count++]=thread;
        }
        if(!Thread32Next(snapshot,&entry)){okay=GetLastError()==ERROR_NO_MORE_FILES;break;}
    }
    CloseHandle(snapshot);
    if(!detach){
        real_pad=reinterpret_cast<ControllerUpdate>(base+0x7ef120);
        real_map=reinterpret_cast<LogicalMap>(base+0xb258d0);real_button=reinterpret_cast<RawButton>(base+0x95fd80);
        constexpr Token axes[]{0x9603d0,0x9603e0,0x960390,0x9603a0,0x9603b0,0x9603c0};
        constexpr Token flags[]{0x95fe50,0x95fe60,0x960360,0x95ffc0,0x95fe40};
        for(std::size_t i=0;i<real_axes.size();++i)real_axes[i]=reinterpret_cast<Axis>(base+axes[i]);
        for(std::size_t i=0;i<real_flags.size();++i)real_flags[i]=reinterpret_cast<Flag>(base+flags[i]);
        real_parameters=reinterpret_cast<Parameters>(base+0xb49d30);real_additional=reinterpret_cast<IsAdditional>(base+0x9f36c0);
        real_held=reinterpret_cast<Held>(base+0xb2b380);real_pressed=reinterpret_cast<Pressed>(base+0xb2b6d0);
        real_graph_set=reinterpret_cast<WriteGraphFloat>(base+0xff3570);
    }
    bool begun=false;LONG result=ERROR_INVALID_FUNCTION;
    if(okay){result=DetourTransactionBegin();begun=result==NO_ERROR;}
    if(begun){
        result=DetourUpdateThread(GetCurrentThread());
        for(std::size_t i=0;i<count;++i)if(result==NO_ERROR)result=DetourUpdateThread(threads[i]);
        const auto change=[&](auto& target,auto hook){
            if(result==NO_ERROR)result=detach?DetourDetach(reinterpret_cast<PVOID*>(&target),reinterpret_cast<PVOID>(hook)):
                DetourAttach(reinterpret_cast<PVOID*>(&target),reinterpret_cast<PVOID>(hook));
        };
        change(real_pad,&pad_update);change(real_map,&map_button);change(real_button,&raw_button);
        change(real_axes[0],&axis<0>);change(real_axes[1],&axis<1>);change(real_axes[2],&axis<2>);
        change(real_axes[3],&axis<3>);change(real_axes[4],&axis<4>);change(real_axes[5],&axis<5>);
        change(real_flags[0],&flag<0>);change(real_flags[1],&flag<1>);change(real_flags[2],&flag<2>);
        change(real_flags[3],&flag<3>);change(real_flags[4],&flag<4>);
        change(real_parameters,&parameters);change(real_additional,&is_additional);
        change(real_held,&held);change(real_pressed,&pressed);
        change(real_graph_set,&observe_graph_set);
        if(result==NO_ERROR)result=DetourTransactionCommit();else DetourTransactionAbort();
    }
    for(std::size_t i=0;i<count;++i)CloseHandle(threads[i]);
    return begun&&result==NO_ERROR;
}
