#include "actor_demo.h"
#include "locomotion_input.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace skcoop::actors;
void require(bool v) { if (!v) throw std::runtime_error("actor lifecycle invariant failed"); }
struct Fake final : Game {
    Frame f{1,2,3,0,0xa000,true,{0,0,0,1},{0,0,1,0},1000};
    Vector guest{};
    bool available=true, ready=true, factory_fails=false;
    unsigned calls{}, writes{};
    unsigned locomotion_commands{};
    bool locomotion_active{};
    bool sample(Frame& out) override { out=f; return available; }
    Token create() override { ++calls; if (factory_fails) return 0; f.guest=4; return 4; }
    bool actor_ready(Token p) override { require(p==4); return ready; }
    Vector position(Token p) override { require(p==4); return guest; }
    void place(Token p,const Vector& v) override { require(p==4); guest=v; ++writes; }
    void command_locomotion(Token p,bool enabled) override { require(p==4); ++locomotion_commands; locomotion_active=enabled; }
    void tick(Demo& d,unsigned n=1) { while(n--) { f.milliseconds+=16; d.tick(*this); } }
};
void following(Demo& d,Fake& g) { g.tick(d,62); require(d.phase==Phase::following && g.calls==1 && g.writes==1); }
int main() {
    try {
        { LocomotionInput input; require(valid_locomotion_input(input));
          input.direction={.7f,0,.7f,0}; input.rotation_target={0,3.14f,0,0}; require(valid_locomotion_input(input));
          input.direction_copy[3]=1; require(!valid_locomotion_input(input)); input.direction_copy[3]=0;
          input.direction[0]=2; require(!valid_locomotion_input(input)); input.direction[0]=0;
          input.rotation_delta[1]=std::numeric_limits<float>::quiet_NaN(); require(!valid_locomotion_input(input)); }
        { Vector delta{9,9,9,9};
          require(relative_rotation({0,-3.1f,0,0},{0,3.1f,0,0},delta));
          require(delta[1]>.08f && delta[1]<.09f && delta[3]==0);
          require(relative_rotation({0,3.1f,0,0},{0,-3.1f,0,0},delta));
          require(delta[1]<-.08f && delta[1]>-.09f);
          require(relative_rotation({0,1.f,0,0},{0,1.f,0,0},delta) && delta==Vector{});
          require(!relative_rotation({0,0,0,0},{0,std::numeric_limits<float>::infinity(),0,0},delta)); }
        { Demo d; Fake g; g.available=false; g.tick(d,100); require(g.calls==0); g.available=true; following(d,g);
          const auto before=g.guest; g.f.position[0]=3; g.tick(d); require(g.guest[0]>before[0] && g.guest[0]<=.0961f);
          const auto n=g.writes; g.tick(d,40); require(g.writes>n && g.calls==1); }
        { Demo d; Fake g; g.factory_fails=true; g.tick(d,500); require(g.calls==1 && d.phase==Phase::failed); }
        { Demo d; Fake g; g.ready=false; g.tick(d,121); require(d.error==Error::model_timeout && g.writes==0); }
        for (unsigned mutation=0;mutation<6;++mutation) {
            Demo d; Fake g; following(d,g); const auto n=g.writes;
            switch(mutation) { case 0: ++g.f.world; break; case 1: ++g.f.primary; break;
                case 2: ++g.f.registry; break; case 3: ++g.f.field_handle; break;
                case 4: g.f.guest=0; break; default: g.available=false; }
            g.tick(d,100); require(d.phase==Phase::stopped && g.calls==1 && g.writes==n);
            g.f={1,2,3,0,0xa000,true,{0,0,0,1},{0,0,1,0},1000}; g.available=true;
            g.tick(d,100); require(g.calls==1 && g.writes==n);
        }
        { Demo d; Fake g; g.f.forward[0]=std::numeric_limits<float>::quiet_NaN(); g.tick(d); require(g.calls==0); }
        { Demo d; Fake g; following(d,g); g.guest[0]=std::numeric_limits<float>::infinity();
          g.tick(d); require(d.error==Error::invalid_pose && g.writes==1); }
        { Demo d; Fake g; following(d,g); g.f.position[0]=100; g.tick(d); require(d.phase==Phase::stopped && g.writes==1); }
        { Demo d; Fake g; following(d,g); g.f.milliseconds+=120000; g.tick(d); require(d.phase==Phase::stopped && g.writes==1); }
        { Demo d; Fake g; following(d,g); g.f.milliseconds+=500; g.tick(d); require(d.phase==Phase::stopped && g.writes==1); }
        { Demo d; Fake g; following(d,g); g.tick(d,1000); require(d.phase==Phase::following && g.writes==1); }
        { Demo d; Fake g; following(d,g); d.motion_paused=true; g.f.position[0]=3;
          g.tick(d,60); require(g.writes==1 && d.phase==Phase::following);
          d.motion_paused=false; g.tick(d); require(g.writes==2 && g.guest[0]>.09f && g.guest[0]<.097f);
          d.motion_paused=true; ++g.f.world; g.tick(d); require(d.phase==Phase::stopped); }
        { Demo d; Fake g; d.native_locomotion=true; following(d,g);
          g.tick(d,10); require(g.locomotion_active && g.writes==1 && g.locomotion_commands==10);
          d.motion_paused=true; g.tick(d); require(!g.locomotion_active && g.writes==1);
          d.motion_paused=false; g.tick(d); require(g.locomotion_active);
          d.stop_requested=true; g.tick(d); require(!g.locomotion_active && d.phase==Phase::stopped);
          const auto n=g.locomotion_commands; g.tick(d,10); require(g.locomotion_commands==n); }
        { Demo d; Fake g; d.native_locomotion=true; following(d,g); g.tick(d);
          g.f.milliseconds+=500; g.tick(d); require(!g.locomotion_active && d.phase==Phase::following);
          g.tick(d); require(g.locomotion_active);
          g.f.milliseconds+=120000; g.tick(d); require(!g.locomotion_active && d.phase==Phase::stopped); }
        { Demo d; Fake g; d.native_pad_reference=true; following(d,g);
          g.f.position[0]=100; g.guest[0]=12; g.tick(d,120);
          require(d.phase==Phase::following && g.writes==1 && g.locomotion_commands==0 && d.observed_displacement_mm>9000);
          d.motion_paused=true; g.tick(d); require(g.writes==1 && g.locomotion_commands==0);
          g.f.milliseconds+=500; g.tick(d); require(d.phase==Phase::following);
          d.stop_requested=true; g.tick(d); require(d.phase==Phase::stopped && g.writes==1 && g.locomotion_commands==0); }
        { Demo d; Fake g; d.native_pad_reference=true;d.external_placement=true;d.initial_position={5,0,7,1};d.max_follow_ms=1500000;
          following(d,g);require(g.guest==d.initial_position);
          g.guest[0]+=1;g.f.milliseconds+=120001;g.tick(d);
          require(d.phase==Phase::following&&g.writes==1&&g.locomotion_commands==0&&g.guest[0]==6);
          g.f.milliseconds+=1500000;g.tick(d);require(d.phase==Phase::stopped&&g.writes==1); }
        { Demo d;Fake g;d.external_placement=true;d.initial_position[0]=std::numeric_limits<float>::quiet_NaN();
          g.tick(d);require(d.phase==Phase::failed&&g.calls==0&&g.writes==0); }
        std::cout<<"PASS local actor failure, scene lifetime, bounded movement and no-retry scenarios\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
