#include "test_support.hpp"

#include "battle.hpp"
#include "battle_log.hpp"
#include "particle_effects.hpp"
#include "projectile_visuals.hpp"
#include "sandbox_options.hpp"
#include "unit_animation.hpp"

namespace {

using namespace eaw_test;

void test_asset_cache_dependencies()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-asset-cache-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "base" / "Data" / "Art" / "Models");
    std::filesystem::create_directories(root / "mod" / "Data");
    const auto write_file = [](const std::filesystem::path& path, const std::span<const std::byte> bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    };
    const auto model_bytes = make_test_alo();
    write_file(root / "base" / "Data" / "Art" / "Models" / "TEST.ALO", model_bytes);
    const auto archive = root / "data.meg";
    write_test_meg(archive, {{"DATA\\XML\\A.XML", "<A />"}});

    // Priority: base directory < archive < mod directory.
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("base", root / "base"));
    vfs.mount(expect_ok(eaw::MegMount::open("archive", archive), "opening the test archive"));
    vfs.mount(std::make_unique<eaw::DirectoryMount>("mod", root / "mod"));

    eaw::AssetCache cache;
    const eaw::RecordingAssetReader first_build(vfs);
    (void)expect_ok(eaw::build_visual_model(first_build, "Data/Art/Models/TEST.ALO", &cache), "first cached model build");
    const eaw::RecordingAssetReader second_build(vfs);
    (void)expect_ok(eaw::build_visual_model(second_build, "Data/Art/Models/TEST.ALO", &cache), "second cached model build");
    const auto stats = cache.stats();
    require(stats.entries == 1 && stats.misses == 1 && stats.hits == 1, "a second build must reuse the cached model");
    const auto scene_dependencies = second_build.dependencies();
    require(scene_dependencies.file_count() >= 1 && scene_dependencies.changes(vfs).empty(), "a reused asset must contribute its dependencies, unchanged");

    // A missing file is a dependency too: adding it changes the recorded state.
    const eaw::RecordingAssetReader probe(vfs);
    require(!probe.resolve("Data/Art/Textures/NEW.DDS"), "the probe file must start absent");
    const std::array<std::byte, 1> one_byte{std::byte{1}};
    write_file(root / "mod" / "Data" / "Art" / "Textures" / "NEW.DDS", one_byte);
    require(probe.dependencies().changes(vfs) == std::vector<std::string>{"DATA/ART/TEXTURES/NEW.DDS"}, "adding a previously missing file must be detected");

    // A loose override in a higher layer invalidates the model cached from the lower layer.
    write_file(root / "mod" / "Data" / "Art" / "Models" / "TEST.ALO", model_bytes);
    require(scene_dependencies.changes(vfs) == std::vector<std::string>{"DATA/ART/MODELS/TEST.ALO"}, "a mod override must change the dependency");
    require(cache.invalidate(vfs).size() == 1 && cache.stats().entries == 0, "the overridden model must be dropped from the cache");

    // Archive entries: a rebuilt archive is detected, remounted, and its assets reload.
    const auto read_text = [](const eaw::AssetReader& reader)
    {
        const auto bytes = expect_ok(reader.read("Data/XML/A.XML"), "reading A.XML");
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    const auto text = cache.load<std::string>("text", "Data/XML/A.XML", vfs, read_text);
    require(*text == "<A />", "archive text was not loaded");
    eaw::ArchiveWatch watch(vfs);
    require(watch.changed(vfs).empty(), "an untouched archive must not report a change");
    write_test_meg(archive, {{"DATA\\XML\\A.XML", "<A version='2' />"}});
    require(watch.changed(vfs) == std::vector<std::size_t>{1}, "a rebuilt archive must be reported by mount index");
    vfs.replace_mount(1, expect_ok(eaw::MegMount::open("archive", archive), "reopening the rebuilt archive"));
    watch.accept(vfs, 1);
    require(watch.changed(vfs).empty(), "an accepted remount must clear the change");
    require(cache.invalidate(vfs) == std::vector<std::string>{"text|DATA/XML/A.XML"}, "assets from a rebuilt archive must be invalidated");
    require(*cache.load<std::string>("text", "Data/XML/A.XML", vfs, read_text) == "<A version='2' />", "the remounted archive must serve the new bytes");

    // A failing loader caches nothing, so the next load retries.
    const auto load_missing = [&]
    {
        return cache.try_load<std::string>("text", "Data/XML/MISSING.XML", vfs,
                                           [](const eaw::AssetReader& reader) -> eaw::Result<std::string>
                                           {
                                               EAW_TRY_ASSIGN(const auto bytes, reader.read("Data/XML/MISSING.XML"));
                                               return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                                           });
    };
    const auto misses_before = cache.stats().misses;
    const auto failed = load_missing();
    require(!failed && failed.error().message.find("MISSING.XML") != std::string::npos && cache.stats().entries == 1, "a failed load must be returned and not cached");
    require(!load_missing() && cache.stats().misses == misses_before + 2U, "a failed load must run the loader again");

    // Eviction: entries a build did not touch are dropped once it succeeds.
    const auto epoch = cache.begin_epoch();
    (void)cache.load<std::string>("text", "Data/XML/OTHER.XML", vfs,
                                  [](const eaw::AssetReader&)
                                  {
                                      return std::string("other");
                                  });
    require(cache.evict_unused_since(epoch) == 1 && cache.stats().entries == 1 && *cache.load<std::string>("text", "Data/XML/OTHER.XML", vfs, read_text) == "other",
            "only entries unused since the epoch must be evicted");
    std::filesystem::remove_all(root, ignored);
}

void test_debug_draw()
{
    // A unit box translated by (10, 20, 30): 12 edges, each of length 1 along one axis, all corners
    // inside the translated box.
    std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1};
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
    require(lines.size() == 3 && lines[0].to == std::array<float, 3>{6.0F, 2.0F, 3.0F} && lines[2].to == std::array<float, 3>{1.0F, 2.0F, 8.0F}, "debug axes have the wrong extent");
}

