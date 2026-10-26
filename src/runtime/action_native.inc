// Included inside actor_native.cpp's private namespace. All addresses below
// belong to the exact supported image and retained loaded-code observations.
// No action path writes to the original player or calls a global-player wrapper.
SRWLOCK action_source_lock=SRWLOCK_INIT;
lan::ActionCapture action_capture{1};
ActionReceiver action_receiver;
volatile LONG action_source_enabled{};
void* volatile action_source_character{};
struct ActionSource {
    Frame frame;
    Token root{},behavior{},graph{},character{};
    GraphVariable angle,jump_angle;
} action_source;
using FireEvent=int (*)(void*);
using LuaContext=void* (*)(void*);
using EventLookup=int* (*)(void*,const char**,int*);
FireEvent original_fire_event{};
LuaContext original_lua_context{};
EventLookup original_event_lookup{};
struct SourceObservation {void* lua{};const char* name{};void* context{};Token map{};int event_id{-1};};
thread_local SourceObservation* source_observation{};
bool action_anchors_match() noexcept;

void action_fault(lan::ActionFault fault) noexcept {
    InterlockedCompareExchange(&status->action_error,static_cast<LONG>(fault),0);
    InterlockedExchange(&action_source_enabled,0);
    if(network)network->request_stop();
}
bool lock_capture() noexcept {
    if(TryAcquireSRWLockExclusive(&action_source_lock))return true;
    // Dropping an unrecorded pulse would be silent data loss. End this bounded
    // test visibly instead; the original Lua callback has already completed.
    action_fault(lan::ActionFault::overflow);return false;
}
void cache_source_body(const Frame& frame) {
    if(InterlockedCompareExchange(&status->action_error,0,0))return;
    if(InterlockedCompareExchange(&action_source_enabled,0,0)){
        const auto& source=action_source;
        if(frame.world!=source.frame.world||frame.primary!=source.frame.primary||frame.registry!=source.frame.registry||
            pointer(frame.primary+0x1ff8)!=source.root||pointer(source.root+0x28)!=source.behavior||
            pointer(source.behavior+0x10)!=source.graph||pointer(source.graph+0x28)!=source.character)
            action_fault(lan::ActionFault::scope_changed);
        return;
    }
    const auto angle=game.graph_variable(frame.primary,1,209);
    const auto jump=game.graph_variable(frame.primary,29,461);
    const auto root=pointer(frame.primary+0x1ff8),behavior=pointer(root+0x28),graph=pointer(behavior+0x10);
    if(angle.character!=jump.character||angle.overrides!=jump.overrides)throw InputFailure{Error::input_owner};
    action_source={frame,root,behavior,graph,angle.character,angle,jump};
    InterlockedExchangePointer(&action_source_character,reinterpret_cast<void*>(angle.character));
    InterlockedExchange(&action_source_enabled,1);
}
void cache_source_safe(const Frame& frame) noexcept {
    try {cache_source_body(frame);}catch(...){action_fault(lan::ActionFault::invalid);}
}
void guarded_cache_source(const Frame& frame) noexcept {
    __try {cache_source_safe(frame);}
    __except(EXCEPTION_EXECUTE_HANDLER){action_fault(lan::ActionFault::invalid);}
}
void cache_action_source(const Frame& frame) noexcept {
    if(!lock_capture())return;
    guarded_cache_source(frame);
    ReleaseSRWLockExclusive(&action_source_lock);
}
std::uint32_t action_source_ms() noexcept {
    const auto now=GetTickCount64();
    if(!armed_at||now<armed_at||now-armed_at>UINT32_MAX){action_fault(lan::ActionFault::clock_regressed);return 0;}
    return static_cast<std::uint32_t>(now-armed_at);
}
bool capture_snapshot(lan::ActionSnapshot& result) noexcept {
    if(!lock_capture())return false;
    const auto snapshot=action_capture.snapshot(action_source_ms());
    const auto fault=action_capture.fault();
    if(snapshot)result=*snapshot;
    ReleaseSRWLockExclusive(&action_source_lock);
    if(fault!=lan::ActionFault::none)action_fault(fault);
    return snapshot.has_value()&&!InterlockedCompareExchange(&status->action_error,0,0);
}
void capture_offered(std::uint64_t serial) noexcept {
    if(!lock_capture())return;
    action_capture.offered(serial);const auto fault=action_capture.fault();
    ReleaseSRWLockExclusive(&action_source_lock);
    if(fault!=lan::ActionFault::none)action_fault(fault);
}
void observe_source_body(const SourceObservation& observation) {
    if(!InterlockedCompareExchange(&action_source_enabled,0,0)||!observation.name||!observation.context)return;
    const auto context=pointer(reinterpret_cast<Token>(observation.context)+0x10);
    if(!context||pointer(context)!=action_source.character){InterlockedIncrement64(&status->action_ignored_sources);return;}
    const auto& source=action_source;const auto& f=source.frame;
    if(pointer(base+0x3d7a1e0)!=f.world||pointer(f.world+0x88)!=f.primary||pointer(f.world+0x48)!=f.registry||
        pointer(f.registry)!=f.primary||pointer(f.primary+0x1ff8)!=source.root||pointer(source.root+0x28)!=source.behavior||
        pointer(source.behavior+8)!=f.primary||pointer(source.behavior+0x10)!=source.graph||
        pointer(source.graph+0x28)!=source.character)throw InputFailure{Error::input_owner};
    char name[64]{};bool terminated=false;
    for(std::size_t i=0;i<sizeof(name);++i){name[i]=read<char>(reinterpret_cast<Token>(observation.name)+i);if(!name[i]){terminated=true;break;}}
    if(!terminated){InterlockedIncrement64(&status->action_unsupported_source);return;}
    const auto* binding=action_binding(std::string_view(name));
    if(!binding){InterlockedIncrement64(&status->action_unsupported_source);return;}
    if(lan::is_combat(binding->kind)&&!combat_preview){InterlockedIncrement64(&status->action_unsupported_source);return;}
    const auto map=pointer(pointer(pointer(source.character+0xe0)+0x100)+0x18)+0x28;
    if(observation.map!=map||observation.event_id!=binding->source_id)throw InputFailure{Error::input_value};
    const auto& variable=binding->kind==lan::ActionKind::ground_jump_ready?source.jump_angle:source.angle;
    const auto direction=lan::is_combat(binding->kind)?0.f:reinterpret_cast<ReadGraphFloat>(base+0xff3250)(&variable);
    if(action_capture.capture(binding->kind,action_source_ms(),direction)){
        InterlockedIncrement64(&status->action_captured);
        if(binding->kind==lan::ActionKind::ground_jump_ready)InterlockedIncrement64(&status->action_jump_captured);
        if(binding->kind==lan::ActionKind::ground_step)InterlockedIncrement64(&status->action_step_captured);
        if(binding->kind==lan::ActionKind::sprint_start_from_step)InterlockedIncrement64(&status->action_sprint_captured);
        if(lan::is_attack(binding->kind))InterlockedIncrement64(&status->action_attack_captured);
        if(lan::is_guard_enter(binding->kind))InterlockedIncrement64(&status->action_guard_enter_captured);
        if(lan::is_guard_exit(binding->kind))InterlockedIncrement64(&status->action_guard_exit_captured);
    }
    else action_fault(action_capture.fault());
}
void observation_safe(const SourceObservation& observation) noexcept {
    try {observe_source_body(observation);}catch(...){action_fault(lan::ActionFault::invalid);}
}
void guarded_observation(const SourceObservation& observation) noexcept {
    __try {observation_safe(observation);}
    __except(EXCEPTION_EXECUTE_HANDLER){action_fault(lan::ActionFault::invalid);}
}
void record_lookup(const char** name,int* result,void* map) noexcept {
    __try {
        source_observation->name=*name;source_observation->event_id=*result;
        source_observation->map=reinterpret_cast<Token>(map);
    } __except(EXCEPTION_EXECUTE_HANDLER){action_fault(lan::ActionFault::invalid);}
}
int* observe_event_lookup(void* map,const char** name,int* fallback) {
    const auto result=original_event_lookup(map,name,fallback);
    if(source_observation)record_lookup(name,result,map);
    return result;
}
bool primary_observation(const SourceObservation& observation) noexcept {
    if(!observation.context||!observation.name)return false;
    __try {
        const auto owner=*reinterpret_cast<Token*>(reinterpret_cast<Token>(observation.context)+0x10);
        if(owner&&*reinterpret_cast<void**>(owner)==InterlockedCompareExchangePointer(&action_source_character,nullptr,nullptr))return true;
        InterlockedIncrement64(&status->action_ignored_sources);return false;
    }__except(EXCEPTION_EXECUTE_HANDLER){action_fault(lan::ActionFault::invalid);return false;}
}
void* observe_lua_context(void* lua) {
    const auto result=original_lua_context(lua);
    if(source_observation&&source_observation->lua==lua)source_observation->context=result;
    return result;
}
int observe_fire_event(void* lua) {
    SourceObservation observation{lua};auto* previous=source_observation;
    source_observation=InterlockedCompareExchange(&action_source_enabled,0,0)?&observation:nullptr;
    int result{};
    // Observe the original helper return values. Never duplicate Lua stack
    // operations, substitute results, swallow native exceptions or skip events.
    __try {result=original_fire_event(lua);}
    __finally {source_observation=previous;}
    if(InterlockedCompareExchange(&action_source_enabled,0,0)&&primary_observation(observation)&&lock_capture()){
        regions.begin();guarded_observation(observation);regions.end();
        ReleaseSRWLockExclusive(&action_source_lock);
    }
    return result;
}

