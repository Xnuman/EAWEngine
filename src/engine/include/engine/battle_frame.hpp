#pragma once

// How a running battle becomes a frame: each tick's events (deaths, impacts, sounds) played by its
// presenters, then the frame's instances, bone palette, animation cues, and attached effects from
// the interpolated snapshots. Presentation only: nothing here feeds back into the battle. The
// caller hands the frame to the renderer (palette before instances: set_instances checks bone
// ranges against it).

#include "math/matrix.hpp"
#include "render/visual_scene.hpp"
#include "simulation/simulation.hpp"
#include "engine/battle_audio.hpp"
#include "engine/battle_effects.hpp"
#include "engine/battle_runtime.hpp"
#include "engine/scene_presentation.hpp"
#include "engine/unit_animation.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace eaw {

// What presents a battle: its scene templates, and optionally unit animation and sound.
struct BattlePresenters
{
    ScenePresentation& presentation;
    UnitAnimator* animator{};
    BattleEffects& effects;
    BattleAudio* audio{};
};

// A frame of a battle: drawn instances (backgrounds, entities, then shots drawn as models), the
// entity of each (invalid for backgrounds and shots), how each entity's clips play, the serials of
// the shots, and the bone palette (filled only with an animator).
struct BattleFrame
{
    std::vector<VisualInstance> instances;
    std::vector<EntityId> entities;
    std::vector<AnimationProfile> animations;
    std::vector<std::uint32_t> projectile_serials;
    std::vector<Matrix4> palette;
};

// Plays the last tick's events: the scripts' sound requests, corpses of the dead (from
// `world_seconds` on the world clock), impact and death effects, and battle sounds. Call after
// every tick, so none is lost when a frame runs several.
void present_tick(const BattleRuntime& runtime, BattlePresenters& presenters, double world_seconds);

// Builds `frame` from the world interpolated between `previous` (the snapshot before the last
// tick) and the current one, with `selected` entity keys highlighted; the palette holds at most
// `max_palette` matrices.
void build_battle_frame(const BattleRuntime& runtime, const SimSnapshot& previous, const std::unordered_set<std::uint64_t>& selected, double world_seconds, std::size_t max_palette,
                        BattlePresenters& presenters, BattleFrame& frame);

} // namespace eaw