void test_scene_presentation()
{
    // pose_transform keeps scale and tilt, turns about +Z by the heading change, and moves.
    const std::array<float, 16> base{2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 5, 6, 7, 1};
    const auto same = eaw::pose_transform(base, 0.0F, {1.0F, 2.0F, 3.0F}, 0.0F);
    require(same[0] == 2.0F && same[5] == 2.0F && same[12] == 1.0F && same[13] == 2.0F && same[14] == 3.0F, "an unchanged heading must only move the transform");
    const auto turned = eaw::pose_transform(base, 0.0F, {0.0F, 0.0F, 0.0F}, std::numbers::pi_v<float> / 2.0F);
    require(std::fabs(turned[0]) < 1.0e-5F && std::fabs(turned[1] - 2.0F) < 1.0e-5F && std::fabs(turned[4] + 2.0F) < 1.0e-5F && turned[10] == 2.0F,
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
    unit.transform[12] = 10.0F;
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
    require(std::fabs(instances[1].transform[13] - 1.0F) < 1.0e-5F && std::fabs(instances[1].transform[1] - 1.0F) < 1.0e-5F, "an entity's instance must follow its simulated position and heading");
    require(world.type_name(id) == "Test_Unit" && presentation.mesh_name(0) == "UNIT.ALO", "entities must be named by type and mesh");
    // An entity spawned by order is drawn from its type's template without a recorded id.
    expect_ok(battle.submit({.order = eaw::BattleOrder::spawn, .point = {-20.0F, 0.0F, 0.0F}, .type = "Spawned_Unit", .count = 1, .owner = 2}), "spawn order");
    battle.step();
    presentation.build(world.snapshot().entities, {}, instances, ids);
    require(instances.size() == 3 && instances[2].mesh == 0 && instances[2].team_color[2] == 1.0F && std::fabs(instances[2].transform[12] + 20.0F) < 1.0e-4F,
            "an entity spawned by order must be drawn from its type's template");
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
    const std::array<float, 3> point{30.0F, 20.0F, 5.0F};
    const auto screen = eaw::project_to_screen(camera, width, height, point);
    require(screen.has_value(), "a visible point must project");
    // Depth of the point as the renderer stores it: z_ndc = f/(f-n) - n f/((f-n) z_view).
    const std::array<float, 3> forward{100.0F / std::sqrt(100.0F * 100.0F + 80.0F * 80.0F), 0.0F, -80.0F / std::sqrt(100.0F * 100.0F + 80.0F * 80.0F)};
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
    for (const auto& pixel : {std::array<float, 2>{320.0F, 240.0F}, std::array<float, 2>{420.0F, 300.0F}, std::array<float, 2>{250.0F, 180.0F}})
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

void test_combat_content()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-combat-content-test";
    const auto xml_root = root / "Data" / "XML";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(xml_root);
    {
        std::ofstream file(xml_root / "GameConstants.xml", std::ios::binary);
        file << "<GameConstants>"
                "<Damage_To_Armor_Mod> Damage_Laser, Armor_Tank, 0.5 </Damage_To_Armor_Mod>"
                "<Damage_To_Armor_Mod>Damage_Laser, Shield_Tank, 2</Damage_To_Armor_Mod>"
                "<Damage_To_Armor_Mod>Damage_Laser, Armor_Tank</Damage_To_Armor_Mod>"
                "</GameConstants>";
    }
    {
        std::ofstream file(xml_root / "Units.xml", std::ios::binary);
        file << "<Units>"
                "<Projectile Name=\"Proj_Base\"><Max_Speed>10</Max_Speed><Projectile_Max_Flight_Distance>400</Projectile_Max_Flight_Distance>"
                "<Projectile_Does_Shield_Damage>No</Projectile_Does_Shield_Damage><Damage_Type>Damage_Laser</Damage_Type></Projectile>"
                "<Projectile Name=\"Proj_Tank\"><Variant_Of_Existing_Type>Proj_Base</Variant_Of_Existing_Type><Projectile_Damage>7.5</Projectile_Damage></Projectile>"
                "<GroundVehicle Name=\"Tank\"><Tactical_Health>100</Tactical_Health><Shield_Points>50</Shield_Points><Shield_Refresh_Rate>4</Shield_Refresh_Rate>"
                "<Armor_Type> Armor_Tank </Armor_Type><Shield_Armor_Type>Shield_Tank</Shield_Armor_Type><Ranged_Target_Z_Adjust>12</Ranged_Target_Z_Adjust>"
                "<Projectile_Types>Proj_Tank, Proj_Base</Projectile_Types><Targeting_Max_Attack_Distance>200</Targeting_Max_Attack_Distance>"
                "<Projectile_Fire_Recharge_Seconds>1.5</Projectile_Fire_Recharge_Seconds><Projectile_Fire_Pulse_Count>3</Projectile_Fire_Pulse_Count>"
                "<Projectile_Fire_Pulse_Delay_Seconds>0.2</Projectile_Fire_Pulse_Delay_Seconds></GroundVehicle>"
                "<GroundStructure Name=\"Wall\"><Tactical_Health>-5</Tactical_Health></GroundStructure>"
                "<HardPoint Name=\"HP_Gun\"><Type> HARD_POINT_WEAPON_LASER </Type><Fire_Projectile_Type>Proj_Tank</Fire_Projectile_Type>"
                "<Fire_Min_Recharge_Seconds>3</Fire_Min_Recharge_Seconds><Fire_Max_Recharge_Seconds>4</Fire_Max_Recharge_Seconds><Fire_Pulse_Count>7</Fire_Pulse_Count>"
                "<Fire_Pulse_Delay_Seconds>0.2</Fire_Pulse_Delay_Seconds><Fire_Range_Distance>2000</Fire_Range_Distance><Damage_Type>Damage_Unknown</Damage_Type></HardPoint>"
                "<HardPoint Name=\"HP_Shield\"><Type>HARD_POINT_SHIELD_GENERATOR</Type><Health>375</Health></HardPoint>"
                "<SpaceUnit Name=\"Cruiser\"><Tactical_Health>500</Tactical_Health><Targeting_Max_Attack_Distance>1500</Targeting_Max_Attack_Distance>"
                "<HardPoints>HP_Gun, HP_Shield,\n HP_Gun HP_Missing</HardPoints></SpaceUnit>"
                "<GroundVehicle Name=\"Broken_Gun\"><Tactical_Health>10</Tactical_Health><Projectile_Types>Proj_Missing</Projectile_Types>"
                "<Targeting_Max_Attack_Distance>100</Targeting_Max_Attack_Distance></GroundVehicle>"
                "</Units>";
    }
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    const auto database = eaw::XmlAssetDatabase::build(vfs);
    const auto catalog = eaw::GameObjectCatalog::build(database);
    const auto content = eaw::load_combat_content(vfs);
    require(content.damage_table.size() == 2 && content.warnings.size() == 1 && content.warnings[0].find("Damage_To_Armor_Mod") != std::string::npos,
            "damage modifiers must be read and malformed entries reported");
    const auto laser = content.type_ids.at("damage_laser");
    const auto tank_armor = content.type_ids.at("armor_tank");
    require(content.damage_table.modifier(laser, tank_armor) == 0.5F && content.damage_table.modifier(laser, content.type_ids.at("shield_tank")) == 2.0F,
            "damage modifiers must map type names to ids");

    const auto definition = [&](const std::string_view name)
    {
        return catalog.find(name).front();
    };
    std::vector<std::string> warnings;
    const auto tank = eaw::combat_profile(catalog, *definition("Tank"), content, warnings);
    require(tank.health == 100.0F && tank.shield == 50.0F && tank.shield_regen_per_second == 4.0F && tank.armor_type == tank_armor && tank.aim_height == 12.0F && tank.weapons.size() == 1,
            "a type's health, shields, armor, and weapon must be read");
    const auto& weapon = tank.weapons.front();
    require(weapon.range == 200.0F && weapon.recharge_seconds == 1.5F && weapon.pulse_count == 3 && weapon.pulse_delay_seconds == 0.2F && weapon.damage == 7.5F && weapon.damage_type == laser &&
                weapon.projectile_speed == 10.0F * eaw::provisional_max_speed_scale && weapon.max_flight_distance == 400.0F && !weapon.damages_shield && weapon.damages_health,
            "the first projectile type must supply the shot, through its variant chain");
    require(warnings.empty(), "a well-formed type must not warn");
    const auto wall = eaw::combat_profile(catalog, *definition("Wall"), content, warnings);
    require(wall.health == 0.0F && wall.weapons.empty() && warnings.size() == 1 && warnings[0].find("Tactical_Health") != std::string::npos, "a negative health must be reported and ignored");
    const auto broken = eaw::combat_profile(catalog, *definition("Broken_Gun"), content, warnings);
    require(broken.health == 10.0F && broken.weapons.empty() && warnings.size() == 2 && warnings[1].find("Proj_Missing") != std::string::npos, "an undefined projectile type must be reported");

    // Weapon hardpoints become weapons (other hardpoint types do not); the recharge is the midpoint of
    // the min/max range and a hardpoint's Damage_Type overrides its projectile's.
    const auto cruiser = eaw::combat_profile(catalog, *definition("Cruiser"), content, warnings);
    require(cruiser.weapons.size() == 2 && cruiser.range() == 2000.0F, "every weapon hardpoint must become a weapon");
    using Kind = eaw::HardpointProfile::Kind;
    require(cruiser.hardpoints.size() == 3 && cruiser.hardpoints[0].kind == Kind::weapon && cruiser.hardpoints[1].kind == Kind::shield_generator && cruiser.hardpoints[1].health == 375.0F &&
                cruiser.hardpoints[0].health == 0.0F && cruiser.weapons[0].hardpoint == 0 && cruiser.weapons[1].hardpoint == 2 && cruiser.weapons[0].cone_half_angle >= 3.14F,
            "every hardpoint must become a part (health, kind), weapons must name their hardpoint, and without bones cover all around");
    const auto& turbolaser = cruiser.weapons.front();
    require(turbolaser.recharge_seconds == 3.5F && turbolaser.pulse_count == 7 && turbolaser.pulse_delay_seconds == 0.2F && turbolaser.damage == 7.5F && turbolaser.damage_type == 0 &&
                turbolaser.projectile_speed == 10.0F * eaw::provisional_max_speed_scale,
            "a weapon hardpoint must read its fire fields and its projectile");
    require(std::ranges::any_of(warnings,
                                [](const std::string& warning)
                                {
                                    return warning.find("HP_Missing") != std::string::npos;
                                }),
            "an undefined hardpoint must be reported");

    const auto hostility = eaw::provisional_map_hostility();
    require(hostility.hostile(0, 1) && hostility.hostile(2, 6) && !hostility.hostile(eaw::neutral_map_owner, 1) && !hostility.hostile(1, 1), "map owners must be hostile except the neutral slot");
    std::filesystem::remove_all(root, ignored);
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
    // Screen right for a left-handed look-at is cross(up, forward).
    const std::array<float, 2> forward{view.target[0] - view.eye[0], view.target[1] - view.eye[1]};
    require(near_equal(right_focus[0] * forward[0] + right_focus[1] * forward[1], 0.0F, 1.0F) && near_equal(std::fabs(right_focus[0]), 150.0F) && right_focus[0] * -forward[1] > 0.0F,
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
}

void test_visual_material_translation()
{
    // The synthetic ALO uses MeshGloss.fx with an exact Diffuse vector4 and a malformed Specular.
    const auto root = std::filesystem::temp_directory_path() / "eawengine-visual-material-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "Data" / "Art" / "Models");
    {
        const auto bytes = make_test_alo();
        std::ofstream file(root / "Data" / "Art" / "Models" / "TEST.ALO", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    const auto asset = expect_ok(eaw::build_visual_model(vfs, "Data/Art/Models/TEST.ALO"), "building the visual model");
    const auto& scene = asset.scene;
    require(scene.meshes.size() == 1 && scene.instances.size() == 1 && scene.materials.size() == 1 && scene.vertices.size() == 3, "visual model scene structure is wrong");
    const auto& material = scene.materials[0];
    require(material.lighting == eaw::VisualLightingModel::lit && material.gloss == eaw::VisualGlossSource::base_alpha && !material.team_color_mask && material.blend == eaw::VisualBlendMode::opaque,
            "gloss profile did not become a lit, base-alpha-gloss material");
    require(material.diffuse == std::array<float, 4>{0.25F, 0.5F, 0.75F, 0.123456789F} && material.specular == std::array<float, 3>{1.0F, 1.0F, 1.0F} &&
                material.emissive == std::array<float, 3>{0.0F, 0.0F, 0.0F},
            "material colors were not taken from exact parameters with legacy defaults");
    require(material.base_color_texture == eaw::invalid_visual_texture && material.tint != std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F}, "untextured material did not receive a debug tint");
    require(scene.instances[0].team_color[3] == 1.0F && !scene.instances[0].background, "model instance team color or layer is wrong");

    // A mesh attached to a bone hidden in the bind pose (an alternate state such as debris) is kept
    // in a model scene but bound to the visibility half of the palette, which collapses it until an
    // animation shows the bone.
    {
        const auto bytes = make_test_alo(false);
        std::ofstream file(root / "Data" / "Art" / "Models" / "HIDDEN.ALO", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const auto hidden = expect_ok(eaw::build_visual_model(vfs, "Data/Art/Models/HIDDEN.ALO"), "building the hidden-bone model");
    require(hidden.visibility_palette && hidden.scene.bone_count == 2 && hidden.scene.vertices.size() == 3 && hidden.scene.vertices[0].bone_indices[0] == 1,
            "a rigid attachment must reference the visibility half of the palette");
    const auto hidden_palette = eaw::sample_skin_palette(hidden, nullptr, 0.0F);
    std::array<float, 16> collapsed{};
    collapsed[15] = 1.0F;
    require(hidden_palette.size() == 2 && hidden_palette[1] == collapsed && hidden_palette[0][0] == 1.0F, "a bone hidden in the bind pose must collapse its attachments");
    const auto shown_palette = eaw::sample_skin_palette(asset, nullptr, 0.0F);
    require(shown_palette.size() == 2 && shown_palette[1] == shown_palette[0], "a visible bone's visibility entry must match its skin matrix");
    std::filesystem::remove_all(root, ignored);
}

void test_visual_terrain()
{
    // The synthetic land TED has a 3x2 grid, two layers, and per-vertex layers {0,0,1,1,0,1}.
    const auto root = std::filesystem::temp_directory_path() / "eawengine-visual-terrain-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "Data" / "Art" / "Maps");
    std::filesystem::create_directories(root / "Data" / "Art" / "Textures");
    std::filesystem::create_directories(root / "Data" / "XML");
    {
        const auto bytes = make_test_dds();
        std::ofstream file(root / "Data" / "Art" / "Textures" / "RIVER.DDS", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    {
        const auto bytes = make_test_ted();
        std::ofstream file(root / "Data" / "Art" / "Maps" / "TEST.TED", std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    const auto asset = expect_ok(eaw::build_visual_map(vfs, "Data/Art/Maps/TEST.TED"), "building the visual map");
    const auto missing = eaw::build_visual_map(vfs, "Data/Art/Maps/MISSING.TED");
    require(!missing && missing.error().message.find("MISSING.TED") != std::string::npos, "a missing map must be reported by path");

    // Spawnable types: a type with a land model joins the mesh library with its speed; an unknown
    // type is reported, not fatal.
    {
        std::filesystem::create_directories(root / "Data" / "Art" / "Models");
        const auto model = make_test_alo();
        std::ofstream(root / "Data" / "Art" / "Models" / "TANK.ALO", std::ios::binary).write(reinterpret_cast<const char*>(model.data()), static_cast<std::streamsize>(model.size()));
        std::ofstream(root / "Data" / "XML" / "UNITS.XML", std::ios::binary)
            << "<Units><GroundVehicle Name=\"Test_Tank\"><Land_Model_Name>TANK.ALO</Land_Model_Name><Max_Speed>2</Max_Speed></GroundVehicle></Units>";
        const std::array<std::string, 2> types{"Test_Tank", "Missing_Type"};
        const auto with_spawns = expect_ok(eaw::build_visual_map(vfs, "Data/Art/Maps/TEST.TED", nullptr, types), "building the map with spawn types");
        require(with_spawns.spawnable.size() == 1 && with_spawns.spawnable[0].type_name == "Test_Tank" &&
                    std::fabs(with_spawns.spawnable[0].max_speed - 2.0F * eaw::provisional_max_speed_scale) < 1.0e-4F && with_spawns.spawnable[0].mesh < with_spawns.scene.meshes.size(),
                "a spawn type must resolve to a mesh and its scaled speed");
        require(std::ranges::any_of(with_spawns.scene.warnings,
                                    [](const std::string& warning)
                                    {
                                        return warning.find("Missing_Type") != std::string::npos;
                                    }),
                "an unknown spawn type must be reported");
        std::filesystem::remove(root / "Data" / "XML" / "UNITS.XML", ignored);
    }

    // Texture identities: stable across builds served from a cache (so the renderer keeps them on
    // the GPU), fresh for every decode.
    {
        eaw::AssetCache cache;
        const auto first = expect_ok(eaw::build_visual_map(vfs, "Data/Art/Maps/TEST.TED", &cache), "first cached map build");
        const auto second = expect_ok(eaw::build_visual_map(vfs, "Data/Art/Maps/TEST.TED", &cache), "second cached map build");
        require(!first.scene.textures.empty() && first.scene.textures.size() == second.scene.textures.size(), "cached map builds must produce the same textures");
        for (std::size_t index = 0; index < first.scene.textures.size(); ++index)
        {
            require(!first.scene.textures[index].identity.empty() && first.scene.textures[index].identity == second.scene.textures[index].identity,
                    "a texture served from the cache must keep its identity");
        }
        const auto uncached = expect_ok(eaw::build_visual_map(vfs, "Data/Art/Maps/TEST.TED"), "uncached map build");
        require(uncached.scene.textures.front().identity != first.scene.textures.front().identity, "a fresh decode must get a new identity");
    }
    const auto& terrain = asset.scene.terrain;
    require(asset.land && terrain.layer_count == 3 && terrain.vertices.size() == 12 && terrain.first_material + 3 <= asset.scene.materials.size(), "terrain mesh structure is wrong");
    require(asset.scene.materials[terrain.first_material].tint[3] == 1.0F && asset.scene.materials[terrain.first_material + 2].tint[3] == 0.0F, "an untextured terrain layer must carry zero coverage");
    require(std::fabs(asset.scene.materials[terrain.first_material].uv_scale - 1.0F / 64.0F) < 1.0e-7F, "terrain layer tiling was not applied");
    // First triangle: (0,0), (1,0), (1,1) with layers {0, 0, 0} -> sample (1,1) is index 4, layer 0.
    const auto& first = terrain.vertices[0];
    const auto& third = terrain.vertices[2];
    require(first.position == std::array<float, 3>{0.0F, 0.0F, -0.5F} && third.position == std::array<float, 3>{20.0F, 20.0F, 1.5F} && first.layers == std::array<std::uint32_t, 3>{0U, 0U, 0U} &&
                first.barycentric == std::array<float, 3>{1.0F, 0.0F, 0.0F} && third.barycentric == std::array<float, 3>{0.0F, 0.0F, 1.0F},
            "terrain vertex positions, layers, or barycentrics are wrong");
    // Second cell's first triangle uses samples (1,0), (2,0), (2,1): layers {0, 1, 1}.
    require(terrain.vertices[6].layers == std::array<std::uint32_t, 3>{0U, 1U, 1U}, "terrain corner layers were not taken from the samples");
    require(terrain.bounds_minimum[2] == -0.5F && terrain.bounds_maximum[0] == 40.0F && terrain.bounds_maximum[2] == 2.0F && asset.scene.bounds_maximum[1] == 20.0F,
            "terrain bounds were not applied to the scene");
    require(!asset.lighting, "an environment without lighting fields produced a light rig");
    // Water at level 1.5 crosses the terrain (-0.5..2.0) and becomes a blended quad instance.
    const auto& water_material = asset.scene.materials[asset.scene.draws[asset.scene.meshes[0].first_draw].material];
    require(asset.water == eaw::TedWaterType::water && asset.scene.meshes.size() == 2 && asset.scene.instances.size() == 2 && asset.scene.vertices.size() == 10 &&
                asset.scene.vertices[2].position == std::array<float, 3>{40.0F, 20.0F, 1.5F} && water_material.blend == eaw::VisualBlendMode::alpha_blend &&
                water_material.diffuse == std::array<float, 4>{0.4F, 0.6F, 0.8F, 0.75F},
            "water surface was not built over the terrain");
    // The track (5,5)->(35,15), widths 10->6, opacity 0/1/0 becomes a 3-sample draped ribbon.
    const auto& ribbon = asset.scene.vertices;
    require(asset.tracks == 1 && asset.scene.indices.size() == 6 + 12 && ribbon[4].color[3] == 0.0F && ribbon[6].color[3] == 1.0F && ribbon[9].color[3] == 0.0F &&
                ribbon[4].texture_coordinates[0] == 0.0F && ribbon[5].texture_coordinates[0] == 1.0F,
            "track ribbon structure or opacity fade is wrong");
    // First left edge vertex lies at (6.581, 0.257): the lower-right triangle of cell (0,0) gives
    // height -0.3163, plus the 0.6 lift.
    require(std::fabs(ribbon[4].position[0] - 6.5811F) < 1.0e-3F && std::fabs(ribbon[4].position[2] - 0.2837F) < 2.0e-3F, "track ribbon does not follow the terrain surface");
    std::filesystem::remove_all(root, ignored);
}

void test_sandbox_options()
{
    const auto parse = [](std::initializer_list<const char*> arguments)
    {
        std::vector<const char*> argv{"EAWEngine"};
        argv.insert(argv.end(), arguments.begin(), arguments.end());
        return eaw::parse_sandbox_options(static_cast<int>(argv.size()), argv.data());
    };
    const auto options = expect_ok(parse({"sandbox", "--map", "DATA/ART/MAPS/X.TED", "--spawn", "T2B_Tank:6@2", "--spawn", "AT_ST_Walker", "--vsync", "off", "--frame-limit", "10", "--overlay",
                                          "--mod", "D:/Mods/A", "--game", "eaw"}),
                                   "parsing sandbox options");
    require(options.mode == eaw::RunMode::sandbox && options.map == "DATA/ART/MAPS/X.TED" && !options.vsync && options.frame_limit == 10U && options.overlay && options.mods.size() == 1 &&
                options.variant == eaw::GameVariant::empire_at_war,
            "sandbox options must be read");
    require(options.spawns.size() == 2 && options.spawns[0].type == "T2B_Tank" && options.spawns[0].count == 6 && options.spawns[0].owner == 2U && options.spawns[1].type == "AT_ST_Walker" &&
                options.spawns[1].count == 0 && !options.spawns[1].owner,
            "--spawn must read its type, count, and owner");
    require(expect_ok(parse({"--help"}), "parsing --help").show_help && expect_ok(parse({"doctor"}), "parsing doctor").mode == eaw::RunMode::doctor && !expect_ok(parse({}), "parsing nothing").overlay,
            "the help flag, doctor mode, and the hidden statistics panel must be the defaults they claim");
    require(!parse({"--spawn", "Tank:1@64"}) && !parse({"--spawn", "Tank:1001"}) && !parse({"--spawn", ":3"}) && !parse({"--vsync", "maybe"}) && !parse({"--width", "0"}) && !parse({"--bogus"}) &&
                !parse({"--map"}) && !parse({"--frame-latency", "3"}),
            "invalid sandbox options must be rejected");
    const auto battle = expect_ok(parse({"battle", "--map", "DATA/ART/MAPS/X.TED", "--ticks", "500", "--record", "out.log", "--check-determinism", "--auto-move"}), "parsing battle options");
    require(battle.mode == eaw::RunMode::battle && battle.ticks == 500U && battle.record == std::filesystem::path("out.log") && battle.check_determinism && battle.auto_move,
            "battle options must be read");
    const auto opened = expect_ok(parse({"--panel", "spawn", "--panel", "menu"}), "parsing --panel");
    require(opened.panels.spawn && opened.panels.menu && !opened.panels.statistics && !parse({"--panel", "bogus"}), "--panel must open the named panels and reject others");
    const auto later = expect_ok(parse({"--map", "X.TED", "--spawn-later", "AT_ST_Walker:2@2"}), "parsing --spawn-later");
    require(later.spawns.empty() && later.later_spawns.size() == 1 && later.later_spawns[0].type == "AT_ST_Walker" && later.later_spawns[0].count == 2 && later.later_spawns[0].owner == 2U,
            "--spawn-later must read like --spawn into its own list");
    require(expect_ok(parse({"battle", "--replay", "in.log"}), "parsing a replay").replay == std::filesystem::path("in.log"), "a replay needs no map");
    require(!parse({"battle"}) && !parse({"battle", "--map", "X.TED", "--ticks", "0"}) && !parse({"battle", "--replay", "in.log", "--spawn", "Tank:1"}) &&
                !parse({"battle", "--replay", "in.log", "--auto-move"}) && !parse({"sandbox", "--replay", "in.log"}),
            "invalid battle options must be rejected");
}

void test_model_forward_axis()
{
    // Alamo models face -Y: an unrotated model's heading is -pi/2, and posing one with heading 0
    // (travel along +X) turns its forward (-Y) axis onto +X.
    const std::array<float, 16> identity{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    require(std::fabs(eaw::model_forward_heading(identity) - eaw::identity_model_heading) < 1.0e-6F, "an unrotated model must face -Y");
    const auto posed = eaw::pose_transform(identity, eaw::identity_model_heading, {10.0F, 20.0F, 0.0F}, 0.0F);
    {
        // Pitch lifts the model's forward axis (-Y) toward +Z; a positive bank lowers its left side (+X).
        const auto apply = [](const std::array<float, 16>& m, const std::array<float, 3>& v)
        {
            return std::array<float, 3>{v[0] * m[0] + v[1] * m[4] + v[2] * m[8], v[0] * m[1] + v[1] * m[5] + v[2] * m[9], v[0] * m[2] + v[1] * m[6] + v[2] * m[10]};
        };
        const auto near = [](const std::array<float, 3>& a, const std::array<float, 3>& b)
        {
            return std::fabs(a[0] - b[0]) < 1.0e-5F && std::fabs(a[1] - b[1]) < 1.0e-5F && std::fabs(a[2] - b[2]) < 1.0e-5F;
        };
        const auto climbing = eaw::pose_transform(identity, eaw::identity_model_heading, {}, 0.0F, std::numbers::pi_v<float> / 2.0F, 0.0F);
        require(near(apply(climbing, {0.0F, -1.0F, 0.0F}), {0.0F, 0.0F, 1.0F}), "pitch must lift the model's nose toward +Z");
        const auto banked = eaw::pose_transform(identity, eaw::identity_model_heading, {}, 0.0F, 0.0F, std::numbers::pi_v<float> / 2.0F);
        require(near(apply(banked, {1.0F, 0.0F, 0.0F}), {0.0F, 0.0F, -1.0F}) && near(apply(banked, {0.0F, -1.0F, 0.0F}), {1.0F, 0.0F, 0.0F}),
                "a positive bank must lower the model's left side about its nose");
        require(eaw::flight_bank(0.0F) == 0.0F && eaw::flight_bank(100.0F) > 1.0F && eaw::flight_bank(-100.0F) < -1.0F && eaw::flight_bank(std::numeric_limits<float>::quiet_NaN()) == 0.0F,
                "flight bank must follow the turn and stay bounded");
    }
    require(std::fabs(-posed[4] - 1.0F) < 1.0e-5F && std::fabs(posed[5]) < 1.0e-5F && std::fabs(eaw::model_forward_heading(posed)) < 1.0e-5F && posed[12] == 10.0F,
            "a model moving along +X must face +X");
}

// Battles: orders in replayable terms become simulation commands at the next tick and are recorded
// with that tick; invalid orders are rejected without effect; the same orders reproduce the same
// battle.
void test_battle_orders()
{
    eaw::SpawnableType fighter;
    fighter.type_name = "Fighter";
    fighter.max_speed = 120.0F;
    fighter.min_speed = 75.0F;
    fighter.turn_rate = 1.5F;
    fighter.radius = 5.0F;
    fighter.combat.health = 50.0F;
    fighter.combat.weapons = {eaw::WeaponProfile{.range = 400.0F, .recharge_seconds = 0.5F, .damage = 2.0F}};
    eaw::SpawnableType squadron;
    squadron.type_name = "Fighter_Squadron";
    squadron.members = {0, 0, 0};
    const std::array<eaw::SpawnableType, 2> types{fighter, squadron};
    eaw::BattleMap map;
    map.spawnable = types;
    map.hostility.set(1, 2, true);

    const auto run = [&](const bool record)
    {
        eaw::Battle battle(map, "TEST.TED", 1.0 / 30.0);
        expect_ok(battle.submit({.order = eaw::BattleOrder::spawn, .point = {0.0F, 0.0F, 0.0F}, .type = "Fighter_Squadron", .count = 2, .owner = 1}), "squadron spawn");
        expect_ok(battle.submit({.order = eaw::BattleOrder::spawn, .point = {300.0F, 0.0F, 0.0F}, .type = "Fighter", .count = 4, .owner = 2}), "fighter spawn");
        // Rejected orders change nothing and are not recorded.
        require(!battle.submit({.order = eaw::BattleOrder::spawn, .type = "Unknown"}), "an unknown type must be rejected");
        require(!battle.submit({.order = eaw::BattleOrder::spawn, .type = "Fighter", .count = 0}), "a zero count must be rejected");
        require(!battle.submit({.order = eaw::BattleOrder::spawn, .type = "Fighter", .owner = eaw::Hostility::max_owners}), "an owner beyond the table must be rejected");
        require(!battle.submit({.order = eaw::BattleOrder::move, .point = {1.0F, 2.0F, 3.0F}}), "an order without entities must be rejected");
        require(!battle.submit({.order = eaw::BattleOrder::move, .entities = {eaw::EntityId{1, 1}}, .point = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}}),
                "a non-finite point must be rejected");
        battle.step();
        require(battle.world().snapshot().entities.size() == 10, "spawn orders must apply at the next tick (two squadrons of three and four fighters)");
        if (record)
        {
            require(battle.log().entries.size() == 2 && battle.log().entries[0].tick == 1 && battle.log().entries[1].command.count == 4, "accepted orders must be recorded with their tick");
        }
        std::vector<eaw::EntityId> squadron_ids;
        for (const auto& entity : battle.world().snapshot().entities)
        {
            if (entity.owner == 1)
            {
                squadron_ids.push_back(entity.id);
            }
        }
        expect_ok(battle.submit({.order = eaw::BattleOrder::move, .entities = squadron_ids, .point = {600.0F, 200.0F, 0.0F}}), "move order");
        for (int tick = 0; tick < 300; ++tick)
        {
            battle.step();
        }
        return std::pair{battle.world().state_hash(), battle.world().stats().kills};
    };
    const auto first = run(true);
    const auto second = run(false);
    require(first.first == second.first && first.second == second.second, "the same orders must reproduce the same battle");
    require(first.second > 0, "the test battle must be a real fight");
}

// Battle logs: the text form round-trips every order bit-exactly, malformed logs are rejected with
// their line, and replaying a recorded battle's log reproduces its state hash.
void test_battle_log()
{
    eaw::BattleLog log;
    log.map = "DATA/ART/MAPS/SOME MOD MAP.TED";
    log.entries.push_back({1, {.order = eaw::BattleOrder::spawn, .point = {0.1F, -0.0F, 1.0e-30F}, .type = "Rebel_X-Wing_Squadron", .count = 2, .owner = 1}});
    log.entries.push_back({40, {.order = eaw::BattleOrder::move, .entities = {{17, 1}, {18, 3}}, .point = {600.25F, -3.4028235e38F, 0.333333343F}}});
    log.entries.push_back({40, {.order = eaw::BattleOrder::attack, .entities = {{17, 1}}, .target = {23, 2}}});
    log.entries.push_back({95, {.order = eaw::BattleOrder::stop, .entities = {{18, 3}}}});
    log.entries.push_back({96, {.order = eaw::BattleOrder::set_stance, .entities = {{18, 3}}, .stance = eaw::Stance::hold_position}});
    log.end_tick = 3000;
    log.end_hash = 0x9f3c0a1e5b7d2c44ULL;
    const auto text = eaw::format_battle_log(log);
    const auto parsed = eaw::parse_battle_log(text);
    require(parsed.ok(), "a formatted battle log must parse: " + (parsed.ok() ? std::string{} : parsed.error().message));
    require(eaw::format_battle_log(*parsed) == text, "a battle log must round-trip through its text form");
    const auto& point = parsed->entries[0].command.point;
    require(point.x == 0.1F && std::signbit(point.y) && point.z == 1.0e-30F && parsed->entries[1].command.point.y == -3.4028235e38F, "battle log numbers must read back bit-identically");
    require(parsed->map == log.map && parsed->entries[1].command.entities[1] == eaw::EntityId{18, 3} && parsed->entries[4].command.stance == eaw::Stance::hold_position && parsed->end_tick == 3000U &&
                parsed->end_hash == 0x9f3c0a1e5b7d2c44ULL,
            "battle log fields must survive the round trip");
    // Comments, blank lines, and CRLF line ends are accepted.
    require(eaw::parse_battle_log("EAWEngine battle log 1\r\n# a comment\r\n\r\n5 stop 1.1\r\n").ok(), "comments, blank lines, and CRLF must be accepted");
    const auto rejects = [](const std::string& body, const std::string& what, const std::string& line)
    {
        const auto result = eaw::parse_battle_log("EAWEngine battle log 1\n" + body);
        require(!result.ok() && result.error().message.find("line " + line) != std::string::npos, "a battle log with " + what + " must be rejected naming line " + line);
    };
    require(!eaw::parse_battle_log("").ok() && !eaw::parse_battle_log("EAWEngine battle log 2\n").ok(), "an empty or unknown-version log must be rejected");
    rejects("5 stop 1.1\n4 stop 1.1\n", "decreasing ticks", "3");
    rejects("5 dance 1.1\n", "an unknown order", "2");
    rejects("5 stop 1\n", "an id without a generation", "2");
    rejects("5 stop\n", "an order without entities", "2");
    rejects("5 move 1 nan 3 1.1\n", "a non-finite point", "2");
    rejects("5 spawn X-Wing 2 1 0 0\n", "a short spawn", "2");
    rejects("5 stance sleepy 1.1\n", "an unknown stance", "2");
    rejects("end 10\n12 stop 1.1\n", "orders after the end", "3");
    rejects("20 stop 1.1\nend 10\n", "an end before the last order", "3");
    rejects("end 10 xyz\n", "an invalid hash", "2");

    // Record a battle, write and read its log, and replay it into a fresh battle.
    eaw::SpawnableType fighter;
    fighter.type_name = "Fighter";
    fighter.max_speed = 120.0F;
    fighter.min_speed = 75.0F;
    fighter.turn_rate = 1.5F;
    fighter.radius = 5.0F;
    fighter.combat.health = 30.0F;
    fighter.combat.weapons = {eaw::WeaponProfile{.range = 400.0F, .recharge_seconds = 0.5F, .damage = 2.0F}};
    eaw::BattleMap map;
    map.spawnable = std::span(&fighter, 1);
    map.hostility.set(1, 2, true);
    eaw::Battle recorded(map, "TEST.TED");
    expect_ok(recorded.submit({.order = eaw::BattleOrder::spawn, .point = {0.1F, 0.2F, 0.3F}, .type = "Fighter", .count = 3, .owner = 1}), "spawn");
    expect_ok(recorded.submit({.order = eaw::BattleOrder::spawn, .point = {350.7F, 10.1F, 0.3F}, .type = "Fighter", .count = 3, .owner = 2}), "spawn");
    for (int tick = 0; tick < 50; ++tick)
    {
        recorded.step();
    }
    const auto first = recorded.world().snapshot().entities.front().id;
    expect_ok(recorded.submit({.order = eaw::BattleOrder::set_stance, .entities = {first}, .stance = eaw::Stance::hold_fire}), "stance");
    expect_ok(recorded.submit({.order = eaw::BattleOrder::move, .entities = {first}, .point = {-500.3F, 77.7F, 0.0F}}), "move");
    for (int tick = 0; tick < 250; ++tick)
    {
        recorded.step();
    }
    auto log_out = recorded.log();
    log_out.end_tick = recorded.world().stats().tick;
    log_out.end_hash = recorded.world().state_hash();
    const auto reread = eaw::parse_battle_log(eaw::format_battle_log(log_out));
    require(reread.ok(), "a recorded log must parse");
    eaw::Battle replayed(map, reread->map);
    std::size_t next = 0;
    while (replayed.world().stats().tick < *reread->end_tick)
    {
        for (; next < reread->entries.size() && reread->entries[next].tick <= replayed.world().stats().tick; ++next)
        {
            expect_ok(replayed.submit(reread->entries[next].command), "replayed order");
        }
        replayed.step();
    }
    require(replayed.world().state_hash() == *reread->end_hash, "replaying a recorded log must reproduce the battle's state hash");
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
    const auto model = std::make_shared<const eaw::AloModel>(expect_ok(eaw::AloModel::parse(make_test_alo()), "ALO parse"));
    const auto animation = std::make_shared<const eaw::AlaAnimation>(expect_ok(eaw::AlaAnimation::parse(make_test_ala()), "ALA parse"));
    eaw::VisualRig rig;
    rig.model = model;
    for (const auto& transform : model->bind_pose())
    {
        rig.inverse_bind_pose.push_back(expect_ok(eaw::alo_inverse(transform), "bind pose inverse"));
    }
    rig.palette_size = 2;
    rig.clips = {{eaw::AnimationRole::idle, 0, "IDLE_00", animation}, {eaw::AnimationRole::move, 0, "MOVE_00", animation}, {eaw::AnimationRole::attack, 0, "ATTACK_00", animation}};
    eaw::VisualRig still;
    still.rest_offset = 2;
    still.palette_size = 1;
    const std::array<eaw::VisualRig, 2> rigs{rig, still};
    const std::vector<std::array<float, 16>> rest(3, eaw::alo_identity_matrix().values);
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
    std::vector<std::array<float, 16>> palette;
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

// Types added at runtime: the battle offsets company members, presentation draws spawned entities
// of the new types from their meshes, and the animator places the new rigs after the old ones.
void test_runtime_spawnable_registration()
{
    eaw::SpawnableType trooper;
    trooper.type_name = "Trooper";
    trooper.max_speed = 10.0F;
    eaw::BattleMap map;
    map.spawnable = std::span(&trooper, 1);
    eaw::Battle battle(map, "TEST.TED", 0.1);
    // A package: a member type and its squad (members relative to the package).
    eaw::SpawnableType walker;
    walker.type_name = "Walker";
    walker.mesh = 1;
    walker.max_speed = 5.0F;
    walker.team_color = {0.0F, 1.0F, 0.0F, 1.0F};
    eaw::SpawnableType squad;
    squad.type_name = "Walker_Squad";
    squad.mesh = 1;
    squad.members = {0, 0};
    const std::array<eaw::SpawnableType, 2> package{walker, squad};
    const auto first = battle.add_spawnable(package);
    require(first == 1 && battle.spawnable().size() == 3 && battle.spawnable()[2].members == std::vector<std::size_t>{1, 1}, "added company members must point at the added member type");
    expect_ok(battle.submit({.order = eaw::BattleOrder::spawn, .point = {50.0F, 0.0F, 0.0F}, .type = "Walker_Squad", .count = 1, .owner = 2}), "spawn of an added squad");
    battle.step();
    require(battle.world().snapshot().entities.size() == 2 && battle.world().snapshot().entities[0].visual == eaw::spawnable_visual(1),
            "an added squad must spawn its members with the member type's visual key");

    eaw::VisualScene scene;
    scene.meshes.resize(1);
    scene.meshes[0].name = "TROOPER.ALO";
    eaw::ScenePresentation presentation(scene, {}, {}, std::span(&trooper, 1));
    eaw::VisualMesh walker_mesh;
    walker_mesh.name = "WALKER.ALO";
    presentation.add_meshes(std::span(&walker_mesh, 1));
    presentation.add_spawnable(package);
    std::vector<eaw::VisualInstance> instances;
    std::vector<eaw::EntityId> ids;
    presentation.build(battle.world().snapshot().entities, {}, instances, ids);
    require(instances.size() == 2 && instances[0].mesh == 1 && instances[0].team_color[1] == 1.0F && presentation.mesh_name(1) == "WALKER.ALO",
            "presentation must draw entities of added types from their added meshes");

    eaw::VisualRig base_rig;
    base_rig.palette_size = 3;
    const std::vector<std::array<float, 16>> base_rest(3, eaw::alo_identity_matrix().values);
    eaw::UnitAnimator animator(std::span(&base_rig, 1), base_rest);
    eaw::VisualRig added_rig;
    added_rig.rest_offset = 0;
    added_rig.palette_size = 2;
    const std::vector<std::array<float, 16>> added_rest(2, eaw::alo_identity_matrix().values);
    animator.add_rigs(std::span(&added_rig, 1), added_rest);
    std::vector<eaw::VisualInstance> drawn(1);
    drawn[0].mesh = 1;
    std::vector<eaw::EntityId> none(1);
    std::vector<std::array<float, 16>> palette;
    animator.animate({}, none, drawn, 1.0, 1U << 20U, palette);
    require(animator.rig_count() == 2 && palette.size() == 5 && drawn[0].bone_offset == 3, "added rigs must take their rest poses after the existing ones");
}

eaw::ParticleEmitter make_test_emitter()
{
    eaw::ParticleEmitter emitter;
    for (auto& track : emitter.tracks)
    {
        track.keys = {{0.0F, 1.0F}, {1.0F, 1.0F}};
    }
    emitter.tracks[static_cast<std::size_t>(eaw::ParticleTrackIndex::scale)].keys = {{0.0F, 4.0F}, {1.0F, 8.0F}};
    emitter.tracks[static_cast<std::size_t>(eaw::ParticleTrackIndex::texture_index)].keys = {{0.0F, 0.0F}, {1.0F, 0.0F}};
    emitter.tracks[static_cast<std::size_t>(eaw::ParticleTrackIndex::rotation_speed)].keys = {{0.0F, 0.0F}, {1.0F, 0.0F}};
    emitter.lifetime = 1.0F;
    return emitter;
}

std::shared_ptr<const eaw::ParticleEffect> make_test_effect(std::vector<eaw::ParticleEmitter> emitters, const bool leave_particles = true,
                                                            const eaw::ParticleFidelity fidelity = eaw::ParticleFidelity::legacy)
{
    eaw::ParticleSystem system;
    system.name = "test";
    system.emitters = std::move(emitters);
    system.leave_particles = leave_particles;
    auto effect = std::make_shared<eaw::ParticleEffect>(eaw::normalize_particle_system(system, fidelity));
    effect->textures.assign(effect->emitters.size(), 7U);
    return effect;
}

void test_intact_hidden_bones()
{
    // A hardpoint scorch bone (and so its meshes) collapses in the visibility half while intact.
    auto model = std::make_shared<eaw::AloModel>();
    model->bones.push_back({"Root", -1, true, 0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}});
    model->bones.push_back({"HP_Blast", 0, true, 0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}});
    eaw::VisualRig rig;
    rig.model = model;
    rig.palette_size = 4;
    rig.intact_hidden = {false, true};
    std::vector<std::array<float, 16>> palette;
    eaw::append_rig_local_palette(rig, {}, palette);
    require(palette.size() == 4 && palette[2][0] == 1.0F && palette[3][0] == 0.0F && palette[3][5] == 0.0F && palette[1][0] == 1.0F, "intact-hidden bone was not collapsed");
}

void test_projectile_sprites()
{
    // A bolt sits between ticks like entities, its head leading, and only typed shots are drawn.
    eaw::ProjectileVisualTable table;
    eaw::ProjectileVisual bolt;
    bolt.width = 1.0F;
    bolt.length = 6.0F;
    bolt.column = 3;
    bolt.tinted = true;
    bolt.color = {0.4F, 0.7F, 0.9F, 1.0F};
    table.emplace(5U, bolt);
    std::vector<eaw::ProjectileSnapshot> shots{{{100.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1U, 5U, 300.0F}, {{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, 1U, 6U, 300.0F}};
    std::vector<eaw::VisualParticle> sprites;
    eaw::append_projectile_sprites(shots, table, 9U, 0.5F, 1.0F / 30.0F, sprites);
    require(sprites.size() == 1, "untyped shots were drawn as bolts");
    const auto& sprite = sprites.front();
    // Half a tick behind (300 / 30 / 2 = 5) plus half the bolt's length (3).
    require(std::abs(sprite.position[0] - 92.0F) < 1e-4F && sprite.tail == 6.0F && sprite.half_size == 0.5F, "bolt placement failed");
    require(sprite.shape == eaw::VisualParticleShape::teardrop && sprite.texture == 9U && sprite.uv_rect[0] == 0.75F && sprite.uv_rect[2] == 0.25F && sprite.color[2] == 0.9F,
            "bolt appearance failed");
}

void test_particle_tracks()
{
    eaw::ParticleTrack track;
    track.keys = {{0.0F, 0.0F}, {0.5F, 10.0F}, {1.0F, 0.0F}};
    require(eaw::sample_particle_track(track, 0.25F) == 5.0F && eaw::sample_particle_track(track, 0.75F) == 5.0F && eaw::sample_particle_track(track, 2.0F) == 0.0F,
            "linear particle track sampling failed");
    track.interpolation = eaw::ParticleInterpolation::step;
    require(eaw::sample_particle_track(track, 0.4F) == 0.0F && eaw::sample_particle_track(track, 0.5F) == 10.0F, "step particle track sampling failed");
    track.interpolation = eaw::ParticleInterpolation::smooth;
    require(eaw::sample_particle_track(track, 0.125F) > 0.0F && eaw::sample_particle_track(track, 0.125F) < 2.5F, "smooth particle track sampling failed");
}

void test_particle_world()
{
    // Three bursts of four, 0.5 s apart, two-second particles: the effect ends on its own.
    auto burst = make_test_emitter();
    burst.use_bursts = true;
    burst.particles_per_burst = 4;
    burst.burst_count = 3;
    burst.burst_delay = 0.5F;
    burst.lifetime = 2.0F;
    burst.groups[0].type = 0;
    burst.groups[0].value = {0.0F, 0.0F, 10.0F};
    eaw::ParticleWorld world;
    const auto effect = make_test_effect({burst});
    std::array<float, 16> transform = eaw::visual_identity_transform;
    transform[12] = 100.0F;
    const auto id = world.play(effect, transform, 42);
    require(world.playing(id) && world.stats().particles == 0, "particle effect did not start");
    world.update(0.01F);
    require(world.stats().particles == 4, "first particle burst failed");
    for (int frame = 0; frame < 60; ++frame)
    {
        world.update(0.02F);
    }
    require(world.stats().particles == 12, "particle bursts did not accumulate");
    std::vector<eaw::VisualParticle> sprites;
    world.build_sprites({0.0F, 0.0F, 0.0F}, sprites);
    require(sprites.size() == 12 && sprites.front().texture == 7U && sprites.front().position[0] == 100.0F && sprites.front().position[2] > 0.0F, "particle sprites failed");
    require(sprites.front().half_size > 4.0F && sprites.front().half_size < 7.0F, "particle size track failed");
    for (std::size_t index = 1; index < sprites.size(); ++index)
    {
        require(sprites[index - 1].position[2] >= sprites[index].position[2], "particle sprites are not sorted back to front");
    }
    for (int frame = 0; frame < 100; ++frame)
    {
        world.update(0.02F);
    }
    require(!world.playing(id) && world.stats().particles == 0 && world.stats().instances == 0, "finished particle effect was not released");

    // The same seed replays the same particles.
    auto spray = make_test_emitter();
    spray.particles_per_second = 50;
    spray.groups[0].type = 3;
    spray.groups[0].sphere_radius = 20.0F;
    spray.random_scale = 0.5F;
    const auto looping = make_test_effect({spray}, false);
    const auto positions = [&](const std::uint64_t seed)
    {
        eaw::ParticleWorld replay;
        replay.play(looping, eaw::visual_identity_transform, seed);
        for (int frame = 0; frame < 30; ++frame)
        {
            replay.update(1.0F / 30.0F);
        }
        std::vector<eaw::VisualParticle> out;
        replay.build_sprites({0.0F, -100.0F, 0.0F}, out);
        std::vector<float> values;
        for (const auto& sprite : out)
        {
            values.insert(values.end(), {sprite.position[0], sprite.position[1], sprite.position[2], sprite.half_size});
        }
        return values;
    };
    require(positions(5) == positions(5) && positions(5) != positions(6), "particle seeds do not replay");

    // Looping effects run until stopped; without leave_particles they vanish at once.
    const auto loop = world.play(looping, eaw::visual_identity_transform, 1);
    for (int frame = 0; frame < 120; ++frame)
    {
        world.update(1.0F / 30.0F);
    }
    require(world.playing(loop) && world.stats().particles > 40, "looping particle effect stopped");
    world.stop(loop);
    require(!world.playing(loop) && world.stats().particles == 0, "stopped particle effect left particles");

    // Capacity bounds the live particles and counts the rest.
    eaw::ParticleWorld small(10);
    small.play(looping, eaw::visual_identity_transform, 3);
    for (int frame = 0; frame < 15; ++frame)
    {
        small.update(1.0F / 30.0F);
    }
    require(small.stats().particles == 10 && small.stats().dropped_particles > 0, "particle capacity failed");

    // A trail child emits from each living parent particle; a death child at each death.
    auto parent = make_test_emitter();
    parent.use_bursts = true;
    parent.particles_per_burst = 2;
    parent.burst_count = 1;
    parent.lifetime = 0.5F;
    parent.life_child = 1;
    parent.death_child = 2;
    auto trail = make_test_emitter();
    trail.particles_per_second = 10;
    trail.lifetime = 5.0F;
    auto debris = make_test_emitter();
    debris.use_bursts = true;
    debris.particles_per_burst = 3;
    debris.lifetime = 5.0F;
    eaw::ParticleWorld family;
    family.play(make_test_effect({parent, trail, debris}), eaw::visual_identity_transform, 9);
    for (int frame = 0; frame < 30; ++frame)
    {
        family.update(1.0F / 30.0F);
    }
    // Two parents for 0.5 s at 10 per second leave about ten trail particles, then six debris.
    const auto count = family.stats().particles;
    require(count >= 14 && count <= 18, "particle child emitters failed: " + std::to_string(count));

    // A moving emitter leaves an even trail: each particle spawns where the emitter was at its
    // emission time, not where it is at the end of the update.
    auto exhaust = make_test_emitter();
    exhaust.particles_per_second = 100;
    exhaust.lifetime = 10.0F;
    eaw::ParticleWorld trail_world;
    auto moving = eaw::visual_identity_transform;
    const auto trail_id = trail_world.play(make_test_effect({exhaust}), moving, 4);
    for (int frame = 1; frame <= 10; ++frame)
    {
        moving[12] = 10.0F * static_cast<float>(frame);
        trail_world.set_transform(trail_id, moving);
        trail_world.update(0.1F);
    }
    std::vector<eaw::VisualParticle> trail_sprites;
    trail_world.build_sprites({0.0F, 0.0F, 100.0F}, trail_sprites);
    std::vector<float> xs;
    for (const auto& sprite : trail_sprites)
    {
        xs.push_back(sprite.position[0]);
    }
    std::ranges::sort(xs);
    require(xs.size() >= 95 && xs.front() < 1.5F && xs.back() > 98.5F, "particle trail does not cover the emitter's path");
    for (std::size_t index = 1; index < xs.size(); ++index)
    {
        require(xs[index] - xs[index - 1] < 1.5F, "particle trail has a gap at x = " + std::to_string(xs[index - 1]));
    }

    // Semantics from the GlyphX particle editor: lifetime variance only shortens, a positive stored
    // inward acceleration pushes outward along the spawn offset, and freeze time stops the emitter.
    {
        auto short_lived = make_test_emitter();
        short_lived.use_bursts = true;
        short_lived.burst_count = 1;
        short_lived.particles_per_burst = 50;
        short_lived.lifetime = 1.0F;
        short_lived.random_lifetime = 0.5F;
        eaw::ParticleWorld lifetimes;
        lifetimes.play(make_test_effect({short_lived}), eaw::visual_identity_transform, 11);
        lifetimes.update(0.01F);
        require(lifetimes.stats().particles == 50, "lifetime test burst failed");
        for (int frame = 0; frame < 11; ++frame)
        {
            lifetimes.update(0.1F);
        }
        require(lifetimes.stats().particles == 0, "lifetime variance lengthened particles");

        auto pushed = make_test_emitter();
        pushed.use_bursts = true;
        pushed.burst_count = 1;
        pushed.lifetime = 10.0F;
        pushed.groups[2].type = 0;
        pushed.groups[2].value = {10.0F, 0.0F, 0.0F};
        pushed.inward_acceleration = 100.0F;
        eaw::ParticleWorld outward;
        outward.play(make_test_effect({pushed}), eaw::visual_identity_transform, 12);
        outward.update(0.01F);
        for (int frame = 0; frame < 10; ++frame)
        {
            outward.update(0.05F);
        }
        std::vector<eaw::VisualParticle> pushed_sprites;
        outward.build_sprites({}, pushed_sprites);
        require(pushed_sprites.size() == 1 && pushed_sprites[0].position[0] > 20.0F && std::abs(pushed_sprites[0].position[1]) < 1e-4F, "inward acceleration did not push outward");

        auto frozen = make_test_emitter();
        frozen.particles_per_second = 10;
        frozen.lifetime = 10.0F;
        frozen.freeze_time = 0.5F;
        frozen.groups[0].type = 0;
        frozen.groups[0].value = {0.0F, 0.0F, 10.0F};
        eaw::ParticleWorld freezing;
        freezing.play(make_test_effect({frozen}), eaw::visual_identity_transform, 13);
        for (int frame = 0; frame < 10; ++frame)
        {
            freezing.update(0.1F);
        }
        std::vector<eaw::VisualParticle> before;
        freezing.build_sprites({}, before);
        freezing.update(0.2F);
        std::vector<eaw::VisualParticle> after;
        freezing.build_sprites({}, after);
        require(before.size() == 6 && after.size() == before.size() && after.front().position == before.front().position, "freeze time did not stop the emitter");
    }

    // Intensity scales drawn size and opacity (engine glows at rest versus at full speed).
    {
        auto steady = make_test_emitter();
        steady.use_bursts = true;
        steady.burst_count = 1;
        steady.lifetime = 10.0F;
        eaw::ParticleWorld dimmed;
        const auto dim_id = dimmed.play(make_test_effect({steady}), eaw::visual_identity_transform, 8);
        dimmed.update(0.01F);
        std::vector<eaw::VisualParticle> full;
        dimmed.build_sprites({}, full);
        dimmed.set_intensity(dim_id, 0.0F);
        std::vector<eaw::VisualParticle> idle;
        dimmed.build_sprites({}, idle);
        require(full.size() == 1 && idle.size() == 1 && std::abs(idle[0].half_size - full[0].half_size * 0.55F) < 1e-4F && std::abs(idle[0].color[3] - full[0].color[3] * 0.45F) < 1e-4F,
                "particle intensity did not scale size and opacity");
    }

    // Particles linked to the system turn with their emitter under every profile (an engine glow on
    // a turning ship).
    auto glow = make_test_emitter();
    glow.use_bursts = true;
    glow.burst_count = 1;
    glow.link_to_system = true;
    glow.lifetime = 10.0F;
    glow.groups[2].type = 0;
    glow.groups[2].value = {10.0F, 0.0F, 0.0F};
    const auto turn = [&](const eaw::ParticleFidelity fidelity)
    {
        eaw::ParticleWorld turning;
        const auto glow_id = turning.play(make_test_effect({glow}, true, fidelity), eaw::visual_identity_transform, 3);
        turning.update(0.01F);
        // A quarter turn about +Z: X goes to Y.
        std::array<float, 16> turned = eaw::visual_identity_transform;
        turned[0] = 0.0F;
        turned[1] = 1.0F;
        turned[4] = -1.0F;
        turned[5] = 0.0F;
        turning.set_transform(glow_id, turned);
        turning.update(0.01F);
        std::vector<eaw::VisualParticle> turned_sprites;
        turning.build_sprites({0.0F, 0.0F, 100.0F}, turned_sprites);
        require(turned_sprites.size() == 1, "linked particle test lost its particle");
        return turned_sprites.front().position;
    };
    const auto rigid = turn(eaw::ParticleFidelity::enhanced);
    require(std::abs(rigid[0]) < 1e-4F && std::abs(rigid[1] - 10.0F) < 1e-4F, "enhanced linked particle did not turn with its emitter");
    const auto legacy_turned = turn(eaw::ParticleFidelity::legacy);
    require(std::abs(legacy_turned[0]) < 1e-4F && std::abs(legacy_turned[1] - 10.0F) < 1e-4F, "legacy linked particle did not turn with its emitter");

    // Additive blending: the original ignores alpha, the enhanced profile honours it.
    {
        const auto legacy = eaw::normalize_particle_emitter(make_test_emitter(), eaw::ParticleFidelity::legacy);
        const auto enhanced = eaw::normalize_particle_emitter(make_test_emitter(), eaw::ParticleFidelity::enhanced);
        require(legacy.blend == eaw::VisualParticleBlend::additive && enhanced.blend == eaw::VisualParticleBlend::additive_alpha, "additive interpretation failed");
        require(eaw::particle_interpretation(eaw::ParticleFidelity::legacy).draw_order == eaw::ParticleDrawOrder::emitter_list &&
                    eaw::particle_interpretation(eaw::ParticleFidelity::enhanced).draw_order == eaw::ParticleDrawOrder::back_to_front,
                "draw order interpretation failed");
    }

    // Emitters that emit from a mesh spawn on it: every vertex in turn, pushed out along the normal.
    auto nozzles = make_test_emitter();
    nozzles.use_bursts = true;
    nozzles.particles_per_burst = 1;
    nozzles.burst_count = 1;
    nozzles.emit_from_mesh = 3;
    nozzles.emit_from_mesh_offset = 2.0F;
    auto emit_mesh = std::make_shared<eaw::ParticleEmitMesh>();
    emit_mesh->positions = {{-50.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {50.0F, 0.0F, 0.0F}};
    emit_mesh->normals = {{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
    emit_mesh->indices = {0, 1, 2};
    eaw::ParticleWorld engines;
    engines.play(make_test_effect({nozzles}), eaw::visual_identity_transform, 2, emit_mesh);
    engines.update(0.01F);
    std::vector<eaw::VisualParticle> glows;
    engines.build_sprites({0.0F, 0.0F, 100.0F}, glows);
    std::vector<float> glow_x;
    for (const auto& sprite : glows)
    {
        glow_x.push_back(sprite.position[0]);
        require(std::abs(sprite.position[1] - 2.0F) < 1e-4F, "mesh emission ignored the normal offset");
    }
    std::ranges::sort(glow_x);
    require(glow_x == std::vector<float>{-50.0F, 0.0F, 50.0F}, "every-vertex mesh emission failed");

    // Heat particles have no pass yet: they are skipped, not drawn.
    auto heat = make_test_emitter();
    heat.heat = true;
    eaw::ParticleWorld hot;
    hot.play(make_test_effect({heat}), eaw::visual_identity_transform, 1);
    hot.update(0.1F);
    std::vector<eaw::VisualParticle> none;
    hot.build_sprites({}, none);
    require(none.empty() && hot.stats().skipped_emitters == 1, "heat particles were drawn");
}

const ModuleRegistrar registrar("engine", {
                                              {"test_intact_hidden_bones", &test_intact_hidden_bones},
                                              {"test_projectile_sprites", &test_projectile_sprites},
                                              {"test_particle_tracks", &test_particle_tracks},
                                              {"test_particle_world", &test_particle_world},
                                              {"test_asset_cache_dependencies", &test_asset_cache_dependencies},
                                              {"test_debug_draw", &test_debug_draw},
                                              {"test_scene_presentation", &test_scene_presentation},
                                              {"test_battle_orders", &test_battle_orders},
                                              {"test_battle_log", &test_battle_log},
                                              {"test_unit_animation", &test_unit_animation},
                                              {"test_runtime_spawnable_registration", &test_runtime_spawnable_registration},
                                              {"test_rts_control", &test_rts_control},
                                              {"test_terrain_height_field", &test_terrain_height_field},
                                              {"test_camera_controllers", &test_camera_controllers},
                                              {"test_visual_material_translation", &test_visual_material_translation},
                                              {"test_visual_terrain", &test_visual_terrain},
                                              {"test_combat_content", &test_combat_content},
                                              {"test_sandbox_options", &test_sandbox_options},
                                              {"test_model_forward_axis", &test_model_forward_axis},
                                          });

} // namespace
