#pragma once
#include "skcoop/actors/player_command.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace skcoop::actors {
// Pure coordinate conversion only; this does not bind or call a native
// controller. Both pairs must be applied by the verified actor-local adapter.
struct NativeStickAxes {
    std::array<float,2> raw{},processed{};
    bool operator==(const NativeStickAxes&)const=default;
};
inline std::optional<NativeStickAxes> project_native_stick(const MovementIntent& movement,float yaw)noexcept {
    if(!std::isfinite(yaw)||!valid(PlayerInput{movement,0}))return {};
    if(movement.intensity==0)return NativeStickAxes{};
    const auto angle=std::remainder(static_cast<double>(yaw),2*std::numbers::pi_v<double>);
    const auto sine=std::sin(angle),cosine=std::cos(angle);
    // Remove the portable unit-vector validator's rounding tolerance before
    // scaling, so accepted near-unit values cannot exceed the requested speed.
    const auto length=std::hypot(static_cast<double>(movement.direction[0]),static_cast<double>(movement.direction[1]));
    const auto world_x=movement.direction[0]/length*movement.intensity;
    const auto world_z=movement.direction[1]/length*movement.intensity;
    NativeStickAxes result;
    result.raw={static_cast<float>(std::clamp(-sine*world_x-cosine*world_z,-1.,1.)),
                static_cast<float>(std::clamp(-cosine*world_x+sine*world_z,-1.,1.))};
    result.processed=result.raw;
    return result;
}
// Source-side conversion from observed native raw axes and controller yaw
// (radians) to world X/Z intent. Native diagonals inside the component bounds
// saturate radially at intensity1; malformed components are rejected, not hidden
// by a clamp. Context and controller ownership remain the adapter's concern.
inline std::optional<MovementIntent> movement_from_native_stick(const std::array<float,2>& raw,float yaw)noexcept {
    if(!std::isfinite(yaw))return {};
    for(const auto value:raw)if(!std::isfinite(value)||std::abs(value)>1)return {};
    if(raw==std::array<float,2>{})return MovementIntent{};
    const auto angle=std::remainder(static_cast<double>(yaw),2*std::numbers::pi_v<double>);
    const auto sine=std::sin(angle),cosine=std::cos(angle);
    const auto world_x=-sine*raw[0]-cosine*raw[1];
    const auto world_z=-cosine*raw[0]+sine*raw[1];
    const auto length=std::hypot(world_x,world_z);
    MovementIntent result{{static_cast<float>(world_x/length),static_cast<float>(world_z/length)},
                          static_cast<float>(std::min(length,1.))};
    if(!valid(PlayerInput{result,0}))return {};
    return result;
}
}
