#pragma once
#include "skcoop/actors/player_command.hpp"
#include <algorithm>
#include <cmath>

namespace skcoop::actors {
// Native results retain their observed values. Direction remains tightly
// checked; intensity allows max(0.0001 absolute, 0.1% relative) difference.
// This comparison supplies neither consumption evidence nor physical motion.
inline bool native_movement_matches(const MovementIntent& expected,const MovementIntent& observed)noexcept {
    if(!valid(PlayerInput{expected,0})||!valid(PlayerInput{observed,0}))return false;
    constexpr float direction_tolerance=.0001f,intensity_absolute=.0001f,intensity_relative=.001f;
    const auto tolerance=std::max(intensity_absolute,intensity_relative*std::max(expected.intensity,observed.intensity));
    return std::abs(expected.intensity-observed.intensity)<=tolerance&&
        std::abs(expected.direction[0]-observed.direction[0])<=direction_tolerance&&
        std::abs(expected.direction[1]-observed.direction[1])<=direction_tolerance;
}
}