// Installed on the worker, with every extant process thread enrolled. The
// transaction aborts completely if any thread/anchor cannot be checked.
bool install_action_hooks(bool detach=false) noexcept {
    std::array<HANDLE,1024> threads{};std::size_t count{};
    if(!detach&&!action_anchors_match())return false;
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
    if(!detach){original_fire_event=reinterpret_cast<FireEvent>(base+0x13f92c0);
        original_event_lookup=reinterpret_cast<EventLookup>(base+0xaebeb0);original_lua_context=reinterpret_cast<LuaContext>(base+0x13fbfc0);}
    bool begun=false;LONG result=ERROR_INVALID_FUNCTION;
    if(okay){result=DetourTransactionBegin();begun=result==NO_ERROR;}
    if(begun){
        result=DetourUpdateThread(GetCurrentThread());
        for(std::size_t i=0;i<count;++i)if(result==NO_ERROR)result=DetourUpdateThread(threads[i]);
        const auto change=[detach](PVOID* target,PVOID hook){return detach?DetourDetach(target,hook):DetourAttach(target,hook);};
        if(result==NO_ERROR)result=change(reinterpret_cast<PVOID*>(&original_fire_event),reinterpret_cast<PVOID>(&observe_fire_event));
        if(result==NO_ERROR)result=change(reinterpret_cast<PVOID*>(&original_event_lookup),reinterpret_cast<PVOID>(&observe_event_lookup));
        if(result==NO_ERROR)result=change(reinterpret_cast<PVOID*>(&original_lua_context),reinterpret_cast<PVOID>(&observe_lua_context));
        if(result==NO_ERROR)result=DetourTransactionCommit();else DetourTransactionAbort();
    }
    for(std::size_t i=0;i<count;++i)CloseHandle(threads[i]);
    return begun&&result==NO_ERROR;
}

