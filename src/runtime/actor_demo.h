#pragma once
#include <array>
#include <cstdint>

namespace skcoop::actors {
using Token = std::uintptr_t;
using Vector = std::array<float, 4>;
enum class Phase : long { off, waiting, placing, settling, following, stopped, failed };
enum class Error : long { none, binding, changed_world, factory, model_timeout, invalid_pose, native_fault, hook_timeout, hook_conflict };
struct Frame {
    Token world{}, primary{}, registry{}, guest{};
    std::uint32_t field_handle{};
    bool primary_ready{};
    Vector position{}, forward{};
    std::uint64_t milliseconds{};
};
class Game {
public:
    virtual ~Game() = default;
    // false means a title/loading state; after creation that ends this demo.
    virtual bool sample(Frame&) = 0;
    virtual Token create() = 0;
    virtual bool actor_ready(Token) = 0;
    virtual Vector position(Token) = 0;
    virtual void place(Token, const Vector&) = 0;
};
// Local transform-follow experiment, not network replication or locomotion input.
// At most one creation attempt. Never retries across loads, failures or expiry.
class Demo {
public:
    void tick(Game& game);
    void fail(Error e) noexcept { phase = Phase::failed; error = e; }
    Phase phase{Phase::waiting};
    Error error{};
    std::uint64_t creation_calls{}, ticks{}, pose_writes{};
private:
    Frame original_{};
    Token actor_{};
    Vector offset_{}, placement_{};
    std::uint64_t created_tick_{}, placed_tick_{}, last_ms_{}, follow_start_{};
};
bool valid_position(const Vector&) noexcept;
} // namespace skcoop::actors
