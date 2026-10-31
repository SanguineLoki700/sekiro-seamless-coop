#pragma once
#include "skcoop/lan/peer.hpp"
#include <memory>
#include <string_view>
namespace skcoop::lan {
// One real game plus a loopback command mirror. These states never constitute
// proof of a second game, native physics, damage or authoritative combat.
enum class LocalPreviewPhase {
    starting,waiting_frame,waiting_separation,active,time_limit,path_limit,
    stale_source,source_ended,failed,stopped
};
bool finished(LocalPreviewPhase) noexcept;
std::string_view local_preview_phase_name(LocalPreviewPhase) noexcept;
// The peer may repeat its previous offer for 500 ms. Leave enough silence
// after a path pause for that offer and the receiver's 500-ms freshness lease
// to expire before handing off a fresh baseline. No old pose is refreshed.
inline constexpr std::uint64_t local_preview_resume_delay_ms=1100;
struct LocalPreviewStatus {
    LocalPreviewPhase phase{LocalPreviewPhase::starting};
    Failure failure{};
    ActionFault action_fault{};
    std::uint64_t sent{},received{},offers{},mirrored_serial{};
    std::uint64_t elapsed_ms{},path_pauses{},stale_pauses{};
};
struct LocalPreviewStep {
    std::optional<Motion> motion;
    std::uint64_t acknowledge{};
};
// Pure policy: exact authenticated source pose/history, observed safe origin,
// measured separation and bounded translated path. No clocks or game calls.
class LocalPreviewMirror {
public:
    explicit LocalPreviewMirror(std::uint64_t start_ms) noexcept:start_(start_ms),checked_(start_ms){}
    LocalPreviewStep step(const View&,std::uint64_t now_ms) noexcept;
    LocalPreviewPhase phase() const noexcept{return phase_;}
private:
    LocalPreviewPhase phase_{LocalPreviewPhase::waiting_frame};
    std::uint64_t start_{},checked_{},active_at_{};
    std::optional<std::uint64_t> path_paused_at_;
    std::optional<Motion> origin_,separated_;
    std::optional<ActionScope> scope_;
};
Build local_preview_build();
class LocalPreview {
public:
    // Only a canonical loopback ticket is accepted. Call after the native host
    // is listening. This owns a background peer, never another game process.
    explicit LocalPreview(const Ticket&);
    ~LocalPreview();
    LocalPreview(const LocalPreview&)=delete;
    LocalPreview& operator=(const LocalPreview&)=delete;
    LocalPreviewStatus read()const;
    void stop()noexcept; // Join outside game callbacks/loader lock.
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
