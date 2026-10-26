#include "skcoop/lan/local_preview.hpp"
#include "skcoop/lan/scenario.hpp"
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace skcoop::lan {
namespace {
float gap(const Motion& a,const Motion& b) noexcept {
    return std::hypot(a.position[0]-b.position[0],a.position[1]-b.position[1],a.position[2]-b.position[2]);
}
}
bool finished(LocalPreviewPhase phase) noexcept {
    return phase==LocalPreviewPhase::time_limit||
        phase==LocalPreviewPhase::source_ended||phase==LocalPreviewPhase::failed||phase==LocalPreviewPhase::stopped;
}
std::string_view local_preview_phase_name(LocalPreviewPhase phase) noexcept {
    switch(phase){
    case LocalPreviewPhase::starting:return "starting";
    case LocalPreviewPhase::waiting_frame:return "waiting_frame";
    case LocalPreviewPhase::waiting_separation:return "waiting_separation";
    case LocalPreviewPhase::active:return "active";
    case LocalPreviewPhase::time_limit:return "time_limit";
    case LocalPreviewPhase::path_limit:return "path_paused";
    case LocalPreviewPhase::stale_source:return "stale_source";
    case LocalPreviewPhase::source_ended:return "source_ended";
    case LocalPreviewPhase::failed:return "failed";
    case LocalPreviewPhase::stopped:return "stopped";
    }
    return "unknown";
}
Build local_preview_build(){
    Build b;b.purpose=Purpose::native_avatar;b.adapter=independent_adapter;b.scenario=local_combat_scenario;
    constexpr char digest[]="637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856";
    const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(std::size_t i=0;i<b.game.size();++i)b.game[i]=static_cast<std::uint8_t>((digit(digest[i*2])<<4)|digit(digest[i*2+1]));
    return b;
}
LocalPreviewStep LocalPreviewMirror::step(const View& view,std::uint64_t now) noexcept {
    if(finished(phase_))return {};
    if(now<checked_){phase_=LocalPreviewPhase::failed;return {};}
    checked_=now;
    if(view.status.state==State::failed||view.actions.fault()!=ActionFault::none||view.status.action_fault!=ActionFault::none){phase_=LocalPreviewPhase::failed;return {};}
    if(view.status.state==State::lost||view.status.state==State::stopped){phase_=LocalPreviewPhase::source_ended;return {};}
    if(view.status.failure!=Failure::none){phase_=LocalPreviewPhase::failed;return {};}
    if(now-start_>=360000||(separated_&&now-active_at_>=90000)){phase_=LocalPreviewPhase::time_limit;return {};}
    if(view.status.state!=State::connected)return {};
    const auto pose=view.remote.newest();const auto& actions=view.actions.latest();
    if(!pose&&!actions)return {};
    if(!pose||!actions||pose->sequence!=actions->packet_sequence||pose->time!=actions->arrival_ms||
       pose->motion.actions!=actions->history||!valid(actions->scope)||!valid(local_preview_build(),pose->motion)||
       actions->scope.epoch!=pose->motion.epoch){phase_=LocalPreviewPhase::failed;return {};}
    if(now<pose->time){phase_=LocalPreviewPhase::failed;return {};}
    if(scope_&&*scope_!=actions->scope){phase_=LocalPreviewPhase::failed;return {};}
    if(!scope_)scope_=actions->scope;
    // Alt-Tab may pause the one real game's world callbacks. Keep the socket
    // alive but offer nothing old; native expiry releases/rebaselines inputs.
    // Scope/origin/separation and the original active deadline are retained.
    if(now-pose->time>action_frame_expiry_ms){phase_=LocalPreviewPhase::stale_source;return {{},actions->packet_sequence};}
    const auto& source=pose->motion;
    const auto pause_path=[&]{
        if(!path_paused_at_)path_paused_at_=now;
        phase_=LocalPreviewPhase::path_limit;
        return LocalPreviewStep{{},actions->packet_sequence};
    };
    if(!origin_){origin_=source;phase_=LocalPreviewPhase::waiting_separation;return {{},actions->packet_sequence};}
    if(source.epoch!=origin_->epoch||source.descriptor!=origin_->descriptor){phase_=LocalPreviewPhase::failed;return {};}
    if(!separated_){
        const auto separation=gap(*origin_,source);
        if(separation>6.f)return pause_path();
        if(path_paused_at_&&now-*path_paused_at_<local_preview_resume_delay_ms)return pause_path();
        path_paused_at_.reset();
        if(separation<1.25f){phase_=LocalPreviewPhase::waiting_separation;return {{},actions->packet_sequence};}
        separated_=source;active_at_=now;phase_=LocalPreviewPhase::active;
    }
    auto result=source;
    for(std::size_t i=0;i<3;++i)result.position[i]+=origin_->position[i]-separated_->position[i];
    if(!valid(result)){phase_=LocalPreviewPhase::failed;return {};}
    if(gap(result,*origin_)>6.f)return pause_path();
    if(path_paused_at_&&now-*path_paused_at_<local_preview_resume_delay_ms)return pause_path();
    path_paused_at_.reset();
    phase_=LocalPreviewPhase::active;
    return {result,actions->packet_sequence};
}
struct LocalPreview::Impl {
    Ticket ticket;std::atomic<bool> stop_requested{};std::thread thread;
    mutable std::mutex mutex;LocalPreviewStatus status;
    explicit Impl(const Ticket& t):ticket(t){}
    void publish(const LocalPreviewStatus& update){std::lock_guard lock(mutex);status=update;}
    void run()noexcept {
        LocalPreviewStatus update;
        try{
            SessionConfig config;config.role=Role::guest;config.ticket=ticket;config.participant=random_participant();config.build=local_preview_build();
            Peer peer(std::move(config),"127.0.0.1");const auto started=GetTickCount64();LocalPreviewMirror mirror(started);
            while(!stop_requested){
                const auto view=peer.read();const auto next=mirror.step(view,GetTickCount64());
                if(mirror.phase()!=update.phase){
                    if(mirror.phase()==LocalPreviewPhase::path_limit)++update.path_pauses;
                    if(mirror.phase()==LocalPreviewPhase::stale_source)++update.stale_pauses;
                }
                update.phase=mirror.phase();update.failure=view.status.failure;update.action_fault=view.status.action_fault;
                update.elapsed_ms=GetTickCount64()-started;
                update.sent=view.status.frames_sent;update.received=view.status.frames_received;
                if(finished(update.phase)){publish(update);break;}
                if(next.motion){
                    if(peer.try_offer(*next.motion)){++update.offers;update.mirrored_serial=next.motion->actions.latest;
                        if(next.acknowledge)peer.try_ack_actions(next.acknowledge);}
                }else if(next.acknowledge)peer.try_ack_actions(next.acknowledge);
                publish(update);Sleep(20);
            }
            if(stop_requested&&!finished(update.phase)){update.phase=LocalPreviewPhase::stopped;publish(update);}
            peer.stop();
        }catch(...){update.phase=LocalPreviewPhase::failed;update.failure=Failure::internal;publish(update);}
    }
};
LocalPreview::LocalPreview(const Ticket& ticket){
    if(ticket.address!="127.0.0.1"||!ticket.port||parse_ticket(ticket_text(ticket))!=ticket)
        throw std::invalid_argument("Local preview requires a valid loopback ticket");
    impl_=std::make_unique<Impl>(ticket);impl_->thread=std::thread([p=impl_.get()]{p->run();});
}
LocalPreview::~LocalPreview(){stop();}
LocalPreviewStatus LocalPreview::read()const{std::lock_guard lock(impl_->mutex);return impl_->status;}
void LocalPreview::stop()noexcept{if(!impl_)return;impl_->stop_requested=true;if(impl_->thread.joinable())impl_->thread.join();}
}
