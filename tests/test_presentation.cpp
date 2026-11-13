// Presentation of a running battle: posing entities, unit animation, cameras, picking, and debug drawing.
#include "test_support.hpp"

#include "formats/alo_model_helpers.hpp"
#include "battle/battle.hpp"
#include "battle/battle_log.hpp"
#include "battle/battle_runtime.hpp"
#include "presentation/battle_effects.hpp"
#include "presentation/battle_frame.hpp"
#include "presentation/particle_effects.hpp"
#include "presentation/projectile_visuals.hpp"
#include "presentation/unit_animation.hpp"
#include "scene/scene_builder_internal.hpp"
#include "sandbox/sandbox_options.hpp"

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
    const std::array<eaw::MapEntity, 2> entities{{{spawn}, {broken}}};
    const std::array<eaw::MapEntityLook, 2> looks{{{1}, {1}}};
    eaw::SpawnableType spawnable;
    spawnable.type_name = "Spawned_Unit";
    eaw::SpawnableLook spawnable_look;
    spawnable_look.mesh = 0;
    spawnable_look.team_color = {0.25F, 0.5F, 1.0F, 1.0F};
    spawnable_look.scale = 0.5F;
    eaw::BattleMap map;
    map.entities = entities;
    map.spawnable = std::span(&spawnable, 1);
    eaw::Battle battle(map, "TEST.TED", 0.1);
    auto& world = battle.world();
    const eaw::ScenePresentation presentation(scene, entities, looks, battle.map_entity_ids(), std::span(&spawnable_look, 1));
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

    // A marker (Is_Marker) is an entity scripts can find, but it is not drawn.
    const eaw::MapEntity marker{spawn};
    eaw::MapEntityLook marker_look{1};
    marker_look.hidden = true;
    eaw::BattleMap marker_map;
    marker_map.entities = std::span(&marker, 1);
    eaw::Battle marker_battle(marker_map, "TEST.TED", 0.1);
    const eaw::ScenePresentation marker_presentation(scene, std::span(&marker, 1), std::span(&marker_look, 1), marker_battle.map_entity_ids(), {});
    marker_presentation.build(marker_battle.world().snapshot().entities, {}, instances, ids);
    require(marker_battle.world().snapshot().entities.size() == 1 && instances.size() == 1 && instances[0].background, "a marker must stay an entity but not be drawn");

    // An instance no entity claims (the water plane, terrain tracks) is drawn as scenery after the
    // backgrounds.
    auto water_scene = scene;
    water_scene.instances.push_back(eaw::VisualInstance{0});
    const eaw::ScenePresentation water_presentation(water_scene, std::span(&marker, 1), std::span(&marker_look, 1), marker_battle.map_entity_ids(), {});
    water_presentation.build(marker_battle.world().snapshot().entities, {}, instances, ids);
    require(instances.size() == 2 && instances[0].background && !instances[1].background && instances[1].mesh == 0 && !ids[1].valid(), "an instance no entity claims must be drawn as scenery");

    // A shot the shields absorbed flashes the entity's shield mesh after it, growing by step and gone
    // after the last.
    auto shielded_looks = looks;
    shielded_looks[0].shield_mesh = 1U;
    eaw::ScenePresentation shield_presentation(scene, entities, shielded_looks, battle.map_entity_ids(), std::span(&spawnable_look, 1));
    const auto shielded = world.snapshot().entities[0].id;
    const std::array<eaw::ImpactSnapshot, 2> hits{{{{}, {}, 0U, eaw::ImpactKind::hull, shielded}, {{}, {}, 0U, eaw::ImpactKind::shield, shielded}}};
    shield_presentation.flash_shields(hits, 10.0);
    const std::vector<eaw::EntitySnapshot> one{world.snapshot().entities[0]};
    shield_presentation.build(one, {}, instances, ids, nullptr, 10.15);
    require(instances.size() == 3 && instances[2].mesh == 1U && !ids[2].valid() &&
                std::fabs(eaw::length(eaw::Vector3{instances[2].transform(0, 0), instances[2].transform(0, 1), instances[2].transform(0, 2)}) -
                          1.1F * eaw::length(eaw::Vector3{instances[1].transform(0, 0), instances[1].transform(0, 1), instances[1].transform(0, 2)})) < 1.0e-4F,
            "a shield hit must show the shield mesh after its entity, at the step's scale");
    shield_presentation.build(one, {}, instances, ids, nullptr, 10.35);
    require(instances.size() == 2, "the shield flash must end after its last step");

    // A frame of a running battle, built without a window: backgrounds, then entities with their
    // ids; no palette without an animator.
    eaw::BattleRuntime runtime(map, "TEST.TED", nullptr);
    eaw::ScenePresentation frame_presentation(scene, entities, looks, runtime.battle().map_entity_ids(), std::span(&spawnable_look, 1));
    eaw::BattleEffects effects;
    eaw::BattlePresenters presenters{frame_presentation, nullptr, effects, nullptr};
    const auto previous = runtime.battle().world().snapshot();
    runtime.step();
    eaw::present_tick(runtime, presenters, 0.0);
    eaw::BattleFrame frame;
    eaw::build_battle_frame(runtime, previous, {}, 0.0, 0, presenters, frame);
    require(frame.instances.size() == 2 && frame.entities.size() == 2 && frame.instances[0].background && frame.entities[1] == runtime.battle().world().snapshot().entities[0].id &&
                frame.palette.empty() && frame.projectile_serials.empty(),
            "a battle frame must hold the backgrounds and the posed entities");

    // A decoration (XML Is_Decoration) is drawn and posed like an idle entity under an id of its
    // own; the simulation never sees it.
    const std::array<eaw::Decoration, 1> decorations{{{eaw::MapEntityLook{1}, {40.0F, 0.0F, 0.0F}, 0.0F, 0U}}};
    const eaw::ScenePresentation decorated(scene, {}, {}, {}, {}, decorations);
    const auto decoration_snapshots = decorated.decoration_snapshots();
    std::vector<eaw::VisualInstance> drawn;
    std::vector<eaw::EntityId> drawn_ids;
    decorated.build(decoration_snapshots, {}, drawn, drawn_ids);
    require(decoration_snapshots.size() == 1 && decoration_snapshots[0].id.index == eaw::decoration_id_base && drawn.size() == 2 && drawn_ids[1] == decoration_snapshots[0].id &&
                std::fabs(drawn[1].transform.values[12] - 40.0F) < 1.0e-4F,
            "a decoration must be drawn where it was placed, under its own id");

    // Damage states (Land_Damage_Thresholds/_Alternates): the state for the health fraction, and a
    // wreck shows the state for no health (rubble).
    {
        eaw::MapEntityLook damaged_look{1};
        damaged_look.damage = {{1.0F, 0U}, {0.66F, 1U}, {0.33F, 2U}, {0.0F, 3U}};
        eaw::BattleMap damaged_map;
        damaged_map.entities = std::span(&marker, 1);
        eaw::Battle damaged_battle(damaged_map, "TEST.TED", 0.1);
        eaw::ScenePresentation damaged(scene, std::span(&marker, 1), std::span(&damaged_look, 1), damaged_battle.map_entity_ids(), {});
        auto shown = damaged_battle.world().snapshot().entities;
        const auto state_at = [&](const float health)
        {
            shown[0].health = health;
            shown[0].max_health = 100.0F;
            damaged.build(shown, {}, instances, ids);
            return instances.back().state;
        };
        require(state_at(100.0F) == 0U && state_at(80.0F) == 0U && state_at(66.0F) == 1U && state_at(50.0F) == 1U && state_at(10.0F) == 2U,
                "an entity must show the damage state of its health fraction");
        eaw::DeathSnapshot death;
        death.id = shown[0].id;
        const auto wreck = damaged.corpse_instance(death);
        require(wreck && wreck->instance.state == 3U, "a wreck must show the damage state for no health");
    }
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
    require(clip("EI_TROOPER_CRUSHED_00") && clip("EI_TROOPER_CRUSHED_00")->role == eaw::AnimationRole::crushed, "CRUSHED_00 must be a crushed death clip");
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

    // Turret turns: about the model's up axis by +90 degrees the model's forward (-Y) goes to +X
    // (counter-clockwise, as headings); about -X by a positive angle the nose rises.
    {
        const auto pivot = eaw::translation(eaw::skeleton_bind_pose(*model)[0]);
        const auto turned_point = [&](const eaw::BoneTurn& turn)
        {
            std::vector<eaw::Matrix4> skin;
            const std::array<eaw::BoneTurn, 1> turns{turn};
            eaw::append_rig_local_palette(rig, eaw::bind_local_pose(*model), skin, turns);
            return eaw::transform_point({pivot.x, pivot.y - 1.0F, pivot.z}, skin[0]);
        };
        const auto yawed = turned_point({0, {0.0F, 0.0F, 1.0F}, 1.5707964F});
        const auto pitched = turned_point({0, {-1.0F, 0.0F, 0.0F}, 0.5F});
        require(std::fabs(yawed.x - pivot.x - 1.0F) < 1e-4F && std::fabs(yawed.y - pivot.y) < 1e-4F && pitched.z > pivot.z + 0.4F && pitched.y > pivot.y - 1.0F,
                "a turret must yaw counter-clockwise about up and a barrel pitch its nose up");
    }
    // A unit whose turret aims at a target to its left takes its own pose; its twin without a
    // target keeps the shared one.
    {
        eaw::UnitAnimator gunner(rigs, rest);
        std::vector<eaw::EntitySnapshot> pair(3);
        pair[0].id = {1, 1};
        pair[0].target = {3, 1};
        pair[0].turret_yaw = 1.5F;
        pair[1].id = {2, 1};
        pair[2].id = {3, 1};
        pair[2].position = {0.0F, 100.0F, 0.0F};
        std::vector<eaw::EntityId> pair_ids{pair[0].id, pair[1].id};
        std::vector<eaw::VisualInstance> pair_instances(2);
        eaw::AnimationProfile turreted;
        turreted.turret_bone = eaw::game_object_name_crc(model->bones[0].name);
        turreted.turret_rate = 10.0F;
        const std::array<eaw::AnimationProfile, 2> profiles{turreted, turreted};
        std::vector<eaw::Matrix4> aimed_palette;
        for (int frame = 1; frame <= 4; ++frame)
        {
            gunner.animate(pair, pair_ids, pair_instances, 0.25 * frame, 1U << 20U, aimed_palette, profiles);
        }
        require(pair_instances[0].bone_offset != pair_instances[1].bone_offset, "an aiming turret must take its own pose");
    }

    // Walker transition clips: names, turning in place, and the start of a walk.
    {
        const auto walker_clip = [](const char* file)
        {
            return eaw::parse_clip_name(file, "ev_at-at");
        };
        require(walker_clip("EV_AT-AT_TURNL_BEGIN_00") && walker_clip("EV_AT-AT_TURNL_BEGIN_00")->role == eaw::AnimationRole::turn_left_begin &&
                    walker_clip("EV_AT-AT_TURNR_00")->role == eaw::AnimationRole::turn_right && walker_clip("EV_AT-AT_MOVESTART_00")->role == eaw::AnimationRole::move_start &&
                    walker_clip("EV_AT-AT_MOVE_ENDTHREE_00")->role == eaw::AnimationRole::move_end && walker_clip("EV_AT-AT_MOVE_ENDTHREE_00")->variant == 2U,
                "walker transition clips must be named roles, MOVE_END by its foot");
        auto walker_rig = rig;
        walker_rig.clips = {{eaw::AnimationRole::idle, 0, "IDLE_00", animation},
                            {eaw::AnimationRole::move, 0, "MOVE_00", animation},
                            {eaw::AnimationRole::turn_left, 0, "TURNL_00", animation},
                            {eaw::AnimationRole::move_start, 0, "MOVESTART_00", animation}};
        const std::array<eaw::VisualRig, 1> walker_rigs{walker_rig};
        const std::vector<eaw::Matrix4> walker_rest(2, eaw::Matrix4::identity());
        // A unit turning left on the spot and its still twin: different clips, so different poses.
        eaw::UnitAnimator walkers(walker_rigs, walker_rest);
        std::vector<eaw::EntitySnapshot> both(2);
        both[0].id = {1, 1};
        both[1].id = {2, 1};
        both[0].max_speed = both[1].max_speed = 10.0F;
        std::vector<eaw::EntityId> both_ids{both[0].id, both[1].id};
        std::vector<eaw::VisualInstance> both_instances(2);
        std::vector<eaw::Matrix4> walker_palette;
        for (int frame = 1; frame <= 3; ++frame)
        {
            both[0].heading = 0.5F * static_cast<float>(frame);
            walkers.animate(both, both_ids, both_instances, 0.25 * frame, 1U << 20U, walker_palette);
        }
        require(both_instances[0].bone_offset != both_instances[1].bone_offset, "a unit turning on the spot must play its turn clip");
        // Starting to walk with Walk_Transition plays MOVESTART first: unlike its twin without it.
        eaw::UnitAnimator starters(walker_rigs, walker_rest);
        both[0].heading = 0.0F;
        const std::array<eaw::AnimationProfile, 2> starting{eaw::AnimationProfile{.walk_transition = true}, eaw::AnimationProfile{}};
        starters.animate(both, both_ids, both_instances, 1.0, 1U << 20U, walker_palette, starting);
        both[0].moving = both[1].moving = true;
        both[0].speed = both[1].speed = 10.0F;
        starters.animate(both, both_ids, both_instances, 1.1, 1U << 20U, walker_palette, starting);
        require(both_instances[0].bone_offset != both_instances[1].bone_offset, "a walker starting to move must play its start clip");

        // Walk transitions play at the move clip's rate at top speed: with a reference of a quarter
        // of its top speed (the AT-AT's Movement_Animation_Speed against its Max_Speed is 0.27),
        // the 1 s MOVESTART is over after 0.25 s and the walker then shows the same pose as its twin
        // without transitions; at its own rate (a reference of its top speed) it is still playing.
        // MOVESTART gets its own clip (the test clip raised 5 units), so its poses differ from MOVE's.
        auto raised = *animation;
        for (auto& track : raised.tracks)
        {
            for (auto& key : track.keys)
            {
                key.translation.z += 5.0F;
            }
        }
        auto start_rig = walker_rig;
        start_rig.clips[3].animation = std::make_shared<const eaw::Animation>(std::move(raised));
        const std::array<eaw::VisualRig, 1> start_rigs{start_rig};
        const auto pose_after = [&](const bool transition, const double after, const float reference)
        {
            eaw::UnitAnimator walker(start_rigs, walker_rest);
            std::vector<eaw::EntitySnapshot> one(1);
            one[0].id = {7, 1};
            one[0].max_speed = 10.0F;
            std::vector<eaw::EntityId> one_id{one[0].id};
            std::vector<eaw::VisualInstance> shown(1);
            std::vector<eaw::Matrix4> palette;
            const std::array<eaw::AnimationProfile, 1> profile{eaw::AnimationProfile{.move_reference = reference, .walk_transition = transition}};
            walker.animate(one, one_id, shown, 1.0, 1U << 20U, palette, profile);
            one[0].moving = true;
            one[0].speed = 10.0F;
            for (double at = 1.05; at < 1.0 + after + 1.0e-9; at += 0.05)
            {
                walker.animate(one, one_id, shown, at, 1U << 20U, palette, profile);
            }
            return std::vector<eaw::Matrix4>(palette.begin() + shown[0].bone_offset, palette.begin() + shown[0].bone_offset + 2);
        };
        const auto same = [](const std::vector<eaw::Matrix4>& a, const std::vector<eaw::Matrix4>& b)
        {
            return std::ranges::equal(a, b,
                                      [](const eaw::Matrix4& x, const eaw::Matrix4& y)
                                      {
                                          return std::ranges::equal(x.values, y.values);
                                      });
        };
        require(same(pose_after(true, 0.5, 0.25F), pose_after(false, 0.5, 0.25F)) && !same(pose_after(true, 0.5, 1.0F), pose_after(false, 0.5, 1.0F)),
                "a walk transition must play at the move clip's rate at top speed");
    }

    // A flyer with only an idle clip (a bird): moving, it stands in its rest pose unless its type
    // allows idling while moving (XML Allow_Idle_When_Moving).
    {
        auto idle_only = rig;
        idle_only.clips = {{eaw::AnimationRole::idle, 0, "IDLE_00", animation}};
        const std::array<eaw::VisualRig, 1> bird_rigs{idle_only};
        const std::vector<eaw::Matrix4> bird_rest(2, eaw::Matrix4::identity());
        eaw::EntitySnapshot bird;
        bird.id = {5, 1};
        bird.moving = true;
        bird.speed = 10.0F;
        bird.max_speed = 10.0F;
        const auto animated = [&](const bool idle_when_moving)
        {
            eaw::UnitAnimator flock(bird_rigs, bird_rest);
            std::vector<eaw::EntityId> ids{bird.id};
            std::vector<eaw::VisualInstance> shown(1);
            std::vector<eaw::Matrix4> bird_palette;
            const std::array<eaw::AnimationProfile, 1> profiles{eaw::AnimationProfile{.idle_when_moving = idle_when_moving}};
            flock.animate(std::span(&bird, 1), ids, shown, 1.0, 1U << 20U, bird_palette, profiles);
            return flock.stats().animated;
        };
        require(animated(false) == 0 && animated(true) == 1, "a moving type without a move clip must idle only when its type allows it");
    }

    // Cues: a still unit's IDLE (30 frames, 1 s) reported frame by frame passes every frame of each
    // loop once, wrapping at the loop's end (a type that loops it: Loop_Idle_Anim_00); a unit at
    // rest pose, a background, and a clipless mesh report nothing.
    {
        eaw::AnimationProfile looped;
        looped.loop_first_idle = true;
        const std::array<eaw::AnimationProfile, 1> looped_profiles{looped};
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
            cue_animator.animate(one, frame_ids, frame_instances, 1.0 + step / 30.0, 1U << 20U, palette, looped_profiles);
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

    // A slow unit blends idle and move; a unit that starts moving out of its aiming pose (ATTACKIDLE)
    // fades it out over 0.2 s.
    auto aiming_rig = rig;
    aiming_rig.clips.push_back({eaw::AnimationRole::attack_idle, 0, "ATTACKIDLE_00", animation});
    const std::array<eaw::VisualRig, 1> aiming_rigs{aiming_rig};
    eaw::UnitAnimator blender(aiming_rigs, rest);
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

    // Without ATTACKIDLE a unit with a target shows no aiming pose (its ATTACK belongs to its shots:
    // the T4-B's gun recoil looped while it had a target, in range or not), and a shot plays ATTACK.
    // ATTACK gets its own clip (the test clip raised 5 units) so its poses differ from IDLE's.
    {
        auto raised_attack = *animation;
        for (auto& track : raised_attack.tracks)
        {
            for (auto& key : track.keys)
            {
                key.translation.z += 5.0F;
            }
        }
        auto gun_rig = rig;
        gun_rig.clips[2].animation = std::make_shared<const eaw::Animation>(std::move(raised_attack));
        const std::array<eaw::VisualRig, 1> gun_rigs{gun_rig};
        // The pose at 1.6 s of a unit with or without a target, that fired at 1.55 s or not.
        const auto pose = [&](const bool targeting, const bool fired)
        {
            eaw::UnitAnimator gunner(gun_rigs, rest);
            std::vector<eaw::EntitySnapshot> tank(1);
            tank[0].id = {3, 1};
            tank[0].max_speed = 10.0F;
            tank[0].target = targeting ? eaw::EntityId{1, 1} : eaw::EntityId{};
            std::vector<eaw::EntityId> tank_id{tank[0].id};
            std::vector<eaw::VisualInstance> shown(1);
            std::vector<eaw::Matrix4> tank_palette;
            gunner.animate(tank, tank_id, shown, 1.0, 1U << 20U, tank_palette);
            gunner.animate(tank, tank_id, shown, 1.5, 1U << 20U, tank_palette);
            tank[0].last_shot_tick = fired ? 10U : 0U;
            gunner.animate(tank, tank_id, shown, 1.55, 1U << 20U, tank_palette);
            gunner.animate(tank, tank_id, shown, 1.6, 1U << 20U, tank_palette);
            return std::vector<eaw::Matrix4>(tank_palette.begin() + shown[0].bone_offset, tank_palette.begin() + shown[0].bone_offset + 2);
        };
        const auto same_pose = [](const std::vector<eaw::Matrix4>& a, const std::vector<eaw::Matrix4>& b)
        {
            return std::ranges::equal(a, b,
                                      [](const eaw::Matrix4& x, const eaw::Matrix4& y)
                                      {
                                          return std::ranges::equal(x.values, y.values);
                                      });
        };
        require(same_pose(pose(true, false), pose(false, false)) && !same_pose(pose(true, true), pose(false, false)), "a unit without ATTACKIDLE must not aim, and must play ATTACK when it fires");
    }

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
    eaw::set_translation(body.transform, {5.0F, 6.0F, 7.0F});
    mourner.add_corpse(body, {5, 1}, 100.0, eaw::DeathStyle{.die_variant = 0U, .persistence_seconds = 12.0F, .end_effect = 77U});
    none.clear();
    drawn.clear();
    mourner.animate({}, none, drawn, 111.0, 1U << 20U, palette);
    require(drawn.size() == 1 && mourner.ended_corpses().empty(), "a death clone's corpse must stay for its persistence");
    none.clear();
    drawn.clear();
    mourner.animate({}, none, drawn, 112.5, 1U << 20U, palette);
    require(drawn.empty() && mourner.ended_corpses().size() == 1 && mourner.ended_corpses()[0].effect == 77U && mourner.ended_corpses()[0].position == eaw::Vector3{5.0F, 6.0F, 7.0F},
            "a death clone's corpse must go once its persistence ends, reporting its end effect where it lay");
    // A clipless mesh in a damage state keeps its rubble, at rest pose, for good.
    auto rubble = prop;
    rubble.state = 3U;
    mourner.add_corpse(rubble, {6, 1}, 200.0);
    none.clear();
    drawn.clear();
    mourner.animate({}, none, drawn, 9000.0, 1U << 20U, palette);
    require(drawn.size() == 1 && drawn[0].state == 3U && drawn[0].bone_offset == death_rigs[1].rest_offset, "rubble must stay at rest pose");
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

    // Idles: each idle that ends is followed by a random IDLE variant, unless the type loops
    // IDLE_00 (Loop_Idle_Anim_00); a lone idle that does not loop pauses 2-4 s between plays.
    {
        auto idler_rig = rig;
        idler_rig.clips = {{eaw::AnimationRole::idle, 0, "IDLE_00", animation}, {eaw::AnimationRole::idle, 1, "IDLE_01", animation}};
        const std::array<eaw::VisualRig, 2> idle_rigs{idler_rig, still};
        std::vector<eaw::EntitySnapshot> lone_idler(1);
        lone_idler[0].id = {7, 1};
        lone_idler[0].max_speed = 10.0F;
        const auto played = [&](const bool loop_first, const std::span<const eaw::VisualRig> rigs_used = {}, std::size_t* busy_frames = nullptr)
        {
            eaw::AnimationProfile profile;
            profile.loop_first_idle = loop_first;
            const std::array<eaw::AnimationProfile, 1> profiles{profile};
            eaw::UnitAnimator idler(rigs_used.empty() ? std::span<const eaw::VisualRig>(idle_rigs) : rigs_used, rest);
            std::set<std::string> clips;
            for (int step = 0; step < 600; ++step)
            {
                std::vector<eaw::EntityId> ids{lone_idler[0].id};
                std::vector<eaw::VisualInstance> idle_instances(1);
                idler.animate(lone_idler, ids, idle_instances, static_cast<double>(step) * 0.05, 1U << 20U, palette, profiles);
                for (const auto& cue : idler.cues())
                {
                    clips.emplace(cue.clip);
                }
                if (busy_frames != nullptr && !idler.cues().empty())
                {
                    ++*busy_frames;
                }
            }
            return clips;
        };
        require(played(false) == std::set<std::string>{"IDLE_00", "IDLE_01"} && played(true) == std::set<std::string>{"IDLE_00"},
                "a unit must move on to random idles as each ends, one that loops IDLE_00 must keep it");
        // The test clip lasts 1 s: looped, nearly every frame plays it; paused, at most a third.
        auto lone_rig = rig;
        lone_rig.clips = {{eaw::AnimationRole::idle, 0, "IDLE_00", animation}};
        const std::array<eaw::VisualRig, 2> lone_rigs{lone_rig, still};
        std::size_t looping = 0;
        std::size_t pausing = 0;
        (void)played(true, lone_rigs, &looping);
        (void)played(false, lone_rigs, &pausing);
        require(looping > 550 && pausing > 100 && pausing < 200, "a lone idle that does not loop must rest 2-4 s between plays");
    }

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