struct GuestActions {
    Token actor{},graph{},character{},queue{},map{};
    GraphVariable angle,speed,jump_angle,jump_stick;
    float prior_jump_angle{},prior_jump_stick{};
    std::array<int,action_bindings.size()> ids{};
    bool sprinting{},inputs{},guarding{};
    lan::ActionKind pending_attack_release{};
} guest_actions;
Token action_graph(Token actor) {return pointer(pointer(pointer(actor+0x1ff8)+0x28)+0x10);}
void validate_action_actor(Token actor) {
    const auto& f=game.frame;
    if(!actor||actor==f.primary||remote_actor.owned_actor(f)!=actor||pointer(base+0x3d7a1e0)!=f.world||
        pointer(f.world+0x88)!=f.primary||pointer(f.world+0x48)!=f.registry||pointer(f.registry)!=f.primary||
        pointer(f.registry+0x38)!=actor||!typed(actor,0x2a2b338)||!game.actor_ready(actor))throw InputFailure{Error::input_owner};
    const auto behavior=pointer(pointer(actor+0x1ff8)+0x28);
    if(!typed(behavior,0x2a790a8)||pointer(behavior+8)!=actor||read<std::uint64_t>(behavior+0xaf0)!=91)
        throw InputFailure{Error::input_owner};
    if(guest_actions.actor&&(guest_actions.actor!=actor||action_graph(actor)!=guest_actions.graph||
        pointer(guest_actions.graph+0x28)!=guest_actions.character||pointer(guest_actions.character+0xd0)!=guest_actions.queue))throw InputFailure{Error::input_owner};
}
void bind_guest_actions(Token actor) {
    validate_action_actor(actor);
    if(guest_actions.actor)return;
    GuestActions value;value.actor=actor;value.graph=action_graph(actor);
    value.angle=game.graph_variable(actor,1,209);value.speed=game.graph_variable(actor,2,213);
    value.jump_angle=game.graph_variable(actor,29,461);value.jump_stick=game.graph_variable(actor,30,491);
    value.character=pointer(value.graph+0x28);
    const auto source_graph=action_graph(game.frame.primary);
    if(source_graph==value.graph||pointer(source_graph+0x28)==value.character||
        value.angle.character!=value.character||value.speed.character!=value.character||
        value.jump_angle.character!=value.character||value.jump_stick.character!=value.character)
        throw InputFailure{Error::input_owner};
    value.queue=pointer(value.character+0xd0);
    value.map=pointer(pointer(pointer(value.character+0xe0)+0x100)+0x18)+0x28;
    using Lookup=int* (*)(void*,const char**,int*);
    const auto lookup=reinterpret_cast<Lookup>(base+0xaebeb0);
    for(std::size_t i=0;i<action_bindings.size();++i){const char* name=action_bindings[i].guest_name;int fallback=-1;
        const auto result=lookup(reinterpret_cast<void*>(value.map),&name,&fallback);
        const auto id=result==&fallback?fallback:read<int>(reinterpret_cast<Token>(result));
        if(id!=action_bindings[i].guest_id)throw InputFailure{Error::input_value};value.ids[i]=id;}
    const auto get=reinterpret_cast<ReadGraphFloat>(base+0xff3250);
    value.prior_jump_angle=get(&value.jump_angle);value.prior_jump_stick=get(&value.jump_stick);
    if(!std::isfinite(value.prior_jump_angle)||std::abs(value.prior_jump_angle)>360||
        !std::isfinite(value.prior_jump_stick)||std::abs(value.prior_jump_stick)>1)throw InputFailure{Error::input_value};
    guest_actions=value;
}
void enqueue_guest(lan::ActionKind kind,float direction) {
    const auto* binding=action_binding(kind);if(!binding||!std::isfinite(direction)||std::abs(direction)>360)throw InputFailure{Error::input_value};
    if(lan::is_combat(kind)&&!combat_preview)throw InputFailure{Error::input_value};
    auto& guest=guest_actions;bind_guest_actions(remote_actor.owned_actor(game.frame));validate_action_actor(guest.actor);
    const auto capacity=read<int>(guest.queue+8),tail=read<int>(guest.queue+0x10),count=read<int>(guest.queue+0x14);
    if(!valid_event_queue(capacity,tail,count))throw InputFailure{Error::input_range};
    const auto entry=pointer(guest.queue)+static_cast<Token>(tail==capacity?0:tail)*24;
    for(const auto range:{std::pair{guest.queue,std::size_t{24}},std::pair{entry,std::size_t{24}}}){
        MEMORY_BASIC_INFORMATION page{};
        if(!readable(range.first,range.second)||!regions.query(range.first,page)||
            ((page.Protect&0xff)!=PAGE_READWRITE&&(page.Protect&0xff)!=PAGE_WRITECOPY))throw InputFailure{Error::input_range};
    }
    const auto set=reinterpret_cast<WriteGraphFloat>(base+0xff3570);
    if(!lan::is_combat(kind)){
        const bool jump=kind==lan::ActionKind::ground_jump_ready;
        set(&guest.angle,direction);set(&guest.jump_angle,jump?direction:guest.prior_jump_angle);
        set(&guest.jump_stick,jump?1.f:guest.prior_jump_stick);guest.inputs=true;
    }
    // The native helper receives a pointer SLOT, not a character pointer.
    auto character=guest.character;using Enqueue=bool (*)(Token*,int);
    const auto index=static_cast<std::size_t>(binding-action_bindings.data());
    if(!reinterpret_cast<Enqueue>(base+0xff1d10)(&character,guest.ids[index]))throw InputFailure{Error::input_value};
    guest.sprinting=kind==lan::ActionKind::sprint_start_from_step;
    if(lan::is_guard_enter(kind)){
        guest.guarding=true;InterlockedExchange(&status->action_guard_rearm_required,0);
    }else if(lan::is_guard_exit(kind))guest.guarding=false;
    if(kind==lan::ActionKind::attack_combo1)guest.pending_attack_release=lan::ActionKind::attack_combo1_release;
    else if(kind==lan::ActionKind::attack_combo2)guest.pending_attack_release=lan::ActionKind::attack_combo2_release;
    else guest.pending_attack_release=lan::ActionKind::none;
    InterlockedExchange(&status->action_sprinting,guest.sprinting?1:0);
    InterlockedExchange(&status->action_guarding,guest.guarding?1:0);
}
void release_guest_actions() {
    auto& guest=guest_actions;if(!guest.actor||(!guest.inputs&&!guest.sprinting&&!guest.guarding&&guest.pending_attack_release==lan::ActionKind::none))return;
    validate_action_actor(guest.actor);
    const auto release=guest.pending_attack_release;
    const bool sprinting=guest.sprinting,guarding=guest.guarding;
    if(release!=lan::ActionKind::none)enqueue_guest(release,0);
    if(sprinting)enqueue_guest(lan::ActionKind::sprint_stop_ready,0);
    if(guarding)enqueue_guest(lan::ActionKind::guard_exit,0);
    const auto set=reinterpret_cast<WriteGraphFloat>(base+0xff3570);
    if(guest.inputs){set(&guest.jump_angle,guest.prior_jump_angle);set(&guest.jump_stick,guest.prior_jump_stick);set(&guest.speed,0);set(&guest.angle,0);}
    guest.inputs=guest.sprinting=guest.guarding=false;guest.pending_attack_release=lan::ActionKind::none;
    InterlockedExchange(&status->action_sprinting,0);InterlockedIncrement64(&status->action_released);
}
void retire_actions_body() noexcept {
    action_consumer_active=false;InterlockedExchange(&action_source_enabled,0);action_receiver.suspend();
    InterlockedExchange(&status->action_guard_rearm_required,0);
    try{release_guest_actions();}catch(...){/* A changed lifetime forbids old actor writes. */}
}
void guarded_retire_actions() noexcept {
    __try {retire_actions_body();}__except(EXCEPTION_EXECUTE_HANDLER){}
}
void receive_actions(const Frame&,bool following) {
    if(!combat_preview){
        const auto allowed=[](const lan::ActionSnapshot& history){
            if(history.guarding)return false;
            for(std::size_t i=0;i<history.count;++i)if(lan::is_combat(history.events[i].kind))return false;
            return true;
        };
        if(network_view.actions.latest()&&!allowed(network_view.actions.latest()->history))action_fault(lan::ActionFault::invalid);
        for(const auto& batch:network_view.actions.pending())if(!allowed(batch.history))action_fault(lan::ActionFault::invalid);
    }
    action_consumer_active=following&&!InterlockedCompareExchange(&status->action_error,0,0);
    action_receiver.observe(network_view.actions,action_consumer_active,GetTickCount64());
    if(action_receiver.fault()!=lan::ActionFault::none)action_fault(action_receiver.fault());
    if(InterlockedCompareExchange(&status->action_error,0,0)){guarded_retire_actions();remote_actor.fail(Error::input_value);return;}
    if(!action_receiver.active()){release_guest_actions();InterlockedExchange(&status->action_guard_rearm_required,0);}
    else {
        while(const auto event=action_receiver.take(GetTickCount64())){
            enqueue_guest(event->kind,event->direction);InterlockedIncrement64(&status->action_replayed);
            if(event->kind==lan::ActionKind::ground_jump_ready)InterlockedIncrement64(&status->action_jump_replayed);
            if(event->kind==lan::ActionKind::ground_step)InterlockedIncrement64(&status->action_step_replayed);
            if(event->kind==lan::ActionKind::sprint_start_from_step)InterlockedIncrement64(&status->action_sprint_replayed);
            if(lan::is_attack(event->kind))InterlockedIncrement64(&status->action_attack_replayed);
            if(lan::is_guard_enter(event->kind))InterlockedIncrement64(&status->action_guard_enter_replayed);
            if(lan::is_guard_exit(event->kind))InterlockedIncrement64(&status->action_guard_exit_replayed);
        }
        if(!action_receiver.active())release_guest_actions();
        else if(!action_receiver.pending()&&action_receiver.held_sprint()&&!guest_actions.sprinting)
            enqueue_guest(lan::ActionKind::sprint_start_from_step,network_view.actions.latest()->history.events[network_view.actions.latest()->history.count-1].direction);
        else if(!action_receiver.pending()&&!action_receiver.held_sprint()&&guest_actions.sprinting)
            enqueue_guest(lan::ActionKind::sprint_stop_ready,0);
        if(action_receiver.active()&&!action_receiver.pending()){
            if(!action_receiver.held_guard()&&guest_actions.guarding)enqueue_guest(lan::ActionKind::guard_exit,0);
            // A guard entry may open a deflect window. No neutral restoration
            // command is proven: never synthesize an old press on a baseline.
            InterlockedExchange(&status->action_guard_rearm_required,
                action_receiver.held_guard()&&!guest_actions.guarding?1:0);
        }
    }
    InterlockedExchange64(&status->action_discarded,static_cast<LONG64>(action_receiver.discarded()));
    const auto ack=action_receiver.acknowledgement();if(ack)network->try_ack_actions(ack);
}
