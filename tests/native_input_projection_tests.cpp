#include "skcoop/actors/native_input_projection.hpp"
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace skcoop::actors;
namespace {
void check(bool yes,int line){if(!yes)throw std::runtime_error("Native input projection invariant at line "+std::to_string(line));}
#define require(value) check((value),__LINE__)
bool near(float a,float b,float tolerance=2e-6f){return std::abs(a-b)<=tolerance;}
NativeStickAxes projected(const MovementIntent& move,float yaw){const auto value=project_native_stick(move,yaw);require(value.has_value());return *value;}
void pair(const NativeStickAxes& value,float a,float b){
    require(near(value.raw[0],a)&&near(value.raw[1],b));
    for(std::size_t i=0;i<2;++i){
        require(std::isfinite(value.raw[i])&&std::abs(value.raw[i])<=1);
        require(std::bit_cast<std::uint32_t>(value.raw[i])==std::bit_cast<std::uint32_t>(value.processed[i]));
    }
}
void known_axes(){
    const auto half_pi=std::numbers::pi_v<float>/2;
    pair(projected({{1,0},1},0),0,-1);pair(projected({{0,1},1},0),-1,0);
    pair(projected({{-1,0},1},0),0,1);pair(projected({{0,-1},1},0),1,0);
    pair(projected({{1,0},1},half_pi),-1,0);pair(projected({{0,1},1},half_pi),0,1);
    pair(projected({{1,0},.35f},0),0,-.35f);
    const auto diagonal=std::sqrt(.5f);
    pair(projected({{diagonal,diagonal},1},std::numbers::pi_v<float>/4),-1,0);
    // Independent forward expectations catch an inverse/forward pair sharing
    // the same wrong sign or swapped axis convention.
    const auto x=movement_from_native_stick({0,-1},0),z=movement_from_native_stick({-1,0},0);
    require(x&&near(x->direction[0],1)&&near(x->direction[1],0)&&near(x->intensity,1));
    require(z&&near(z->direction[0],0)&&near(z->direction[1],1)&&near(z->intensity,1));
    const auto rotated=movement_from_native_stick({-1,0},half_pi);
    require(rotated&&near(rotated->direction[0],1)&&near(rotated->direction[1],0));
}
void roundtrips_and_wrap(){
    for(unsigned direction=0;direction<32;++direction)for(unsigned rotation=0;rotation<37;++rotation)
        for(const auto intensity:{.05f,.35f,1.f}){
            const auto heading=static_cast<float>(direction)*std::numbers::pi_v<float>/16;
            const auto yaw=(static_cast<float>(rotation)-18)*.37f;
            const MovementIntent move{{std::cos(heading),std::sin(heading)},intensity};
            const auto native=projected(move,yaw);pair(native,native.raw[0],native.raw[1]);
            const auto restored=movement_from_native_stick(native.raw,yaw);require(restored&&valid(PlayerInput{*restored,0}));
            require(near(restored->direction[0],move.direction[0])&&near(restored->direction[1],move.direction[1])&&near(restored->intensity,intensity));
            // Directly evaluate the observed forward transform too, rather
            // than relying only on the helper's inverse roundtrip.
            const auto world_x=-std::sin(yaw)*native.raw[0]-std::cos(yaw)*native.raw[1];
            const auto world_z=-std::cos(yaw)*native.raw[0]+std::sin(yaw)*native.raw[1];
            require(near(world_x,move.direction[0]*intensity)&&near(world_z,move.direction[1]*intensity));
        }
    const MovementIntent move{{.6f,.8f},.75f};const float yaw=.31f;const auto original=projected(move,yaw);
    for(int turns=-100;turns<=100;++turns){
        const auto wrapped=projected(move,yaw+static_cast<float>(turns)*2*std::numbers::pi_v<float>);
        require(near(wrapped.raw[0],original.raw[0],1e-4f)&&near(wrapped.raw[1],original.raw[1],1e-4f));
    }
}
void neutral_saturation_and_rejection(){
    for(const auto yaw:{0.f,-9.f,9.f,std::numeric_limits<float>::max()}){
        pair(projected({},yaw),0,0);const auto zero=movement_from_native_stick({0,0},yaw);require(zero&&*zero==MovementIntent{});
        const auto finite=projected({{1,0},1},yaw);pair(finite,finite.raw[0],finite.raw[1]);
    }
    const auto saturated=movement_from_native_stick({1,1},0);const auto diagonal=std::sqrt(.5f);
    require(saturated&&near(saturated->direction[0],-diagonal)&&near(saturated->direction[1],-diagonal)&&saturated->intensity==1);
    const auto after=projected(*saturated,0);pair(after,diagonal,diagonal);
    // Valid near-unit rounding is normalized before scaling, not amplified.
    const MovementIntent near_unit{{.7074f,.7074f},1};require(valid(PlayerInput{near_unit,0}));
    pair(projected(near_unit,std::numbers::pi_v<float>/4),-1,0);
    for(const auto bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}){
        require(!project_native_stick({},bad)&&!movement_from_native_stick({0,0},bad));
        require(!project_native_stick({{bad,0},1},0)&&!project_native_stick({{1,0},bad},0));
        require(!movement_from_native_stick({bad,0},0)&&!movement_from_native_stick({0,bad},0));
    }
    for(const auto bad:{-1.01f,1.01f})require(!movement_from_native_stick({bad,0},0)&&!movement_from_native_stick({0,bad},0));
    require(!project_native_stick({{1,0},-.01f},0)&&!project_native_stick({{1,0},1.01f},0));
    require(!project_native_stick({{0,0},1},0)&&!project_native_stick({{1,0},0},0));
    require(!project_native_stick({{.5f,.5f},1},0)&&!project_native_stick({{1,1},1},0));
    require(!project_native_stick({{1.000001f,0},1},0));
}
}
int main(){try{known_axes();roundtrips_and_wrap();neutral_saturation_and_rejection();
    std::cout<<"PASS pure world/native stick projection, paired axes, cardinal/diagonal roundtrips, yaw wrap, saturation and malformed-value rejection\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
