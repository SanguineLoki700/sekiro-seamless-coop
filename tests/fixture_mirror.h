#pragma once
#include "skcoop/lan/protocol.hpp"
#include <cmath>
namespace skcoop::fixture {
inline float gap(const lan::Motion& a,const lan::Motion& b) noexcept {
    return std::hypot(a.position[0]-b.position[0],a.position[1]-b.position[1],a.position[2]-b.position[2]);
}
// A one-game diagnostic. The anchor is an observed pose, then the owner walks
// away. Only a short translated path is allowed. This is not a second player,
// native navigation validation or independent physics evidence.
inline std::optional<lan::Motion> mirror(const lan::Motion& anchor,const lan::Motion& separated,const lan::Motion& source) noexcept {
    for(const auto* motion:{&anchor,&separated,&source})
        if(!lan::valid(*motion)||!lan::valid(motion->actions)||motion->epoch!=anchor.epoch||
            motion->descriptor!=0xa000||motion->actions.epoch!=motion->epoch)return {};
    const auto separation=gap(anchor,separated);
    if(separation<1.25f||separation>6.f)return {};
    auto result=source;
    for(std::size_t i=0;i<3;++i)result.position[i]+=anchor.position[i]-separated.position[i];
    if(!lan::valid(result)||gap(result,anchor)>6.f)return {};
    return result;
}
}
