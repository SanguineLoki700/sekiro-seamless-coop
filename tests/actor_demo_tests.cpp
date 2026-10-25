#include "actor_demo.h"
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
    bool sample(Frame& out) override { out=f; return available; }
    Token create() override { ++calls; if (factory_fails) return 0; f.guest=4; return 4; }
    bool actor_ready(Token p) override { require(p==4); return ready; }
    Vector position(Token p) override { require(p==4); return guest; }
    void place(Token p,const Vector& v) override { require(p==4); guest=v; ++writes; }
    void tick(Demo& d,unsigned n=1) { while(n--) { f.milliseconds+=16; d.tick(*this); } }
};
void following(Demo& d,Fake& g) { g.tick(d,62); require(d.phase==Phase::following && g.calls==1 && g.writes==1); }
int main() {
    try {
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
        std::cout<<"PASS local actor failure, scene lifetime, bounded movement and no-retry scenarios\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
