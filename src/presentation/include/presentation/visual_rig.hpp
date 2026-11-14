#pragma once

// A mesh's skeleton for animation (VisualRig) with its clips, attached effects, and palettes: what
// scene building fills and unit animation and effects read.

#include "math/matrix.hpp"
#include "math/vector.hpp"
#include "assets/animation.hpp"
#include "assets/clip_names.hpp"
#include "assets/pose.hpp"
#include "assets/skeleton.hpp"
#include "presentation/particle_effects.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace eaw {

// How a type's clips play (presentation only). The move and walk clips play at their own rate when
// the unit goes at a reference speed, given as a fraction of its top speed: XML
// Movement_Animation_Speed and Walk_Animation_Speed are those speeds in Max_Speed's units (measured:
// the move clip's planted feet move over the ground at Movement_Animation_Speed, the AT-AT's at
// 0.137 per frame against its 0.1366, the scaled stormtrooper's and Bantha's alike; EAWAssetInspector
// gait; Walk_Animation_Speed is about 0.4 of it), so playback scales by speed over reference. The idle clip plays at Idle_Anim_00_Rate_Mod times its rate (props: 8 spins the huge
// asteroid masses); a type without a move or walk clip keeps its idle clip while moving when
// Allow_Idle_When_Moving (birds) instead of standing in its rest pose.
struct AnimationProfile
{
    float move_reference{1.0F};
    // 0: the engine's walk speed.
    float walk_reference{};
    float idle_rate{1.0F};
    // Loops IDLE_00 (XML Loop_Idle_Anim_00); otherwise each idle that ends is followed by a random
    // IDLE variant, and a lone IDLE clip plays again after a 2-4 s pause (the owner's recollection
    // and choice, 2026-11-13; engine choices until observed).
    bool loop_first_idle{};
    bool idle_when_moving{};
    // Transition clips: before and after turning in place (XML Has_Pre_Turn_Anim) and when starting
    // and stopping a walk (XML Walk_Transition).
    bool pre_turn{};
    bool walk_transition{};
    // The turn rate (radians per second) at which the turn clips play at their own rate (XML
    // Rotation_Animation_Speed in degrees per frame, read like Movement_Animation_Speed by analogy;
    // 0: they play at their own rate). Walk transitions play at the move clip's rate at top speed.
    float turn_reference{};
    // XML Locomotor_Has_Animation_Priority: the simulation winds the walker's speed and its turns in
    // place up and down over its clips (battle's apply_animation_locomotion), so its transitions follow
    // the simulation's flags (EntitySnapshot::arriving, turning_in_place, turn_arriving), and its turn
    // clips play at the rate for `in_place_turn_rate` (radians per second: XML Stopped_Rate_Of_Turn,
    // else Max_Rate_Of_Turn).
    bool animation_led{};
    float in_place_turn_rate{};
    // XML Attack_Animation_Is_Overlay: ATTACK and ATTACKIDLE play over the locomotion on the bones
    // they animate only (the AT-AT's guns), instead of taking the whole body.
    bool attack_overlay{};
    // A turret (XML Turret_Bone_Name, Barrel_Bone_Name: the bones' names as game_object_name_crc, 0
    // none): the turret bone's subtree yaws about the model's up axis to the simulation's turret
    // direction (EntitySnapshot::turret_yaw, which also decides when it fires), and the barrel's
    // pitches toward the target at `turret_rate` radians per second (Turret_Rotate_Speed per 30 Hz
    // frame, a guess like Max_Rate_Of_Turn; 5 without it), within half of
    // Turret_Elevate_Extent_Degrees each way (a guess).
    std::uint32_t turret_bone{};
    std::uint32_t barrel_bone{};
    float turret_rate{};
    float turret_pitch_limit{1.04719755F};
};

// A turn of a bone's subtree in model space about the bone's posed position (turret aiming).
struct BoneTurn
{
    std::uint32_t bone{};
    Vector3 axis;
    float radians{};
};

