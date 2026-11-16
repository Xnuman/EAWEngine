#pragma once

// The roles animation clips play, found by the stock naming convention <MODEL>_<ROLE>_<NN>.ALA
// beside the model: the one place that knows the clip names, for the scene that finds a model's
// clips, the battle that reads their lengths, and the animation that plays them.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace eaw {

// What a unit is doing, as far as choosing its animation goes. Clips are found by the stock naming
// convention <MODEL>_<ROLE>_<NN>.ALA beside the model (IDLE 539 stock files, MOVE 158, WALKMOVE 53,
// ATTACK 160, ATTACKIDLE 108, DIE 301); NN numbers variants. Other roles (CROUCHIDLE, FW_DIE, ...)
// are not used yet.
enum class AnimationRole : std::uint8_t
{
    idle,
    move,
    walk_move,
    attack,
    attack_idle,
    die,
    // A death by being run over (CRUSHED_nn clips; a Damage_Crush death clone's
    // Specific_Death_Anim_Type Crushed).
    crushed,
    // Walkers' transitions: turning in place to the left and right (TURNL_nn, TURNR_nn loops), the
    // clips before and after such a turn (TURNL_BEGIN, TURNL_END, ...), starting to walk
    // (MOVESTART), and stopping (MOVE_ENDONE..MOVE_ENDFOUR: variants 0..3, by the stride's part).
    turn_left,
    turn_right,
    turn_left_begin,
    turn_left_end,
    turn_right_begin,
    turn_right_end,
    move_start,
    move_end,
    // The shorter turn cycles between BEGIN and END (TURNL_HALF, TURNL_QUARTER, ...): they start and
    // end where the loop does (docs/compatibility-research.md, "The turn kit").
    turn_left_half,
    turn_left_quarter,
    turn_right_half,
    turn_right_quarter,
    // Raising and lowering a leg the walk and turn clips keep raised (IDLE_LEGLIFT, IDLE_LEGDROP:
    // the pod walker's middle leg; docs/compatibility-research.md, "The pod walker's raised leg").
    idle_leg_lift,
    idle_leg_drop,
};

// The role a clip file name (without directory and extension) plays for `model_stem`, and its
// variant number; nullopt for other files and roles. Case-insensitive.
struct ClipName
{
    AnimationRole role{};
    std::uint32_t variant{};
};
[[nodiscard]] std::optional<ClipName> parse_clip_name(std::string_view file_stem, std::string_view model_stem);

// The file stem (without directory and extension) of `model_stem`'s clip for `role` and `variant`
// by the stock naming, upper case: EV_AT-AT_MOVE_ENDTWO_00 for move_end variant 1 (the stopping
// clips carry their variant in the name), EI_TROOPER_DIE_01 for die variant 1. Empty for a
// move_end variant past the four stock names.
[[nodiscard]] std::string clip_file_stem(std::string_view model_stem, AnimationRole role, std::uint32_t variant);

} // namespace eaw
