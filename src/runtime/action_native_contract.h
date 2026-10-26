#pragma once
#include "skcoop/lan/actions.hpp"
#include <array>
#include <string_view>

namespace skcoop::actors {
// Names and IDs were independently resolved in the supported source/guest
// assets. Never accept native IDs, pointers or arbitrary names from a peer.
struct ActionBinding {
    lan::ActionKind kind;
    const char* source_name;
    int source_id;
    const char* guest_name;
    int guest_id;
};
inline constexpr std::array action_bindings{
    ActionBinding{lan::ActionKind::stand_move_start,"W_StandMoveStart",2977,"W_StandMoveStart",2977},
    ActionBinding{lan::ActionKind::stand_run_stop,"W_StandRunStop",2978,"W_StandRunStop",2978},
    ActionBinding{lan::ActionKind::stand_walk_stop,"W_StandWalkStop",2979,"W_StandWalkStop",2979},
    ActionBinding{lan::ActionKind::ground_step,"W_GroundStep",3865,"W_GroundStep",3865},
    ActionBinding{lan::ActionKind::sprint_start_from_step,"W_SprintStartFromStep",3966,"W_SprintStartFromStep",3966},
    ActionBinding{lan::ActionKind::sprint_stop_ready,"W_SprintStopReady",4138,"W_SprintStopReady",4138},
    ActionBinding{lan::ActionKind::sprint_stop_quick_turn_front,"W_SprintStopQuickTurnFront",4133,"W_SprintStopQuickTurnFront",4133},
    // The actor-local Start trial produced physical rise/return. Queueing Ready
    // alone did not establish a jump; normalize the observed source preparation.
    ActionBinding{lan::ActionKind::ground_jump_ready,"W_GroundJumpReady",988,"W_GroundJumpStart",989},
    ActionBinding{lan::ActionKind::ground_jump_land_ready,"W_GroundJumpLandReady",4185,"W_GroundJumpLandReady",4185},
    ActionBinding{lan::ActionKind::land_ground_jump,"W_LandGroundJump",1223,"W_LandGroundJump",1223},
};
inline const ActionBinding* action_binding(std::string_view name) noexcept {
    for (const auto& binding : action_bindings) if (name == binding.source_name) return &binding;
    return nullptr;
}
inline const ActionBinding* action_binding(lan::ActionKind kind) noexcept {
    for (const auto& binding : action_bindings) if (kind == binding.kind) return &binding;
    return nullptr;
}
// The observed queue wraps a tail equal to capacity. Refuse growth and every
// out-of-range index before entering the game's allocator/write helper.
inline bool valid_event_queue(int capacity,int tail,int count) noexcept {
    return capacity>=1&&capacity<=4096&&tail>=0&&tail<=capacity&&count>=0&&count<capacity;
}
}