struct VisualClip
{
    AnimationRole role{};
    std::uint32_t variant{};
    std::string path;
    std::shared_ptr<const Animation> animation;
};

// A particle effect attached to a model bone (an ALO proxy naming a particle system). The effect's
// texture indices point into the build's particle texture list.
struct VisualEffectAttachment
{
    std::shared_ptr<const ParticleEffect> effect;
    std::uint32_t bone{};
    std::string proxy;
    // A hardpoint's damage effect (the proxy hangs under the bone its XML Damage_Particles names):
    // that bone, shown once the hardpoint is destroyed (its bone is hidden while intact), else -1.
    std::int32_t damage_bone{-1};
    // The mesh emit-from-mesh emitters spawn on (the mesh on the proxy bone's parent, in the proxy
    // bone's space), when the effect has such emitters and the model has that mesh.
    std::shared_ptr<const ParticleEmitMesh> emit_mesh;
    // An engine effect (named pe_*, or under a hardpoint's Engine_Particles bone): it glows with the
    // unit's speed.
    bool engine{};
    // The damage state it belongs to (an _ALT<n> proxy: shown only while the instance draws that
    // state, VisualInstance::state); none for an effect shown in every state.
    std::optional<std::uint32_t> state;
};

// A map mesh's skeleton for animation: the model, the inverse of its bind pose (empty when the bind
// pose cannot be inverted; the mesh then stays in its rest pose), where its rest palette starts in
// the scene palette, and its clips sorted by role and variant. A rest palette is the mesh's bind
// pose: identity skin matrices, then the visibility half with attachments of bones hidden in the
// bind pose collapsed (see sample_skin_palette).
struct VisualRig
{
    std::shared_ptr<const Skeleton> skeleton;
    std::vector<Matrix4> inverse_bind_pose;
    std::uint32_t rest_offset{};
    // Palette entries the mesh uses (its rest palette's size).
    std::uint32_t palette_size{};
    std::vector<VisualClip> clips;
    // Particle effects on the model's visible proxies.
    std::vector<VisualEffectAttachment> effects;
    // Per bone: hidden while the entity is intact (a hardpoint's Damage_Decal and Damage_Particles
    // bones and their children: scorch meshes and damage effects that appear once that hardpoint is
    // destroyed). Applied to the palette's visibility half; empty when nothing is hidden.
    std::vector<bool> intact_hidden;
};

// A skin matrix that collapses every vertex of a hidden bone onto the origin, so it rasterizes
// nothing.
constexpr Matrix4 collapsed_bone{std::array<float, 16>{0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F}};

// Skin matrices of `pose` (and the visibility half) appended to `out`; `hidden` bones collapse too.
void append_pose_palette(const Skeleton& skeleton, std::span<const Matrix4> inverse_bind_pose, std::span<const PoseBone> pose, bool visibility_palette, std::vector<Matrix4>& out,
                         const std::vector<bool>& hidden = {});
// A rig's rest palette: identity skin matrices, then the visibility half from the bind pose.
void append_rest_palette(const Skeleton& skeleton, std::vector<Matrix4>& out, const std::vector<bool>& hidden = {});
// Appends a rig's palette at `seconds` of `animation` (rig.palette_size entries) to `out`. Without
// an inverse bind pose it appends the rest pose.
void append_rig_palette(const VisualRig& rig, const Animation& animation, float seconds, std::vector<Matrix4>& out);
// Appends a rig's palette for a local pose (a blend of clips; see blend_pose), its bones turned by
// `turns` in order (each bone's subtree about the bone's posed position).
// `hidden`, when given, replaces the rig's intact_hidden (an entity with destroyed hardpoints).
void append_rig_local_palette(const VisualRig& rig, std::span<const LocalBone> pose, std::vector<Matrix4>& out, std::span<const BoneTurn> turns = {}, const std::vector<bool>* hidden = nullptr);

} // namespace eaw
