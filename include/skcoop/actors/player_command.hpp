#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace skcoop::actors {
// Logical input intents, independent of devices, native layouts and animation
// events. This finite vocabulary does not claim a working native binding.
enum class CommandButton : std::uint32_t {
    attack=1u<<0, guard=1u<<1, jump=1u<<2, dodge=1u<<3,
    interact=1u<<4, item=1u<<5,
};
using ButtonMask=std::uint32_t;
constexpr ButtonMask button_mask(CommandButton button) noexcept {return static_cast<ButtonMask>(button);}
inline constexpr ButtonMask command_button_mask=(1u<<6)-1;
inline constexpr std::size_t command_history_capacity=16,command_queue_capacity=64;
inline constexpr std::uint64_t command_expiry_ms=500;

struct MovementIntent {
    // World-plane direction: zero when intensity is zero, otherwise unit
    // length. The adapter, rather than a peer, resolves camera/control context.
    std::array<float,2> direction{};
    float intensity{};
    bool operator==(const MovementIntent&)const=default;
};
struct PlayerInput {
    MovementIntent movement;
    ButtonMask held{}; // Unsupported bits must be clear, never silently ignored.
    bool operator==(const PlayerInput&)const=default;
};
struct ButtonTransition {
    std::uint64_t serial{},source_ms{};
    CommandButton button{};
    bool pressed{};
    bool operator==(const ButtonTransition&)const=default;
};
struct PlayerCommand {
    std::uint64_t input_generation{},frame_sequence{},source_ms{};
    PlayerInput input;
    std::uint64_t transition_latest{};
    std::size_t transition_count{};
    std::array<ButtonTransition,command_history_capacity> transitions{};
    bool operator==(const PlayerCommand&)const=default;
};
// Supplied by the authenticated local caller, never trusted from a command.
// One receiver belongs to one session/controller lifetime. A new generation
// requires a new object, preventing previous-lifetime input from rearming it.
struct CommandScope {
    std::array<std::uint8_t,16> session{},connection{};
    std::uint64_t player{},input_generation{};
    bool operator==(const CommandScope&)const=default;
};
enum class CommandFault {none,invalid,scope_changed,clock_regressed,gap,overflow,sequence_exhausted};
bool valid(CommandButton)noexcept;
bool valid(const PlayerInput&,ButtonMask accepted_buttons=command_button_mask)noexcept;
bool valid(const PlayerCommand&,ButtonMask accepted_buttons=command_button_mask)noexcept;
bool valid(const CommandScope&)noexcept;

class PlayerCommandCapture {
public:
    explicit PlayerCommandCapture(std::uint64_t input_generation,ButtonMask accepted_buttons=command_button_mask)noexcept;
    // Call every source game frame, before a slower network offer. Press and
    // release between two offers remain distinct chronological transitions.
    bool capture(const PlayerInput&,std::uint64_t source_ms)noexcept;
    std::optional<PlayerCommand> latest()const noexcept;
    bool offered(std::uint64_t transition_serial)noexcept;
    CommandFault fault()const noexcept{return fault_;}
private:
    PlayerCommand command_;
    ButtonMask accepted_{};
    std::uint64_t offered_{};
    CommandFault fault_{};
    bool fail(CommandFault)noexcept;
};

struct PlayerCommandState {
    std::uint64_t input_generation{},frame_sequence{},source_ms{};
    PlayerInput input;
    bool operator==(const PlayerCommandState&)const=default;
};
struct CommandDelivery {
    PlayerCommandState state;
    // Press/release are edges, not aliases for state.input.held. A native
    // press-only request must not return true on every frame a button is held.
    // Multiple retained edges remain ordered; an adapter must not collapse
    // several taps into one Boolean or infer a missing edge from held metadata.
    std::size_t transition_count{};
    std::array<ButtonTransition,command_queue_capacity> transitions{};
    bool baseline{},neutralize{};
};
enum class CommandReceive {accepted,stale,failed};
class PlayerCommandReceiver {
public:
    explicit PlayerCommandReceiver(ButtonMask accepted_buttons=command_button_mask)noexcept;
    CommandReceive observe(const CommandScope&,const PlayerCommand&,std::uint64_t arrival_ms,bool following=true)noexcept;
    // A delivery owns each queued edge exactly once. Expiry, pause and failure
    // produce neutralization before a resumed baseline, even if no callback
    // observed the gap. Baselines contain current metadata and no old presses.
    std::optional<CommandDelivery> take(std::uint64_t now_ms)noexcept;
    void suspend()noexcept;
    bool active()const noexcept{return active_&&fault_==CommandFault::none;}
    CommandFault fault()const noexcept{return fault_;}
    std::size_t pending()const noexcept{return count_;}
private:
    CommandScope scope_;
    PlayerCommand known_;
    ButtonMask accepted_{};
    struct Pending {ButtonTransition transition;std::uint64_t arrival_ms{};};
    std::array<Pending,command_queue_capacity> pending_{};
    std::size_t count_{};
    std::uint64_t arrival_{},checked_at_{};
    bool bound_{},active_{},delivery_pending_{},baseline_pending_{},neutral_pending_{};
    CommandFault fault_{};
    CommandReceive fail(CommandFault)noexcept;
};

// Observation is separate from submitted input. Consumption identifiers must
// come from the native downstream consumer, never from provider writeback.
struct NativePlayerState {
    bool valid{},ready{};
    std::array<float,3> position{};
    bool attacking{},guarding{};
    std::uint64_t consumed_input_generation{},consumed_frame_sequence{};
};
class NativeControllerAdapter {
public:
    virtual ~NativeControllerAdapter()=default;
    virtual bool capture_primary(PlayerInput&)noexcept=0;
    // Distinct native press/hold/release requests must retain those semantics.
    // A baseline must never be interpreted as a new button press. Restoring a
    // held level is permitted only when the native path proves that distinction.
    virtual bool apply_guest(const CommandDelivery&)noexcept=0;
    virtual void neutralize_guest()noexcept=0;
    virtual bool observe_guest(NativePlayerState&)noexcept=0;
};
}
