#pragma once
#include "skcoop/lan/peer.hpp"
#include <memory>
namespace skcoop::lan {
// One real game plus a loopback command mirror. These states never constitute
// proof of a second game, native physics, damage or authoritative combat.
enum class LocalPreviewPhase {
    starting,waiting_frame,waiting_separation,active,time_limit,path_limit,
    stale_source,source_ended,failed,stopped
};
bool finished(LocalPreviewPhase) noexcept;
struct LocalPreviewStatus {
    LocalPreviewPhase phase{LocalPreviewPhase::starting};
    Failure failure{};
    ActionFault action_fault{};
    std::uint64_t sent{},received{},offers{},mirrored_serial{};
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
