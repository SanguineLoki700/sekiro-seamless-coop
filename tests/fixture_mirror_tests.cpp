#include "fixture_mirror.h"
#include "skcoop/lan/session.hpp"
#include <iostream>
#include <stdexcept>
using namespace skcoop::lan;
void check(bool b){if(!b)throw std::runtime_error("Local mirror boundary failed");}
int main(){try{
    Motion origin;origin.epoch=7;origin.descriptor=0xa000;origin.position={5,0,5};origin.actions.epoch=7;
    auto separated=origin;separated.position[2]+=2;
    auto source=separated;source.position[0]++;source.speed=.75f;source.angle=30;
    ActionCapture capture(7);check(capture.capture(ActionKind::ground_jump_ready,1,45));
    source.actions=*capture.snapshot(2);
    const auto out=skcoop::fixture::mirror(origin,separated,source);check(out.has_value());
    check(out->position==std::array<float,3>{6,0,5}&&out->actions==source.actions&&out->speed==source.speed&&out->angle==source.angle);
    check(skcoop::fixture::mirror(origin,separated,separated)->position==origin.position);
    check(!skcoop::fixture::mirror(origin,origin,source));
    auto bad=source;bad.epoch++;check(!skcoop::fixture::mirror(origin,separated,bad));
    bad=source;bad.descriptor++;check(!skcoop::fixture::mirror(origin,separated,bad));
    bad=source;bad.actions={};check(!skcoop::fixture::mirror(origin,separated,bad));
    bad=source;bad.position[0]+=10;check(!skcoop::fixture::mirror(origin,separated,bad));
    bad=separated;bad.position[2]+=10;check(!skcoop::fixture::mirror(origin,bad,source));
    MotionBuffer buffer;check(!buffer.newest());check(buffer.push(origin,1000,1)==PushResult::accepted);
    check(buffer.push(source,1050,2)==PushResult::accepted);const auto latest=buffer.newest();
    check(latest&&latest->motion==source&&latest->sequence==2&&latest->time==1050);
    check(buffer.sample(1100)->motion.actions==ActionSnapshot{}&&buffer.newest()->motion.actions==source.actions);
    std::cout<<"PASS observed-anchor mirror bounds, exact action/pose pair and no interpolated pulse echo\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
