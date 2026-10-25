#pragma once
#include "actor_demo.h"
#include <cmath>
#include <numbers>
namespace skcoop::actors {
// Four observed float4 fields only: no buttons, pointers, owner or save data.
struct LocomotionInput { Vector direction{},rotation_delta{},rotation_target{},direction_copy{}; };
inline bool relative_rotation(const Vector& target,const Vector& current,Vector& out) noexcept {
    Vector delta{};
    for(unsigned i=0;i<3;++i) {
        if(!std::isfinite(target[i]) || !std::isfinite(current[i]))return false;
        delta[i]=std::remainder(target[i]-current[i],2*std::numbers::pi_v<float>);
        if(!std::isfinite(delta[i]))return false;
    }
    out=delta; return true;
}
inline bool valid_locomotion_input(const LocomotionInput& input) noexcept {
    for(const auto* v:{&input.direction,&input.direction_copy,&input.rotation_delta,&input.rotation_target}) {
        for(const float x:*v)if(!std::isfinite(x))return false;
        if(std::abs((*v)[3])>.001f)return false;
    }
    for(const auto* v:{&input.direction,&input.direction_copy})
        if(std::hypot((*v)[0],(*v)[1],(*v)[2])>1.5f)return false;
    for(const auto* v:{&input.rotation_delta,&input.rotation_target})
        for(unsigned i=0;i<3;++i)if(std::abs((*v)[i])>6.4f)return false;
    return true;
}
}
