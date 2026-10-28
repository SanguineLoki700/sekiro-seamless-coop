#pragma once
#include "skcoop/actors/player_command.hpp"

namespace skcoop::actors {
enum class NativeInputFault {none,invalid,generation_changed,clock_regressed,overflow};
enum class NativeInputOffer {accepted,stale,needs_baseline,failed};
struct NativeInputSource {
    std::uint64_t input_generation{},frame_sequence{};
    bool operator==(const NativeInputSource&)const=default;
};
// One trusted receiver/controller lifetime, owned on the native update thread.
// This class supplies input; it never calls a game API or claims consumption.
class NativeInputLatch {
public:
    explicit NativeInputLatch(std::uint64_t input_generation)noexcept;
    NativeInputOffer offer(const CommandDelivery&,std::uint64_t arrival_ms)noexcept;
    // At most one global button transition per strictly advancing update time.
    // Repeated calls at the same timestamp cannot consume a second transition.
    PlayerInput begin_update(std::uint64_t now_ms)noexcept;
    void suspend()noexcept;
    bool requires_baseline()const noexcept{return baseline_required_;}
    NativeInputFault fault()const noexcept{return fault_;}
    std::size_t pending()const noexcept{return count_;}
    // Only when the supplied output matches the complete newest source state
    // with no remaining edges. Intermediate coalesced pulses have no source
    // frame claim. Even this identity is provider metadata, not native evidence.
    std::optional<NativeInputSource> source_identity()const noexcept{return output_source_;}
private:
    struct Pending {ButtonTransition event;std::uint64_t arrival_ms{};};
    std::uint64_t generation_{},arrival_{},updated_at_{},last_serial_{};
    std::optional<PlayerCommandState> known_;
    std::optional<CommandDelivery> last_delivery_;
    std::optional<NativeInputSource> output_source_;
    std::array<Pending,command_queue_capacity> pending_{};
    std::array<ButtonTransition,command_queue_capacity> recent_{};
    std::size_t count_{},recent_count_{};
    ButtonMask held_{},blocked_{};
    PlayerInput output_;
    bool baseline_required_{true},active_{},have_update_{},fresh_baseline_{},known_input_{};
    NativeInputFault fault_{};
    NativeInputOffer fail(NativeInputFault)noexcept;
};
}
