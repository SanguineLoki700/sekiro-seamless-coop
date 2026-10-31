// Explicit native-command LAN adapter. Never calls the legacy motion/event
// renderer; snapshots select initial placement and bound divergence only.
namespace native_lan {
std::uint64_t sent_at{},received_at{},issued{},consumed_count{},last_consumed{},source_origin{},arrival_origin{};
bool timeline_bound{};
std::array<lan::ReceivedCommand,command_queue_capacity> incoming;
void stop(Error error) noexcept {
    native_commands::commands_armed=false;native_commands::provider_disabled=true;
    native_commands::receiver.suspend();native_commands::neutralize();
    // Keep a still-owned Pad quarantined on neutral input. Clearing its
    // selection here would expose it to this machine's hardware after loss.
    native_commands::fault(native_commands::StopReason::delivery);
    demo.fail(error);network->request_stop();
}
void publish_result() noexcept {
    using namespace native_commands;
    publish_native_commands();
    if(!aborted.load())InterlockedExchange(&status->native_controller_phase,static_cast<LONG>(demo.phase==Phase::following?ControllerScenarioPhase::moving:ControllerScenarioPhase::waiting_ready));
    InterlockedExchange64(&status->native_controller_commands,static_cast<LONG64>(issued));
    InterlockedExchange64(&status->native_controller_consumed,static_cast<LONG64>(consumed_count));
    InterlockedExchange(&status->native_controller_output_bound,action_output_bound?1:0);
    InterlockedExchange(&status->native_controller_state_code,state_code);
}
}
void native_lan_step(void* creator) {
    using namespace native_commands;
    const auto now=GetTickCount64();network->try_read(network_view);
    if(pause_requested.exchange(false)){receiver.suspend();neutralize();commands_armed=false;}
    if(aborted.load()||mailbox.failed()){native_lan::stop(Error::input_owner);native_lan::publish_result();return;}
    if(demo.phase==Phase::failed||demo.phase==Phase::stopped){neutralize();network->request_stop();return;}
    const auto state=network_view.status.state;
    if(state==lan::State::failed||state==lan::State::lost||state==lan::State::stopped){native_lan::stop(Error::changed_world);return;}
    DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    const bool focused=foreground==GetCurrentProcessId();
    if(last_world_at&&(now<last_world_at||now-last_world_at>250)){
        commands_armed=false;receiver.suspend();neutralize();
    }
    last_world_at=now;game.creator=creator;Frame frame{};
    const bool loaded=game.sample(frame)&&frame.primary_ready;
    if(!loaded){if(owned_actor.load())native_lan::stop(Error::changed_world);return;}
    if(!local_loaded){source_world=frame.world;source_primary=frame.primary;source_registry=frame.registry;local_loaded=true;}
    if(source_world!=frame.world||source_primary!=frame.primary||source_registry!=frame.registry||frame.field_handle!=0xa000){native_lan::stop(Error::changed_world);return;}
    if(now-offered_at>=50){if(network->try_offer(game.local_motion()))offered_at=now;}
    const auto remote=network_view.remote.newest();
    const bool scene_fresh=remote&&now>=remote->time&&now-remote->time<=500&&remote->motion.descriptor==frame.field_handle;
    if(!owned_actor.load()){
        if(!focused||state!=lan::State::connected||!scene_fresh)return;
        const auto& p=remote->motion.position;
        if(std::hypot(p[0]-frame.position[0],p[1]-frame.position[1],p[2]-frame.position[2])>20.f)return;
        if(!load_at)load_at=now;if(now-load_at<2000)return;
        demo.external_placement=true;demo.initial_position={p[0],p[1],p[2],1};
    }
    demo.tick(game);
    if(demo.phase==Phase::failed||demo.phase==Phase::stopped){native_lan::stop(demo.error);return;}
    if(demo.phase!=Phase::following)return;
    NativePlayerState observation;
    if(!guarded_observe(observation)){native_lan::stop(Error::input_owner);return;}
    const bool enabled=focused&&observation.ready&&scene_fresh&&state==lan::State::connected;
    demo.motion_paused=!enabled;
    if(!network->try_set_command_following(enabled)){
        receiver.suspend();neutralize();commands_armed=false;return;
    }
    // Publish retained input history, not state-machine events or graph speed.
    if(now-native_lan::sent_at>=50){
        std::unique_lock lock(primary_lock,std::try_to_lock);
        if(lock){const auto command=primary_capture.latest();
            if(command&&primary_at&&now>=primary_at&&now-primary_at<=100&&network->try_offer_command(*command,enabled)){
                native_lan::sent_at=now;
                if(!primary_capture.offered(command->transition_latest)){native_lan::stop(Error::input_value);return;}
            }
        }
    }
    std::size_t count{};network->try_take_commands(native_lan::incoming,count);
    const auto delivery_now=GetTickCount64(); // Ingress may arrive during native reads above.
    for(std::size_t i=0;i<count;++i){const auto& message=native_lan::incoming[i];
        const auto& command=message.command;
        if(!native_lan::timeline_bound){native_lan::timeline_bound=true;native_lan::source_origin=command.source_ms;native_lan::arrival_origin=message.arrival_ms;}
        const bool timely=command.source_ms>=native_lan::source_origin&&message.arrival_ms>=native_lan::arrival_origin&&
            delivery_now>=message.arrival_ms&&delivery_now-message.arrival_ms<=command_expiry_ms&&
            (message.arrival_ms-native_lan::arrival_origin<=command.source_ms-native_lan::source_origin||
             message.arrival_ms-native_lan::arrival_origin-(command.source_ms-native_lan::source_origin)<=command_expiry_ms);
        const bool follow=enabled&&message.enabled&&timely;
        if(receiver.observe(message.scope,command,message.arrival_ms,follow)==CommandReceive::failed){native_lan::stop(Error::input_value);return;}
        native_lan::received_at=message.arrival_ms;++native_lan::issued;
    }
    if(!enabled||!native_lan::received_at||delivery_now-native_lan::received_at>command_expiry_ms){receiver.suspend();neutralize();commands_armed=false;}
    else {
        provider_disabled=false;
        if(const auto delivery=receiver.take(delivery_now)){
            if(delivery->neutralize)neutralize();
            else if(!adapter.apply_guest(*delivery)){native_lan::stop(Error::input_value);return;}
        }
        commands_armed=receiver.active();
    }
    if(const auto actual=mailbox.consumed();actual&&actual->source.frame_sequence>native_lan::last_consumed){
        native_lan::last_consumed=actual->source.frame_sequence;++native_lan::consumed_count;
    }
    // No pose correction: stop a representation that diverges substantially.
    if(enabled&&remote&&std::hypot(observation.position[0]-remote->motion.position[0],
       observation.position[1]-remote->motion.position[1],observation.position[2]-remote->motion.position[2])>8.f){native_lan::stop(Error::invalid_pose);return;}
    native_lan::publish_result();
}
