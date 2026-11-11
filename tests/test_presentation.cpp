// Presentation of a running battle: posing entities, unit animation, cameras, picking, and debug drawing.
#include "test_support.hpp"

#include "battle.hpp"
#include "battle_effects.hpp"
#include "battle_log.hpp"
#include "particle_effects.hpp"
#include "projectile_visuals.hpp"
#include "scene_builder_internal.hpp"
#include "sandbox_options.hpp"
#include "unit_animation.hpp"
#include "formats/alo_model_helpers.hpp"

namespace {

using namespace eaw_test;

void test_scene_presentation()
{
    // pose_transform keeps scale and tilt, turns about +Z by the heading change, and moves.
    const eaw::Matrix4 base{{2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 5, 6, 7, 1}};
    const auto same = eaw::pose_transform(base, 0.0F, {1.0F, 2.0F, 3.0F}, 0.0F);
    require(same.values[0] == 2.0F && same.values[5] == 2.0F && same.values[12] == 1.0F && same.values[13] == 2.0F && same.values[14] == 3.0F, "an unchanged heading must only move the transform");
    const auto turned = eaw::pose_transform(base, 0.0F, {0.0F, 0.0F, 0.0F}, std::numbers::pi_v<float> / 2.0F);
    require(std::fabs(turned.values[0]) < 1.0e-5F && std::fabs(turned.values[1] - 2.0F) < 1.0e-5F && std::fabs(turned.values[4] + 2.0F) < 1.0e-5F && turned.values[10] == 2.0F,
            "a quarter turn must rotate the model X axis onto +Y and keep its scale");

    // The presentation spawns entities from scene instances, keeps backgrounds static, and maps
    // instances back to entities.
    eaw::VisualScene scene;
    scene.meshes.resize(2);
    scene.meshes[0].name = "UNIT.ALO";
    scene.meshes[1].name = "SKY.ALO";
    eaw::VisualInstance sky{1};
    sky.background = true;
    eaw::VisualInstance unit{0};
    unit.transform.values[12] = 10.0F;
    scene.instances = {sky, unit};
    eaw::EntitySpawn spawn;
    spawn.type_name = "Test_Unit";
    spawn.position = {10.0F, 0.0F, 0.0F};
    spawn.max_speed = 10.0F;
    // The second entity's map data is not finite: it is skipped with a warning, the rest still spawn.
    eaw::EntitySpawn broken = spawn;
    broken.type_name = "Broken_Unit";
    broken.position.x = std::numeric_limits<float>::infinity();
    const std::array<eaw::MapEntitySpawn, 2> entities{{{spawn, 1}, {broken, 1}}};
    eaw::SpawnableType spawnable;
    spawnable.type_name = "Spawned_Unit";
    spawnable.mesh = 0;
    spawnable.team_color = {0.25F, 0.5F, 1.0F, 1.0F};
    spawnable.scale = 0.5F;
    eaw::BattleMap map;
    map.entities = entities;
    map.spawnable = std::span(&spawnable, 1);
    eaw::Battle battle(map, "TEST.TED", 0.1);
    auto& world = battle.world();
    const eaw::ScenePresentation presentation(scene, entities, battle.map_entity_ids(), battle.spawnable());
    require(world.snapshot().entities.size() == 1 && battle.map_entity_ids()[0] && !battle.map_entity_ids()[1], "a battle must spawn the valid map entity and skip the broken one");
    const auto id = world.snapshot().entities[0].id;
    world.submit({eaw::SimCommandKind::move, {id}, {10.0F, 5.0F, 0.0F}});
    world.step();
    std::vector<eaw::VisualInstance> instances;
    std::vector<eaw::EntityId> ids;
    presentation.build(world.snapshot().entities, {eaw::entity_key(id)}, instances, ids);
    require(instances.size() == 2 && instances[0].background && !ids[0].valid() && ids[1] == id && instances[1].highlighted && instances[1].mesh == 0,
            "presentation must list backgrounds first, then entities with their selection state");
    require(std::fabs(instances[1].transform.values[13] - 1.0F) < 1.0e-5F && std::fabs(instances[1].transform.values[1] - 1.0F) < 1.0e-5F,
            "an entity's instance must follow its simulated position and heading");
    require(world.type_name(id) == "Test_Unit" && presentation.mesh_name(0) == "UNIT.ALO", "entities must be named by type and mesh");
    // An entity spawned by order is drawn from its type's template without a recorded id.
    expect_ok(battle.submit({.order = eaw::BattleOrder::spawn, .point = {-20.0F, 0.0F, 0.0F}, .type = "Spawned_Unit", .count = 1, .owner = 2}), "spawn order");
    battle.step();
    presentation.build(world.snapshot().entities, {}, instances, ids);
    require(instances.size() == 3 && instances[2].mesh == 0 && instances[2].team_color[2] == 1.0F && std::fabs(instances[2].transform.values[12] + 20.0F) < 1.0e-4F,
            "an entity spawned by order must be drawn from its type's template");
    const auto& m = instances[2].transform;
    require(std::fabs(std::sqrt(m.values[0] * m.values[0] + m.values[1] * m.values[1] + m.values[2] * m.values[2]) - 0.5F) < 1.0e-5F && std::fabs(m.values[10] - 0.5F) < 1.0e-5F,
            "a spawned type must be drawn at its Scale_Factor");
}

// Unit animation: clip names map to roles, snapshots choose roles, entities share sampled poses,
// and the palette budget leaves the rest in their rest pose.
void test_unit_animation()
{
    const auto clip = [](const char* file) -> std::optional<eaw::ClipName>
    {
        return eaw::parse_clip_name(file, "ei_trooper");
    };
    require(clip("EI_TROOPER_IDLE_00") && clip("EI_TROOPER_IDLE_00")->role == eaw::AnimationRole::idle && clip("EI_TROOPER_IDLE_00")->variant == 0, "IDLE_00 must be an idle clip");
    require(clip("EI_TROOPER_WALKMOVE_12") && clip("EI_TROOPER_WALKMOVE_12")->role == eaw::AnimationRole::walk_move && clip("EI_TROOPER_WALKMOVE_12")->variant == 12,
            "WALKMOVE_12 must be walk-move variant 12");
    require(clip("ei_trooper_attackidle_01") && clip("ei_trooper_attackidle_01")->role == eaw::AnimationRole::attack_idle, "clip names must be case-insensitive");
    require(!clip("OTHER_IDLE_00") && !clip("EI_TROOPER_IDLE") && !clip("EI_TROOPER_IDLE_0X") && !clip("EI_TROOPER_ATTACKFLINCHL_00"), "other roles, models, and malformed names must not be clips");

    eaw::EntitySnapshot standing;
    standing.id = {1, 1};
    eaw::EntitySnapshot walking = standing;
    walking.moving = true;
    walking.target = {9, 1};
    eaw::EntitySnapshot shooting = standing;
    shooting.target = {9, 1};
    require(eaw::wanted_animation_role(standing) == eaw::AnimationRole::idle && eaw::wanted_animation_role(walking) == eaw::AnimationRole::move &&
                eaw::wanted_animation_role(shooting) == eaw::AnimationRole::attack,
            "snapshots must ask for idle, move (even with a target), and attack");

    // Mesh 0: a one-bone rig with idle and move clips; mesh 1: no clips.
    const auto model = std::make_shared<const eaw::Skeleton>(eaw::load_skeleton(expect_ok(eaw::read_alo<eaw::AloModelData>(make_test_alo()), "ALO parse")));
    const auto animation = std::make_shared<const eaw::Animation>(eaw::load_animation(expect_ok(eaw::read_ala(make_test_ala()), "ALA parse")));
    eaw::VisualRig rig;
    rig.skeleton = model;
    for (const auto& transform : eaw::skeleton_bind_pose(*model))
    {
        const auto inverted = eaw::inverse(transform);
        require(inverted.has_value(), "the test bind pose must be invertible");
        rig.inverse_bind_pose.push_back(*inverted);
    }
    rig.palette_size = 2;
    rig.clips = {{eaw::AnimationRole::idle, 0, "IDLE_00", animation}, {eaw::AnimationRole::move, 0, "MOVE_00", animation}, {eaw::AnimationRole::attack, 0, "ATTACK_00", animation}};
    eaw::VisualRig still;
    still.rest_offset = 2;
    still.palette_size = 1;
    const std::array<eaw::VisualRig, 2> rigs{rig, still};
    const std::vector<eaw::Matrix4> rest(3, eaw::Matrix4::identity());
    eaw::UnitAnimator animator(rigs, rest);

    // 100 standing units, one walking, one clipless unit, and a background.
    std::vector<eaw::EntitySnapshot> snapshot;
    std::vector<eaw::EntityId> entities{eaw::EntityId{}};
    std::vector<eaw::VisualInstance> instances(1);
    instances[0].mesh = 1;
    instances[0].background = true;
    for (std::uint32_t index = 0; index < 102; ++index)
    {
        eaw::EntitySnapshot entity;
        entity.id = {index + 10U, 1};
        entity.moving = index == 100;
        entity.max_speed = 10.0F;
        snapshot.push_back(entity);
        entities.push_back(entity.id);
        eaw::VisualInstance instance;
        instance.mesh = index == 101 ? 1U : 0U;
        instances.push_back(instance);
    }
    std::vector<eaw::Matrix4> palette;
    animator.animate(snapshot, entities, instances, 0.25, 1U << 20U, palette);
    const auto& stats = animator.stats();
    require(stats.animated == 101 && stats.over_budget == 0 && stats.poses <= 31 && stats.poses > 1 && stats.sampled == stats.poses, "standing units must share at most one pose per slot");
    require(palette.size() == rest.size() + stats.poses * 2U && stats.palette_entries == palette.size(), "the palette must hold the rest pose and each pose once");
    require(instances[0].bone_offset == 2 && instances[102].bone_offset == 2, "backgrounds and clipless meshes must stay in their rest pose");
    require(std::ranges::all_of(instances.begin() + 1, instances.begin() + 102,
                                [&](const eaw::VisualInstance& instance)
                                {
                                    return instance.bone_offset >= rest.size() && instance.bone_offset + 2U <= palette.size();
                                }),
            "animated units must point at a pose in the palette");
    animator.animate(snapshot, entities, instances, 0.25, 1U << 20U, palette);
    require(animator.stats().sampled == 0 && animator.stats().poses == stats.poses, "the same poses must come from the cache");
    // A palette with room for nothing beyond the rest pose leaves everyone at rest.
    animator.animate(snapshot, entities, instances, 0.5, rest.size(), palette);
    require(animator.stats().over_budget == 101 && palette.size() == rest.size() && instances[5].bone_offset == 0, "units beyond the palette budget must stay in their rest pose");

    // Cues: a still unit's IDLE (30 frames, 1 s) reported frame by frame passes every frame of each
    // loop once, wrapping at the loop's end; a unit at rest pose, a background, and a clipless mesh
    // report nothing.
    {
        eaw::UnitAnimator cue_animator(rigs, rest);
        std::vector<eaw::EntitySnapshot> one{snapshot.front()};
        std::vector<eaw::EntityId> ids{one.front().id};
        std::vector<eaw::VisualInstance> shown(1);
        int halfway = 0;
        double covered = 0.0;
        for (int step = 0; step <= 90; ++step)
        {
            auto frame_instances = shown;
            auto frame_ids = ids;
            cue_animator.animate(one, frame_ids, frame_instances, 1.0 + step / 30.0, 1U << 20U, palette);
            for (const auto& cue : cue_animator.cues())
            {
                require(cue.entity == one.front().id && cue.clip == "IDLE_00" && cue.from_frame >= -1.0 && cue.to_frame <= 30.0 && cue.to_frame > cue.from_frame, "cues must lie within the clip");
                covered += cue.to_frame - std::max(cue.from_frame, 0.0);
                halfway += cue.from_frame < 15.0 && 15.0 <= cue.to_frame ? 1 : 0;
            }
        }
        require(halfway == 3 && std::abs(covered - 90.0) < 1.5, "three seconds of a one-second loop must pass its middle three times and cover 90 frames");
    }

    // The locomotion scale: idle, then WALKMOVE at 40% of top speed, then MOVE; without WALKMOVE,
    // idle into MOVE by a quarter of top speed.
    const auto at = [](const float speed, const bool walk)
    {
        return eaw::locomotion_blend(speed, 10.0F, walk);
    };
    require(at(0.0F, true).lower == eaw::AnimationRole::idle && at(0.0F, true).blend == 0.0F && at(std::numeric_limits<float>::quiet_NaN(), true).blend == 0.0F,
            "a unit at rest (or with a broken speed) must idle");
    require(at(2.0F, true).upper == eaw::AnimationRole::walk_move && std::fabs(at(2.0F, true).blend - 0.5F) < 1.0e-6F, "half the walking speed must be halfway into WALKMOVE");
    require(at(7.0F, true).lower == eaw::AnimationRole::walk_move && at(7.0F, true).upper == eaw::AnimationRole::move && std::fabs(at(7.0F, true).blend - 0.5F) < 1.0e-6F,
            "halfway between walking and top speed must be halfway into MOVE");
    require(at(20.0F, true).blend == 1.0F && at(1.25F, false).upper == eaw::AnimationRole::move && std::fabs(at(1.25F, false).blend - 0.5F) < 1.0e-6F && at(5.0F, false).blend == 1.0F,
            "the blend must saturate and blend straight into MOVE without WALKMOVE");

    // A slow unit blends idle and move; a unit that starts moving out of an attack fades the attack
    // out over 0.2 s.
    eaw::UnitAnimator blender(rigs, rest);
    std::vector<eaw::EntitySnapshot> pair(2);
    pair[0].id = {1, 1};
    pair[0].max_speed = 10.0F;
    pair[0].speed = 1.0F;
    pair[0].moving = true;
    pair[1].id = {2, 1};
    pair[1].max_speed = 10.0F;
    pair[1].target = {1, 1};
    std::vector<eaw::EntityId> pair_ids{pair[0].id, pair[1].id};
    std::vector<eaw::VisualInstance> pair_instances(2);
    blender.animate(pair, pair_ids, pair_instances, 1.0, 1U << 20U, palette);
    require(blender.stats().animated == 2 && blender.stats().blending == 1, "a slow unit must blend idle and move, an attacking one play its attack alone");
    pair[1].target = {};
    pair[1].moving = true;
    pair[1].speed = 10.0F;
    blender.animate(pair, pair_ids, pair_instances, 1.1, 1U << 20U, palette);
    require(blender.stats().blending == 2, "a unit leaving its attack must fade it out while it starts to move");
    blender.animate(pair, pair_ids, pair_instances, 1.4, 1U << 20U, palette);
    blender.animate(pair, pair_ids, pair_instances, 2.0, 1U << 20U, palette);
    require(blender.stats().blending == 1, "the attack must have faded out and the fast unit settled into MOVE");

    // Corpses: a mesh with a DIE clip keeps its dead for the clip plus 4 s, drawn after the living
    // and not pickable; a mesh without one keeps nothing.
    auto dying = rig;
    dying.clips.push_back({eaw::AnimationRole::die, 0, "DIE_00", animation});
    const std::array<eaw::VisualRig, 2> death_rigs{dying, still};
    eaw::UnitAnimator mourner(death_rigs, rest);
    eaw::VisualInstance body;
    body.mesh = 0;
    eaw::VisualInstance prop;
    prop.mesh = 1;
    mourner.add_corpse(body, {3, 1}, 10.0);
    mourner.add_corpse(prop, {4, 1}, 10.0);
    std::vector<eaw::EntityId> none;
    std::vector<eaw::VisualInstance> drawn;
    mourner.animate({}, none, drawn, 10.5, 1U << 20U, palette);
    require(drawn.size() == 1 && none.size() == 1 && !none[0].valid() && mourner.stats().corpses == 1 && drawn[0].bone_offset >= rest.size(),
            "a corpse with a DIE clip must be drawn, posed, and unpickable; one without must not be kept");
    none.clear();
    drawn.clear();
    mourner.animate({}, none, drawn, 10.0 + 1.0 + 3.9, 1U << 20U, palette);
    require(drawn.size() == 1, "a corpse must hold its last frame for a while after its clip");
    none.clear();
    drawn.clear();
    mourner.animate({}, none, drawn, 10.0 + 1.0 + 4.1, 1U << 20U, palette);
    require(drawn.empty() && mourner.stats().corpses == 0, "a corpse must be removed after lingering");
    // A death clone's style: its DIE variant and persistence (30 s for the AT-ST's).
    mourner.add_corpse(body, {5, 1}, 100.0, eaw::DeathStyle{0U, 12.0F});
    none.clear();
    drawn.clear();
    mourner.animate({}, none, drawn, 111.0, 1U << 20U, palette);
    require(drawn.size() == 1, "a death clone's corpse must stay for its persistence");
    none.clear();
    drawn.clear();
    mourner.animate({}, none, drawn, 112.5, 1U << 20U, palette);
    require(drawn.empty(), "a death clone's corpse must go once its persistence ends");
    // Shots: a mesh with ATTACK and ATTACKIDLE aims with ATTACKIDLE, plays ATTACK from its start on
    // each shot, and fades back to aiming after ATTACK ends (the test clip lasts 1 s).
    auto shooter_rig = rig;
    shooter_rig.clips.push_back({eaw::AnimationRole::attack_idle, 0, "ATTACKIDLE_00", animation});
    std::ranges::sort(shooter_rig.clips, {}, &eaw::VisualClip::role);
    const std::array<eaw::VisualRig, 2> shooter_rigs{shooter_rig, still};
    eaw::UnitAnimator gunner(shooter_rigs, rest);
    std::vector<eaw::EntitySnapshot> aiming(1);
    aiming[0].id = {7, 1};
    aiming[0].target = {8, 1};
    std::vector<eaw::EntityId> aiming_ids{aiming[0].id};
    std::vector<eaw::VisualInstance> aiming_instances(1);
    double clock = 20.0;
    // Advances in 50 ms frames up to `at` (longer frames count as 0.25 s).
    const auto frame = [&](const double at)
    {
        do
        {
            clock = std::min(clock + 0.05, at);
            aiming_ids.resize(1);
            aiming_instances.resize(1);
            gunner.animate(aiming, aiming_ids, aiming_instances, clock, 1U << 20U, palette);
        } while (clock < at);
        return gunner.stats().blending;
    };
    require(frame(20.0) == 0, "a unit with a target must aim with ATTACKIDLE alone");
    aiming[0].last_shot_tick = 5;
    require(frame(20.1) == 0 && frame(20.6) == 0, "a shot must play ATTACK alone while it runs");
    require(frame(21.175) == 1, "ATTACK must fade back into aiming after it ends");
    require(frame(21.5) == 0, "the unit must aim again once ATTACK has faded");
    aiming[0].last_shot_tick = 9;
    require(frame(21.6) == 0, "the next shot must restart ATTACK");

    require(eaw::parse_clip_name("EI_TROOPER_DIE_01", "EI_TROOPER") && eaw::parse_clip_name("EI_TROOPER_DIE_01", "EI_TROOPER")->role == eaw::AnimationRole::die &&
                !eaw::parse_clip_name("EI_TROOPER_FW_DIE_00", "EI_TROOPER"),
            "DIE clips must be recognized, directional deaths not yet");
}

void test_model_forward_axis()
{
    // Alamo models face -Y: an unrotated model's heading is -pi/2, and posing one with heading 0
    // (travel along +X) turns its forward (-Y) axis onto +X.
    const auto identity = eaw::Matrix4::identity();
    require(std::fabs(eaw::model_forward_heading(identity) - eaw::identity_model_heading) < 1.0e-6F, "an unrotated model must face -Y");
    const auto posed = eaw::pose_transform(identity, eaw::identity_model_heading, {10.0F, 20.0F, 0.0F}, 0.0F);
    {
        // Pitch lifts the model's forward axis (-Y) toward +Z; a positive bank lowers its left side (+X).
        const auto apply = [](const eaw::Matrix4& m, const eaw::Vector3& v)
        {
            return eaw::transform_direction(v, eaw::Matrix4{m});
        };
        const auto near = [](const eaw::Vector3& a, const eaw::Vector3& b)
        {
            return eaw::length(a - b) < 1.0e-5F;
        };
        const auto climbing = eaw::pose_transform(identity, eaw::identity_model_heading, {}, 0.0F, std::numbers::pi_v<float> / 2.0F, 0.0F);
        require(near(apply(climbing, {0.0F, -1.0F, 0.0F}), {0.0F, 0.0F, 1.0F}), "pitch must lift the model's nose toward +Z");
        const auto banked = eaw::pose_transform(identity, eaw::identity_model_heading, {}, 0.0F, 0.0F, std::numbers::pi_v<float> / 2.0F);
        require(near(apply(banked, {1.0F, 0.0F, 0.0F}), {0.0F, 0.0F, -1.0F}) && near(apply(banked, {0.0F, -1.0F, 0.0F}), {1.0F, 0.0F, 0.0F}),
                "a positive bank must lower the model's left side about its nose");
        require(eaw::flight_bank(0.0F) == 0.0F && eaw::flight_bank(100.0F) > 1.0F && eaw::flight_bank(-100.0F) < -1.0F && eaw::flight_bank(std::numeric_limits<float>::quiet_NaN()) == 0.0F,
                "flight bank must follow the turn and stay bounded");
        require(std::fabs(eaw::flight_bank(100.0F, 0.1F) - 0.1F) < 1.0e-6F && eaw::flight_bank(100.0F, 0.0F) == 0.0F, "flight bank must stop at the type's limit");
    }
    require(std::fabs(-posed.values[4] - 1.0F) < 1.0e-5F && std::fabs(posed.values[5]) < 1.0e-5F && std::fabs(eaw::model_forward_heading(posed)) < 1.0e-5F && posed.values[12] == 10.0F,
            "a model moving along +X must face +X");
}

void test_camera_controllers()
{
    const auto near_equal = [](const float left, const float right, const float tolerance = 1.0e-3F)
    {
        return std::fabs(left - right) <= tolerance;
    };
    const auto distance = [](const eaw::VisualCamera& camera)
    {
        const auto x = camera.eye[0] - camera.target[0];
        const auto y = camera.eye[1] - camera.target[1];
        const auto z = camera.eye[2] - camera.target[2];
        return std::sqrt(x * x + y * y + z * z);
    };

    // Orbit: frames the bounds center at 2.6 bounding radii and clamps pitch.
    eaw::OrbitCamera orbit({{-1.0F, -2.0F, -2.0F}, {3.0F, 2.0F, 2.0F}});
    auto view = orbit.camera();
    require(near_equal(view.target[0], 1.0F) && near_equal(view.target[1], 0.0F) && near_equal(distance(view), 2.6F * std::sqrt(12.0F)), "orbit camera does not frame the bounds");
    orbit.apply({.pitch = 10.0F});
    view = orbit.camera();
    require(view.eye[2] - view.target[2] < distance(view) && view.eye[2] > view.target[2], "orbit pitch was not clamped below vertical");
    orbit.apply({.reset = true});
    require(near_equal(distance(orbit.camera()), 2.6F * std::sqrt(12.0F)), "orbit reset failed");

    // Tactical: centered on the map, looking north (+Y), zoom limited to the map size.
    eaw::TacticalCamera tactical({{-1000.0F, -500.0F, 0.0F}, {1000.0F, 500.0F, 0.0F}});
    view = tactical.camera();
    require(near_equal(view.target[0], 0.0F) && near_equal(view.target[1], 0.0F) && view.eye[1] < view.target[1] && view.eye[2] > 0.0F, "tactical camera did not start south of the map center");
    require(near_equal(tactical.distance(), 1500.0F) && view.far_plane > view.near_plane && view.near_plane > 0.0F, "tactical camera default distance or planes are wrong");
    tactical.apply({.zoom_steps = 1000.0F});
    require(near_equal(tactical.distance(), tactical.minimum_distance()), "tactical zoom-in was not clamped");
    tactical.apply({.zoom_steps = -1000.0F});
    require(near_equal(tactical.distance(), tactical.maximum_distance()), "tactical zoom-out was not clamped");
    tactical.apply({.reset = true});

    // Panning is relative to the heading: facing north, "right" moves the focus along the
    // renderer's screen-right axis and "forward" moves it north.
    tactical.apply({.pan_forward = 0.1F});
    require(near_equal(tactical.focus()[1], 150.0F) && near_equal(tactical.focus()[0], 0.0F), "tactical forward pan did not follow the heading");
    tactical.apply({.reset = true});
    tactical.apply({.pan_right = 0.1F});
    const auto right_focus = tactical.focus();
    view = tactical.camera();
    // Screen right for the right-handed look-at is cross(forward, up).
    const eaw::Vector2 forward{view.target.x - view.eye.x, view.target.y - view.eye.y};
    require(near_equal(right_focus[0] * forward[0] + right_focus[1] * forward[1], 0.0F, 1.0F) && near_equal(std::fabs(right_focus[0]), 150.0F) && right_focus[0] * forward[1] > 0.0F,
            "tactical right pan is not perpendicular to the heading");

    // The focus stays inside the map, and malformed input or bounds cannot poison the view.
    tactical.apply({.pan_right = 1.0e6F, .pan_forward = 1.0e6F});
    require(tactical.focus()[0] >= -1000.0F && tactical.focus()[0] <= 1000.0F && tactical.focus()[1] >= -500.0F && tactical.focus()[1] <= 500.0F, "tactical focus left the map bounds");
    tactical.apply({.yaw = std::numeric_limits<float>::quiet_NaN(), .pitch = std::numeric_limits<float>::infinity(), .zoom_steps = std::numeric_limits<float>::quiet_NaN()});
    view = tactical.camera();
    require(std::isfinite(view.eye[0]) && std::isfinite(view.eye[1]) && std::isfinite(view.eye[2]), "non-finite camera input produced a non-finite view");
    eaw::TacticalCamera degenerate({{std::numeric_limits<float>::quiet_NaN(), 5.0F, 0.0F}, {1.0F, -5.0F, 0.0F}});
    view = degenerate.camera();
    require(std::isfinite(view.eye[0]) && distance(view) >= degenerate.minimum_distance() * 0.999F, "degenerate bounds produced an invalid tactical view");

    // The free camera starts where a view is, moves along its own view, and turns without limit in
    // yaw but stops short of straight up or down.
    eaw::VisualCamera start;
    start.eye = {0.0F, 0.0F, 100.0F};
    start.target = {100.0F, 0.0F, 100.0F};
    start.near_plane = 5.0F;
    eaw::FlyCamera fly(start);
    require(fly.camera().near_plane <= eaw::FlyCamera::close_near_plane && std::abs(fly.camera().target[1]) < 1e-5F, "the free camera must start at the view it is given");
    fly.apply({.right = 10.0F, .forward = 50.0F, .up = 5.0F});
    view = fly.camera();
    require(std::abs(view.eye[0] - 50.0F) < 1e-3F && std::abs(view.eye[1] + 10.0F) < 1e-3F && std::abs(view.eye[2] - 105.0F) < 1e-3F, "the free camera must move forward, right, and up");
    fly.apply({.yaw = 1.5707964F, .pitch = 10.0F});
    view = fly.camera();
    const auto look = eaw::normalize_or(view.target - view.eye, eaw::Vector3{});
    require(look.z < 1.0F && look.z > 0.99F && std::abs(look.x) < 0.01F, "the free camera must turn and stop short of straight up");
    fly.apply({.yaw = std::numeric_limits<float>::quiet_NaN(), .forward = std::numeric_limits<float>::infinity()});
    require(std::isfinite(fly.camera().eye[0]) && std::isfinite(fly.camera().target[2]), "non-finite free-camera input must be ignored");
}

void test_rts_control()
{
    // A camera above the origin looking down +X and slightly down.
    eaw::VisualCamera camera;
    camera.eye = {-100.0F, 0.0F, 80.0F};
    camera.target = {0.0F, 0.0F, 0.0F};
    camera.up = {0.0F, 0.0F, 1.0F};
    camera.vertical_fov_degrees = 50.0F;
    camera.near_plane = 1.0F;
    camera.far_plane = 5000.0F;
    const float width = 800.0F;
    const float height = 600.0F;

    // The target projects to the screen center; unprojecting it at its depth returns it.
    const auto center = eaw::project_to_screen(camera, width, height, {0.0F, 0.0F, 0.0F});
    require(center && std::fabs((*center)[0] - 399.5F) < 0.01F && std::fabs((*center)[1] - 299.5F) < 0.01F, "the look-at target must project to the screen center");
    require(!eaw::project_to_screen(camera, width, height, {-200.0F, 0.0F, 80.0F}), "a point behind the camera must not project");
    const eaw::Vector3 point{30.0F, 20.0F, 5.0F};
    const auto screen = eaw::project_to_screen(camera, width, height, point);
    require(screen.has_value(), "a visible point must project");
    // Depth of the point as the renderer stores it: z_ndc = f/(f-n) - n f/((f-n) z_view).
    const eaw::Vector3 forward{100.0F / std::sqrt(100.0F * 100.0F + 80.0F * 80.0F), 0.0F, -80.0F / std::sqrt(100.0F * 100.0F + 80.0F * 80.0F)};
    const auto view_depth = (point[0] - camera.eye[0]) * forward[0] + (point[2] - camera.eye[2]) * forward[2];
    const auto depth = camera.far_plane / (camera.far_plane - camera.near_plane) - camera.near_plane * camera.far_plane / ((camera.far_plane - camera.near_plane) * view_depth);
    const auto back = eaw::unproject_screen(camera, width, height, (*screen)[0], (*screen)[1], depth);
    require(std::fabs(back[0] - point[0]) < 0.05F && std::fabs(back[1] - point[1]) < 0.05F && std::fabs(back[2] - point[2]) < 0.05F, "unprojecting at the stored depth must return the point");
    const auto ground = eaw::intersect_ground_plane(camera, width, height, (*center)[0], (*center)[1], 0.0F);
    require(ground && std::fabs((*ground)[0]) < 0.05F && std::fabs((*ground)[1]) < 0.05F, "the center ray must meet the ground at the target");

    // Box selection keeps mobile, selectable entities inside the rectangle.
    std::vector<eaw::EntitySnapshot> entities(3);
    entities[0] = {{1, 1}, {0.0F, 0.0F, 0.0F}, 0.0F, 0, 0, false, true, 10.0F};
    entities[1] = {{2, 1}, {0.0F, 0.0F, 0.0F}, 0.0F, 0, 0, false, true, 0.0F};
    entities[2] = {{3, 1}, {0.0F, 300.0F, 0.0F}, 0.0F, 0, 0, false, true, 10.0F};
    const auto boxed = eaw::entities_in_rectangle(entities, camera, width, height, {350.0F, 250.0F, 450.0F, 350.0F}, true);
    require(boxed.size() == 1 && boxed[0] == eaw::EntityId{1, 1}, "drag-selection must keep only mobile entities inside the box");
    require(eaw::entities_in_rectangle(entities, camera, width, height, {450.0F, 350.0F, 350.0F, 250.0F}, false).size() == 2, "any corner order works and props count without mobile_only");

    // Grid targets are centered on the order point.
    const auto targets = eaw::grid_targets({100.0F, 50.0F, 2.0F}, 4, 10.0F);
    require(targets.size() == 4 && targets[0].x == 95.0F && targets[0].y == 45.0F && targets[3].x == 105.0F && targets[3].y == 55.0F && targets[2].z == 2.0F,
            "grid targets must form a centered square");
    require(eaw::grid_targets({}, 0, 10.0F).empty(), "no units need no targets");
}

void test_terrain_height_field()
{
    // A 4x4 field rising along X (height = 5 * column) with a liquid at 7.
    eaw::GroundMap field;
    field.width = 4;
    field.height = 4;
    field.spacing = 20.0F;
    for (std::uint32_t y = 0; y < 4; ++y)
    {
        for (std::uint32_t x = 0; x < 4; ++x)
        {
            field.heights.push_back(5.0F * static_cast<float>(x));
        }
    }
    require(field.valid() && std::fabs(field.terrain_height(30.0F, 10.0F) - 7.5F) < 1.0e-4F && field.terrain_height(-50.0F, 0.0F) == 0.0F && field.terrain_height(500.0F, 0.0F) == 15.0F,
            "terrain heights must interpolate within cells and clamp at the edges");
    field.liquid_level = 7.0F;
    require(field.surface_height(10.0F, 10.0F) == 7.0F && field.surface_height(50.0F, 10.0F) == 12.5F, "the liquid surface must cover lower terrain");

    // A camera looking down onto the field: the hit lies on the surface and on the pixel's ray.
    eaw::VisualCamera camera;
    camera.eye = {-40.0F, 30.0F, 120.0F};
    camera.target = {40.0F, 30.0F, 0.0F};
    camera.up = {0.0F, 0.0F, 1.0F};
    camera.vertical_fov_degrees = 60.0F;
    camera.near_plane = 1.0F;
    camera.far_plane = 2000.0F;
    const float width = 640.0F;
    const float height = 480.0F;
    for (const auto& pixel : {eaw::Vector2{320.0F, 240.0F}, eaw::Vector2{420.0F, 300.0F}, eaw::Vector2{250.0F, 180.0F}})
    {
        const auto hit = eaw::intersect_terrain(camera, width, height, pixel[0], pixel[1], field);
        require(hit.has_value(), "a ray into the map must hit its surface");
        require(std::fabs((*hit)[2] - field.surface_height((*hit)[0], (*hit)[1])) < 1.0e-3F, "a terrain hit must lie on the surface");
        const auto screen = eaw::project_to_screen(camera, width, height, *hit);
        require(screen && std::fabs((*screen)[0] - pixel[0]) < 0.5F && std::fabs((*screen)[1] - pixel[1]) < 0.5F, "a terrain hit must lie on the clicked pixel's ray");
    }
    camera.target = {-40.0F, 30.0F, 500.0F};
    camera.up = {1.0F, 0.0F, 0.0F};
    require(!eaw::intersect_terrain(camera, width, height, 320.0F, 240.0F, field), "a ray pointing away from the map must miss");
}

void test_debug_draw()
{
    // A unit box translated by (10, 20, 30): 12 edges, each of length 1 along one axis, all corners
    // inside the translated box.
    eaw::Matrix4 transform{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1}};
    std::vector<eaw::VisualDebugLine> lines;
    eaw::append_debug_box(lines, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, transform, {1.0F, 0.0F, 0.0F, 1.0F});
    require(lines.size() == 12, "a debug box must have 12 edges");
    std::array<int, 3> axis_edges{};
    for (const auto& line : lines)
    {
        int changed = -1;
        for (int axis = 0; axis < 3; ++axis)
        {
            const auto base = axis == 0 ? 10.0F : axis == 1 ? 20.0F : 30.0F;
            require(line.from[axis] >= base && line.from[axis] <= base + 1.0F && line.to[axis] >= base && line.to[axis] <= base + 1.0F, "debug box corner outside the transformed box");
            if (line.from[axis] != line.to[axis])
            {
                require(changed < 0 && std::fabs(line.to[axis] - line.from[axis]) == 1.0F, "a debug box edge must run along exactly one axis");
                changed = axis;
            }
        }
        require(changed >= 0, "a debug box edge must not be degenerate");
        ++axis_edges[static_cast<std::size_t>(changed)];
    }
    require(axis_edges == std::array<int, 3>{4, 4, 4}, "a debug box has four edges per axis");
    lines.clear();
    eaw::append_debug_axes(lines, {1.0F, 2.0F, 3.0F}, 5.0F);
    require(lines.size() == 3 && lines[0].to == eaw::Vector3{6.0F, 2.0F, 3.0F} && lines[2].to == eaw::Vector3{1.0F, 2.0F, 8.0F}, "debug axes have the wrong extent");
}

const ModuleRegistrar registrar("presentation", {
                                                    {"test_scene_presentation", &test_scene_presentation},
                                                    {"test_unit_animation", &test_unit_animation},
                                                    {"test_model_forward_axis", &test_model_forward_axis},
                                                    {"test_camera_controllers", &test_camera_controllers},
                                                    {"test_rts_control", &test_rts_control},
                                                    {"test_terrain_height_field", &test_terrain_height_field},
                                                    {"test_debug_draw", &test_debug_draw},
                                                });

} // namespace
