#include "skcoop/actors/native_input_latch.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace skcoop::actors;
namespace {
void check(bool yes,int line){if(!yes)throw std::runtime_error("Native input latch invariant at line "+std::to_string(line));}
#define require(value) check((value),__LINE__)
constexpr auto attack=button_mask(CommandButton::attack),guard=button_mask(CommandButton::guard);
CommandDelivery command(std::uint64_t frame,std::uint64_t ms,ButtonMask held=0){
    CommandDelivery value;value.state={7,frame,ms,{{},held}};return value;
}
CommandDelivery initial(ButtonMask held=0){auto value=command(1,0,held);value.baseline=true;return value;}
void edge(CommandDelivery& delivery,std::uint64_t serial,std::uint64_t ms,CommandButton button,bool pressed){
    delivery.transitions[delivery.transition_count++]={serial,ms,button,pressed};
}
void ready(NativeInputLatch& latch,std::uint64_t arrival=0){require(latch.offer(initial(),arrival)==NativeInputOffer::accepted);require(latch.begin_update(arrival)==PlayerInput{});}
void rapid_pulses_and_duplicate_ingress(){
    NativeInputLatch latch(7);ready(latch);
    auto tap=command(2,13);edge(tap,1,10,CommandButton::attack,true);edge(tap,2,11,CommandButton::attack,false);
    edge(tap,3,12,CommandButton::attack,true);edge(tap,4,13,CommandButton::attack,false);
    require(latch.offer(tap,1)==NativeInputOffer::accepted&&latch.pending()==4);
    require(latch.begin_update(1).held==attack&&latch.pending()==3&&!latch.source_identity());
    require(latch.begin_update(1).held==attack&&latch.pending()==3); // Same native update.
    require(latch.offer(tap,2)==NativeInputOffer::stale&&latch.pending()==3);
    require(latch.begin_update(2).held==0&&latch.pending()==2&&!latch.source_identity());
    require(latch.begin_update(3).held==attack&&latch.pending()==1);
    require(latch.begin_update(4).held==0&&latch.pending()==0);
    const auto source=latch.source_identity();require(source&&source->input_generation==7&&source->frame_sequence==2);
    require(latch.begin_update(5).held==0);
    // Even a different newest frame cannot replay an already accepted serial.
    auto repeated=tap;repeated.state.frame_sequence=3;repeated.state.source_ms=14;
    repeated.state.input.movement={{1,0},.5f};
    require(latch.offer(repeated,6)==NativeInputOffer::accepted&&latch.pending()==0);
    const auto moved=latch.begin_update(6);require(moved.held==0&&moved.movement==repeated.state.input.movement);
    repeated.state.frame_sequence=4;repeated.state.source_ms=15;repeated.transitions[0].pressed=false;
    require(latch.offer(repeated,7)==NativeInputOffer::failed&&latch.fault()==NativeInputFault::invalid);
    require(latch.begin_update(7)==PlayerInput{}&&!latch.source_identity());
}
void latest_movement_and_held_state(){
    NativeInputLatch latch(7);ready(latch);
    auto press=command(2,1,attack);edge(press,1,1,CommandButton::attack,true);
    require(latch.offer(press,1)==NativeInputOffer::accepted);
    auto latest=command(3,2,attack);latest.state.input.movement={{0,-1},.35f};
    require(latch.offer(latest,2)==NativeInputOffer::accepted);
    auto value=latch.begin_update(2);require(value.held==attack&&value.movement==latest.state.input.movement);
    require(latch.source_identity()&&latch.source_identity()->frame_sequence==3);
    for(std::uint64_t now=3;now<20;++now)require(latch.begin_update(now).held==attack);
    auto released=command(4,3);edge(released,2,3,CommandButton::attack,false);
    require(latch.offer(released,20)==NativeInputOffer::accepted&&latch.begin_update(20).held==0);
    // Missing/expired press metadata may not invent a new native button edge.
    auto metadata=command(5,4,guard);require(latch.offer(metadata,21)==NativeInputOffer::accepted);
    require(latch.begin_update(21).held==0&&!latch.source_identity());
}
void baseline_rearm_and_neutralization(){
    NativeInputLatch latch(7);require(latch.offer(initial(guard),0)==NativeInputOffer::accepted);
    require(latch.begin_update(0).held==0&&!latch.source_identity());
    // A press while the baseline-held bit is blocked cannot bypass rearming.
    auto held=command(2,1,guard);edge(held,1,1,CommandButton::guard,true);
    require(latch.offer(held,1)==NativeInputOffer::accepted&&latch.begin_update(1).held==0);
    auto release=command(3,2);edge(release,2,2,CommandButton::guard,false);
    require(latch.offer(release,2)==NativeInputOffer::accepted&&latch.begin_update(2).held==0);
    auto fresh=command(4,3,guard);edge(fresh,3,3,CommandButton::guard,true);
    require(latch.offer(fresh,3)==NativeInputOffer::accepted&&latch.begin_update(3).held==guard);
    auto extra=command(5,4,guard|attack);edge(extra,4,4,CommandButton::attack,true);
    require(latch.offer(extra,4)==NativeInputOffer::accepted&&latch.pending()==1);
    auto zero=command(5,4);zero.neutralize=true;
    require(latch.offer(zero,5)==NativeInputOffer::accepted&&latch.requires_baseline()&&latch.pending()==0);
    require(latch.begin_update(5)==PlayerInput{}&&!latch.source_identity());
    require(latch.offer(zero,6)==NativeInputOffer::stale);
    auto resume=extra;resume.transitions={};resume.transition_count=0;resume.baseline=true;
    require(latch.offer(resume,6)==NativeInputOffer::accepted&&latch.begin_update(6).held==0);
    // State alone clearing a held bit does not claim the required release edge.
    auto metadata=command(6,5);require(latch.offer(metadata,7)==NativeInputOffer::accepted&&latch.begin_update(7).held==0);
    auto ignored=command(7,6,guard);edge(ignored,5,6,CommandButton::guard,true);
    require(latch.offer(ignored,8)==NativeInputOffer::accepted&&latch.begin_update(8).held==0);
    auto rearm=command(8,7);edge(rearm,6,7,CommandButton::guard,false);
    require(latch.offer(rearm,9)==NativeInputOffer::accepted&&latch.begin_update(9).held==0);
    auto pressed=command(9,8,guard);edge(pressed,7,8,CommandButton::guard,true);
    require(latch.offer(pressed,10)==NativeInputOffer::accepted&&latch.begin_update(10).held==guard);
    latch.suspend();require(latch.begin_update(11)==PlayerInput{}&&latch.requires_baseline()&&latch.pending()==0);
}
void expiry_and_stalled_updates(){
    NativeInputLatch latch(7);ready(latch);
    auto press=command(2,1,attack);edge(press,1,1,CommandButton::attack,true);
    require(latch.offer(press,1)==NativeInputOffer::accepted&&latch.begin_update(1).held==attack);
    require(latch.begin_update(501).held==attack&&!latch.requires_baseline());
    require(latch.begin_update(502)==PlayerInput{}&&latch.requires_baseline()&&latch.fault()==NativeInputFault::none);
    auto next=command(3,2,attack);require(latch.offer(next,503)==NativeInputOffer::needs_baseline);
    next.baseline=true;require(latch.offer(next,503)==NativeInputOffer::accepted&&latch.begin_update(503).held==0);
    require(!latch.source_identity());
    // A duplicate baseline may resume a short explicit pause, but cannot renew
    // an old frame's freshness lease after that frame has expired.
    latch.suspend();require(latch.offer(next,600)==NativeInputOffer::accepted);
    require(latch.begin_update(600).held==0);require(latch.begin_update(1004)==PlayerInput{}&&latch.requires_baseline());
    require(latch.offer(next,1005)==NativeInputOffer::needs_baseline);

    NativeInputLatch stalled(7);ready(stalled);
    require(stalled.offer(press,1)==NativeInputOffer::accepted&&stalled.begin_update(1).held==attack);
    for(std::uint64_t i=1;i<=5;++i){auto update=command(i+2,i+1,attack);require(stalled.offer(update,i*100)==NativeInputOffer::accepted);}
    require(stalled.begin_update(502)==PlayerInput{}&&stalled.requires_baseline()); // Latest ingress only2ms old.
    auto restore=command(8,8,attack);restore.baseline=true;
    require(stalled.offer(restore,503)==NativeInputOffer::accepted&&stalled.begin_update(503).held==0);

    NativeInputLatch before_first(7);require(before_first.offer(command(1,0),0)==NativeInputOffer::needs_baseline);
    require(before_first.begin_update(0)==PlayerInput{}&&before_first.fault()==NativeInputFault::none);
    NativeInputLatch old_edges(7);ready(old_edges);
    auto aged=command(2,1000,attack);edge(aged,1,1,CommandButton::attack,true);
    require(old_edges.offer(aged,1)==NativeInputOffer::needs_baseline&&old_edges.begin_update(1)==PlayerInput{});
}
void invalid_ingress_and_overflow(){
    for(unsigned defect=0;defect<13;++defect){
        NativeInputLatch latch(7);ready(latch);auto bad=command(2,2,attack);edge(bad,1,1,CommandButton::attack,true);
        auto expected=NativeInputFault::invalid;
        switch(defect){
        case 0:bad.state.input_generation=8;expected=NativeInputFault::generation_changed;break;
        case 1:bad.state.frame_sequence=0;break;
        case 2:bad.state.input.movement.intensity=std::numeric_limits<float>::quiet_NaN();break;
        case 3:bad.state.input.held=1u<<9;break;
        case 4:bad.transition_count=command_queue_capacity+1;break;
        case 5:bad.transitions[1]=bad.transitions[0];break;
        case 6:bad.transitions[0].serial=0;break;
        case 7:bad.transitions[0].button=static_cast<CommandButton>(3);break;
        case 8:bad.transitions[0].source_ms=3;break;
        case 9:bad.baseline=true;break;
        case 10:bad.neutralize=true;break;
        case 11:bad.baseline=bad.neutralize=true;bad.transition_count=0;bad.transitions={};bad.state.input={};break;
        case 12:edge(bad,2,0,CommandButton::attack,false);break;
        }
        require(latch.offer(bad,1)==NativeInputOffer::failed&&latch.fault()==expected);
        require(latch.pending()==0&&latch.begin_update(1)==PlayerInput{}&&!latch.source_identity());
        require(latch.offer(initial(),2)==NativeInputOffer::failed&&latch.fault()==expected);
    }
    NativeInputLatch invalid(0);require(invalid.fault()==NativeInputFault::invalid&&invalid.begin_update(0)==PlayerInput{});
    NativeInputLatch full(7);ready(full);auto flood=command(2,64);
    for(std::uint64_t i=1;i<=command_queue_capacity;++i)edge(flood,i,i,CommandButton::attack,i%2!=0);
    require(full.offer(flood,1)==NativeInputOffer::accepted&&full.pending()==command_queue_capacity);
    auto extra=command(3,65,attack);edge(extra,65,65,CommandButton::attack,true);
    require(full.offer(extra,2)==NativeInputOffer::failed&&full.fault()==NativeInputFault::overflow&&full.pending()==0);
    require(full.begin_update(2)==PlayerInput{});
    NativeInputLatch expired_queue(7);ready(expired_queue);require(expired_queue.offer(flood,1)==NativeInputOffer::accepted);
    require(expired_queue.begin_update(400).held==attack);
    require(expired_queue.offer(command(3,65),450)==NativeInputOffer::accepted);
    require(expired_queue.begin_update(502)==PlayerInput{}&&expired_queue.requires_baseline()&&expired_queue.pending()==0);
}
void clocks_and_source_identity(){
    NativeInputLatch clock(7);ready(clock,100);require(clock.begin_update(99)==PlayerInput{}&&clock.fault()==NativeInputFault::clock_regressed);
    NativeInputLatch future(7);require(future.offer(initial(),100)==NativeInputOffer::accepted);
    require(future.begin_update(99)==PlayerInput{}&&future.fault()==NativeInputFault::clock_regressed);
    NativeInputLatch ingress(7);ready(ingress,100);
    require(ingress.offer(command(2,1),99)==NativeInputOffer::failed&&ingress.fault()==NativeInputFault::clock_regressed);
    NativeInputLatch source(7);auto first=initial();first.state.source_ms=100;
    require(source.offer(first,0)==NativeInputOffer::accepted);source.begin_update(0);
    require(source.offer(command(2,99),1)==NativeInputOffer::failed&&source.fault()==NativeInputFault::clock_regressed);
    NativeInputLatch stable(7);ready(stable);require(stable.source_identity()&&stable.source_identity()->frame_sequence==1);
    auto move=command(2,1);move.state.input.movement={{1,0},.5f};
    require(stable.offer(move,1)==NativeInputOffer::accepted&&!stable.source_identity());
    require(stable.begin_update(1).movement==move.state.input.movement&&stable.source_identity()->frame_sequence==2);
    auto mutated=move;mutated.state.input.movement.intensity=.6f;
    require(stable.offer(mutated,2)==NativeInputOffer::failed&&stable.fault()==NativeInputFault::invalid);
}
void actual_receiver_handoff(){
    CommandScope scope;scope.session[0]=1;scope.connection[0]=2;scope.player=3;scope.input_generation=7;
    PlayerCommandCapture source(7);PlayerCommandReceiver receiver;NativeInputLatch latch(7);
    require(source.capture({},0));auto captured=source.latest();require(captured&&source.offered(captured->transition_latest));
    require(receiver.observe(scope,*captured,1000)==CommandReceive::accepted);
    auto delivered=receiver.take(1000);require(delivered&&delivered->baseline);
    require(latch.offer(*delivered,1000)==NativeInputOffer::accepted&&latch.begin_update(1000)==PlayerInput{});
    std::uint64_t serial{},source_ms{},arrival=1000;
    // Several real source frames occur before each slower receiver offer.
    // The native update receives every tap as a distinct high/low frame.
    for(unsigned batch=0;batch<40;++batch){
        for(unsigned pulse=0;pulse<2;++pulse){
            PlayerInput press;press.held=attack;require(source.capture(press,++source_ms));
            require(source.capture({},++source_ms));serial+=2;
        }
        captured=source.latest();require(captured&&captured->transition_latest==serial&&source.offered(serial));
        arrival+=64;require(receiver.observe(scope,*captured,arrival)==CommandReceive::accepted);
        delivered=receiver.take(arrival);require(delivered&&delivered->transition_count==4);
        require(latch.offer(*delivered,arrival)==NativeInputOffer::accepted);
        require(latch.offer(*delivered,arrival)==NativeInputOffer::stale);
        for(std::uint64_t step=0;step<4;++step){
            const auto value=latch.begin_update(arrival+step*16);
            require(value.held==(step%2==0?attack:0));
            require(latch.pending()==3-step);
            require(latch.source_identity().has_value()==(step==3));
        }
    }
    require(latch.fault()==NativeInputFault::none&&receiver.fault()==CommandFault::none&&serial==160);
}
}
int main(){try{rapid_pulses_and_duplicate_ingress();latest_movement_and_held_state();baseline_rearm_and_neutralization();expiry_and_stalled_updates();invalid_ingress_and_overflow();clocks_and_source_identity();actual_receiver_handoff();
    std::cout<<"PASS portable native input latch, spaced rapid pulses, duplicate suppression, baseline rearm, neutralization, freshness and bounded ingress\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
