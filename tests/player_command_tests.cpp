#include "skcoop/actors/player_command.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace skcoop::actors;
namespace {
void check(bool yes,int line){if(!yes)throw std::runtime_error("Player command invariant at line "+std::to_string(line));}
#define require(value) check((value),__LINE__)
CommandScope scope(){CommandScope value;value.session[0]=1;value.connection[0]=2;value.player=3;value.input_generation=7;return value;}
PlayerInput input(ButtonMask held=0){PlayerInput value;value.held=held;return value;}
PlayerCommand latest(PlayerCommandCapture& source){const auto value=source.latest();require(value.has_value()&&valid(*value));return *value;}
void source_frame(PlayerCommandCapture& source,const PlayerInput& value,std::uint64_t ms){require(source.capture(value,ms));}
CommandDelivery delivery(PlayerCommandReceiver& receiver,std::uint64_t now){const auto value=receiver.take(now);require(value.has_value());return *value;}
void baseline(PlayerCommandCapture& source,PlayerCommandReceiver& receiver,std::uint64_t arrival=1000){
    source_frame(source,{},0);const auto first=latest(source);
    require(receiver.observe(scope(),first,arrival)==CommandReceive::accepted);
    const auto out=delivery(receiver,arrival);require(out.baseline&&!out.neutralize&&out.transition_count==0);
    require(source.offered(first.transition_latest));
}
void rapid_edges_and_duplicates(){
    PlayerCommandCapture source(7);PlayerCommandReceiver receiver;baseline(source,receiver);
    const auto old=latest(source);const auto attack=button_mask(CommandButton::attack),guard=button_mask(CommandButton::guard);
    source_frame(source,input(attack),10);source_frame(source,{},11);
    source_frame(source,input(guard),12);source_frame(source,{},13);source_frame(source,{},50);
    const auto command=latest(source);require(command.transition_latest==4&&command.transition_count==4);
    require(receiver.observe(scope(),command,1050)==CommandReceive::accepted);require(source.offered(command.transition_latest));
    const auto out=delivery(receiver,1050);require(!out.baseline&&!out.neutralize&&out.state.input.held==0&&out.transition_count==4);
    for(std::size_t i=0;i<4;++i){require(out.transitions[i].serial==i+1&&out.transitions[i].source_ms==10+i);require(out.transitions[i].pressed==(i%2==0));}
    require(out.transitions[0].button==CommandButton::attack&&out.transitions[2].button==CommandButton::guard);
    require(!receiver.take(1050));
    require(receiver.observe(scope(),command,1200)==CommandReceive::stale&&!receiver.take(1200));
    require(receiver.observe(scope(),old,2000)==CommandReceive::stale);
    // A duplicate cannot extend the original freshness lease.
    const auto zero=delivery(receiver,1551);require(zero.neutralize&&zero.state.input==PlayerInput{}&&!receiver.active());
    require(!receiver.take(1600)&&receiver.fault()==CommandFault::none);
}
void held_pause_and_expiry(){
    const auto guard=button_mask(CommandButton::guard);
    PlayerCommandCapture source(7);PlayerCommandReceiver receiver;baseline(source,receiver);
    source_frame(source,input(guard),1);auto command=latest(source);require(source.offered(command.transition_latest));
    require(receiver.observe(scope(),command,1001)==CommandReceive::accepted);
    auto out=delivery(receiver,1001);require(out.transition_count==1&&out.transitions[0].pressed&&out.state.input.held==guard);
    receiver.suspend();out=delivery(receiver,1002);require(out.neutralize&&out.state.input==PlayerInput{}&&out.transition_count==0);
    require(receiver.observe(scope(),command,1001)==CommandReceive::accepted);
    out=delivery(receiver,1003);require(out.baseline&&out.state.input.held==guard&&out.transition_count==0&&!out.neutralize);
    source_frame(source,{},2);command=latest(source);require(source.offered(command.transition_latest));
    require(receiver.observe(scope(),command,1004)==CommandReceive::accepted);
    out=delivery(receiver,1004);require(out.transition_count==1&&!out.transitions[0].pressed&&out.state.input.held==0);
    // No world observation occurs during this long pause. Every source edge
    // was handed off locally, but downstream receives only the latest history.
    for(std::uint64_t i=3;i<=26;++i){source_frame(source,input(i%2?guard:0),i);require(source.offered(latest(source).transition_latest));}
    source_frame(source,input(guard),1000);command=latest(source);
    require(receiver.observe(scope(),command,2100)==CommandReceive::accepted);
    out=delivery(receiver,2100);require(out.neutralize&&out.transition_count==0);
    out=delivery(receiver,2100);require(out.baseline&&!out.neutralize&&out.transition_count==0&&out.state.input.held==guard);
    require(!receiver.take(2100));
    source_frame(source,{},1001);command=latest(source);
    require(receiver.observe(scope(),command,2101)==CommandReceive::accepted);
    out=delivery(receiver,2101);require(out.transition_count==1&&!out.transitions[0].pressed);
    require(receiver.fault()==CommandFault::none);
}
void boundary_and_pending_expiry(){
    const auto attack=button_mask(CommandButton::attack);
    for(const auto elapsed:{command_expiry_ms,command_expiry_ms+1}){
        PlayerCommandCapture source(7);PlayerCommandReceiver receiver;baseline(source,receiver);
        for(std::uint64_t i=1;i<=20;++i){source_frame(source,input(i%2?attack:0),i);require(source.offered(latest(source).transition_latest));}
        const auto result=receiver.observe(scope(),latest(source),1000+elapsed);
        if(elapsed==command_expiry_ms){require(result==CommandReceive::failed&&receiver.fault()==CommandFault::gap);require(delivery(receiver,1500).neutralize);}
        else {require(result==CommandReceive::accepted);require(delivery(receiver,1501).neutralize);require(delivery(receiver,1501).baseline);}
    }
    PlayerCommandCapture source(7);PlayerCommandReceiver receiver;baseline(source,receiver);
    source_frame(source,input(attack),1);auto command=latest(source);require(source.offered(command.transition_latest));
    require(receiver.observe(scope(),command,1001)==CommandReceive::accepted);
    // New held-state snapshots do not renew the arrival of a queued old press.
    source_frame(source,input(attack),500);command=latest(source);
    require(receiver.observe(scope(),command,1500)==CommandReceive::accepted);
    auto out=delivery(receiver,1502);require(out.transition_count==0&&out.state.input.held==attack&&!out.baseline);
    require(!receiver.take(1502));
    // A source-old pulse in a fresh coalesced frame is discarded too.
    PlayerCommandCapture aged(7);PlayerCommandReceiver checked;baseline(aged,checked);
    source_frame(aged,input(attack),1);source_frame(aged,{},2);source_frame(aged,{},1000);
    require(checked.observe(scope(),latest(aged),1100)==CommandReceive::accepted);
    out=delivery(checked,1100);require(out.transition_count==0&&out.state.input.held==0);
}
void invalid_values(){
    const auto nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    require(valid(PlayerInput{}));
    for(const auto value:{nan,inf,-inf,-.1f,1.1f}){auto bad=input();bad.movement.intensity=value;require(!valid(bad));}
    for(const auto value:{nan,inf,-inf,-1.1f,1.1f}){auto bad=input();bad.movement={{value,0},1};require(!valid(bad));}
    auto bad=input();bad.movement={{1,0},0};require(!valid(bad));bad.movement={{0,0},1};require(!valid(bad));
    bad.movement={{.5f,.5f},1};require(!valid(bad));bad.movement={{1,0},.5f};require(valid(bad));
    bad=input(1u<<6);require(!valid(bad));require(!valid(static_cast<CommandButton>(0))&&!valid(static_cast<CommandButton>(3))&&!valid(static_cast<CommandButton>(1u<<6)));
    require(!valid(input(button_mask(CommandButton::guard)),button_mask(CommandButton::attack)));
    PlayerCommandCapture source(7);source_frame(source,input(button_mask(CommandButton::attack)),10);source_frame(source,{},11);
    const auto good=latest(source);
    for(unsigned defect=0;defect<12;++defect){
        auto wrong=good;
        switch(defect){
        case 0:wrong.input_generation=0;break;case 1:wrong.frame_sequence=0;break;
        case 2:wrong.transition_count=command_history_capacity+1;break;
        case 3:++wrong.transition_latest;break;case 4:wrong.transitions[0].serial=0;break;
        case 5:wrong.transitions[0].button=static_cast<CommandButton>(3);break;
        case 6:wrong.transitions[1].source_ms=9;break;case 7:wrong.transitions[1].source_ms=12;break;
        case 8:wrong.transitions[2]=wrong.transitions[0];break;case 9:wrong.input.held=button_mask(CommandButton::attack);break;
        case 10:wrong.transitions[1].pressed=true;break;case 11:wrong.input.held=button_mask(CommandButton::guard);break;
        }
        require(!valid(wrong));
    }
    PlayerCommandCapture zero(0),mask(7,1u<<7);require(zero.fault()==CommandFault::invalid&&mask.fault()==CommandFault::invalid);
    PlayerCommandReceiver rejected_mask(1u<<7);require(rejected_mask.fault()==CommandFault::invalid&&!rejected_mask.active());
    require(delivery(rejected_mask,0).neutralize&&!rejected_mask.take(0));
    for(unsigned field=0;field<4;++field){auto invalid=scope();if(field==0)invalid.session={};if(field==1)invalid.connection={};if(field==2)invalid.player=0;if(field==3)invalid.input_generation=0;require(!valid(invalid));}
    PlayerCommandCapture limited(7,button_mask(CommandButton::attack));require(!limited.capture(input(button_mask(CommandButton::guard)),0)&&limited.fault()==CommandFault::invalid);
    PlayerCommandCapture clock(7);source_frame(clock,{},5);require(!clock.capture({},4)&&clock.fault()==CommandFault::clock_regressed&&!clock.latest());
    PlayerCommandCapture handoff(7);source_frame(handoff,input(button_mask(CommandButton::attack)),1);
    require(!handoff.offered(2)&&handoff.fault()==CommandFault::invalid);
    PlayerCommandCapture regressed(7);source_frame(regressed,input(button_mask(CommandButton::attack)),1);require(regressed.offered(1));require(!regressed.offered(0));
}
void lifetime_and_mutation(){
    for(unsigned defect=0;defect<9;++defect){
        PlayerCommandCapture source(7);PlayerCommandReceiver receiver;baseline(source,receiver);
        source_frame(source,input(button_mask(CommandButton::attack)),10);auto command=latest(source);auto incoming=scope();std::uint64_t arrival=1010;
        auto expected=CommandFault::invalid;
        switch(defect){
        case 0:incoming.session[0]++;expected=CommandFault::scope_changed;break;
        case 1:incoming.connection[0]++;expected=CommandFault::scope_changed;break;
        case 2:incoming.player++;expected=CommandFault::scope_changed;break;
        case 3:incoming.input_generation++;command.input_generation++;expected=CommandFault::scope_changed;break;
        case 4:command.input_generation++;break;case 5:incoming.player=0;break;
        case 6:arrival=999;expected=CommandFault::clock_regressed;break;
        case 7:command.frame_sequence=1;command.input={};command.transition_latest=0;command.transition_count=0;command.transitions={};break;
        case 8:incoming.session={};break;
        }
        require(receiver.observe(incoming,command,arrival)==CommandReceive::failed&&receiver.fault()==expected&&!receiver.active());
        require(delivery(receiver,1100).neutralize&&!receiver.take(1100));
        require(receiver.observe(scope(),latest(source),2000)==CommandReceive::failed&&receiver.fault()==expected);
    }
    // Retained overlaps and complete-suffix held state stay immutable after an expiry.
    const auto guard=button_mask(CommandButton::guard),attack=button_mask(CommandButton::attack);
    PlayerCommandCapture source(7);source_frame(source,input(guard),0);require(source.offered(1));
    for(std::uint64_t i=1;i<=20;++i){source_frame(source,input(guard|(i%2?attack:0)),i);require(source.offered(latest(source).transition_latest));}
    const auto old=latest(source);require(old.transitions[0].button==CommandButton::attack);
    for(bool overlap:{false,true}){
        PlayerCommandReceiver receiver;require(receiver.observe(scope(),old,1000)==CommandReceive::accepted);delivery(receiver,1000);
        auto forged=old;++forged.frame_sequence;forged.source_ms=21;
        if(overlap)forged.transitions[0].source_ms=0;else forged.input.held&=~guard;
        require(valid(forged));require(receiver.observe(scope(),forged,2000)==CommandReceive::failed&&receiver.fault()==CommandFault::invalid);
    }
    PlayerCommandReceiver clock;PlayerCommandCapture clean(7);baseline(clean,clock);require(!clock.take(1100));
    require(delivery(clock,1099).neutralize&&clock.fault()==CommandFault::clock_regressed);
    PlayerCommandCapture timed(7);PlayerCommandReceiver source_clock;source_frame(timed,{},100);auto old_time=latest(timed);
    require(source_clock.observe(scope(),old_time,1000)==CommandReceive::accepted);delivery(source_clock,1000);
    auto earlier=old_time;++earlier.frame_sequence;earlier.source_ms=99;require(valid(earlier));
    require(source_clock.observe(scope(),earlier,1001)==CommandReceive::failed&&source_clock.fault()==CommandFault::clock_regressed);
}
void first_fault_and_frame_lag(){
    PlayerCommandReceiver first;PlayerCommand bad;
    require(first.observe(scope(),bad,1000)==CommandReceive::failed&&first.fault()==CommandFault::invalid);
    const auto neutral=delivery(first,1000);require(neutral.neutralize&&neutral.transition_count==0&&neutral.state.input==PlayerInput{});
    require(!first.take(999)&&first.fault()==CommandFault::invalid); // Preserve the first fault.
    require(first.observe(scope(),bad,2000)==CommandReceive::failed&&!first.take(2000));

    // Simulated consumer deliberately runs one source frame behind emission.
    // This checks the portable hand-off only, not a native input binding.
    PlayerCommandCapture source(7);PlayerCommandReceiver receiver;baseline(source,receiver);
    std::optional<CommandDelivery> pending;
    PlayerInput consumed;
    std::uint64_t consumed_sequence{},attack_presses{},attack_releases{},guard_presses{},guard_releases{};
    const auto consume=[&]{
        if(!pending)return;
        require(!pending->neutralize&&!pending->baseline);
        require(pending->state.frame_sequence>consumed_sequence);
        consumed_sequence=pending->state.frame_sequence;consumed=pending->state.input;
        for(std::size_t i=0;i<pending->transition_count;++i){
            const auto& edge=pending->transitions[i];
            if(edge.button==CommandButton::attack){if(edge.pressed)++attack_presses;else ++attack_releases;}
            if(edge.button==CommandButton::guard){if(edge.pressed)++guard_presses;else ++guard_releases;}
        }
        pending.reset();
    };
    PlayerInput previous;
    for(std::uint64_t frame=1;frame<=240;++frame){
        consume();require(consumed==previous);
        auto current=input();
        if(frame>=30&&frame<42)current.held=button_mask(CommandButton::attack);
        if(frame>=90&&frame<210)current.held=button_mask(CommandButton::guard);
        if(frame>=60&&frame<90)current.movement={{0,1},.35f};
        source_frame(source,current,frame*16);const auto command=latest(source);require(source.offered(command.transition_latest));
        require(receiver.observe(scope(),command,1000+frame*16)==CommandReceive::accepted);
        pending=delivery(receiver,1000+frame*16);
        require(pending->state.frame_sequence==command.frame_sequence&&consumed_sequence<command.frame_sequence);
        require(pending->state.input==current&&!receiver.take(1000+frame*16));
        // Held attack/guard does not manufacture another press every frame.
        require(attack_presses<=1&&guard_presses<=1);
        previous=current;
    }
    consume();require(consumed==PlayerInput{}&&receiver.fault()==CommandFault::none);
    require(attack_presses==1&&attack_releases==1&&guard_presses==1&&guard_releases==1);
}
void bounds_and_seeded_sequences(){
    const auto attack=button_mask(CommandButton::attack);
    PlayerCommandCapture blocked(7);
    for(std::uint64_t i=1;i<=command_history_capacity;++i)source_frame(blocked,input(i%2?attack:0),i);
    require(!blocked.capture(input(attack),17)&&blocked.fault()==CommandFault::overflow&&!blocked.latest());
    PlayerCommandCapture flood(7);PlayerCommandReceiver receiver;baseline(flood,receiver);
    for(std::uint64_t i=1;i<=command_queue_capacity;++i){source_frame(flood,input(i%2?attack:0),i);const auto command=latest(flood);require(flood.offered(command.transition_latest));require(receiver.observe(scope(),command,1000+i)==CommandReceive::accepted);}
    require(receiver.pending()==command_queue_capacity);source_frame(flood,input(attack),65);const auto extra=latest(flood);
    require(receiver.observe(scope(),extra,1065)==CommandReceive::failed&&receiver.fault()==CommandFault::overflow);
    require(delivery(receiver,1065).neutralize&&!receiver.take(1065));
    std::uint32_t seed=0x684e173b;PlayerCommandCapture source(7);PlayerCommandReceiver checked;baseline(source,checked);
    ButtonMask held{};std::vector<ButtonTransition> expected,seen;std::uint64_t offered_serial{};
    const auto stale=latest(source);
    for(std::uint64_t frame=1;frame<=512;++frame){
        seed=seed*1664525u+1013904223u;held^=1u<<(seed%6);
        auto current=input(held);if(seed&8u)current.movement={{(seed&16u)?1.f:-1.f,0},.75f};
        source_frame(source,current,frame*4);const auto command=latest(source);expected.push_back(command.transitions[command.transition_count-1]);
        // One lost offer plus duplicates/reordering remain within retained history.
        if(frame%4!=0||(frame%12==0&&frame!=512))continue;
        require(checked.observe(scope(),command,1000+frame*4)==CommandReceive::accepted);require(source.offered(command.transition_latest));
        const auto out=delivery(checked,1000+frame*4);require(out.state.input==current&&!out.baseline);
        for(std::size_t i=0;i<out.transition_count;++i){require(out.transitions[i].serial>offered_serial);seen.push_back(out.transitions[i]);}
        offered_serial=command.transition_latest;
        require(checked.observe(scope(),command,1001+frame*4)==CommandReceive::stale&&!checked.take(1001+frame*4));
        require(checked.observe(scope(),stale,1001+frame*4)==CommandReceive::stale);
    }
    require(seen==expected&&seen.size()==512&&checked.fault()==CommandFault::none);
}
}
int main(){try{rapid_edges_and_duplicates();held_pause_and_expiry();boundary_and_pending_expiry();invalid_values();lifetime_and_mutation();first_fault_and_frame_lag();bounds_and_seeded_sequences();
    std::cout<<"PASS upstream command intent capture, exact-once edges, held metadata, expiry/pause baselines, lifetime/value guards and seeded loss/replay\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
