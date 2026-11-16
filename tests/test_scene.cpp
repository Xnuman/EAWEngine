// Scene building from decoded assets: the asset cache, materials, terrain, and model visibility.
#include "test_support.hpp"

#include "assets/legacy_units.hpp"
#include "assets/model_geometry.hpp"
#include "battle/battle.hpp"
#include "battle/battle_log.hpp"
#include "presentation/battle_effects.hpp"
#include "presentation/particle_effects.hpp"
#include "presentation/projectile_visuals.hpp"
#include "presentation/unit_animation.hpp"
#include "scene/scene_builder_internal.hpp"
#include "sandbox/sandbox_options.hpp"

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
    require(material.diffuse == eaw::Color{0.25F, 0.5F, 0.75F, 0.123456789F} && material.specular == eaw::ColorRgb{1.0F, 1.0F, 1.0F} && material.emissive == eaw::ColorRgb{0.0F, 0.0F, 0.0F},
            "material colors were not taken from exact parameters with legacy defaults");
    require(material.base_color_texture == eaw::invalid_visual_texture && material.tint != eaw::Color{1.0F, 1.0F, 1.0F, 1.0F}, "untextured material did not receive a debug tint");
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
    // All zero but the last element: every vertex of a hidden bone collapses onto the origin.
    auto collapsed = eaw::Matrix4::zero();
    collapsed.values[15] = 1.0F;
    require(hidden_palette.size() == 2 && hidden_palette[1] == collapsed && hidden_palette[0].values[0] == 1.0F, "a bone hidden in the bind pose must collapse its attachments");
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
        const auto& spawnable = with_spawns.battle.spawnable;
        require(spawnable.size() == 1 && spawnable[0].type_name == "Test_Tank" && std::fabs(spawnable[0].max_speed - eaw::legacy_units::speed_per_frame(2.0F)) < 1.0e-4F &&
                    with_spawns.spawnable_looks.size() == 1 && with_spawns.spawnable_looks[0].mesh < with_spawns.scene.meshes.size(),
                "a spawn type must resolve to a mesh and its scaled speed");
        // The battle side alone is what the drawn build holds: no scene needed.
        const auto battle = expect_ok(eaw::build_battle_map(vfs, "Data/Art/Maps/TEST.TED", nullptr, types), "building the battle map");
        require(battle.land && battle.ground.valid() && battle.ground.heights == with_spawns.battle.ground.heights && battle.entities.size() == with_spawns.battle.entities.size() &&
                    battle.spawnable.size() == 1 && battle.spawnable[0].max_speed == spawnable[0].max_speed && battle.spawnable[0].radius == spawnable[0].radius,
                "the battle map must match the battle side of the drawn map");
        require(!eaw::build_battle_map(vfs, "Data/Art/Maps/MISSING.TED"), "a missing battle map must be reported");
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
        // Decoded textures only: computed ones (the terrain's sky visibility) are named by their texels.
        const auto decoded = [](const eaw::VisualScene& scene)
        {
            return std::ranges::find_if(scene.textures,
                                        [](const eaw::VisualTexture& texture)
                                        {
                                            return texture.name != "terrain sky visibility";
                                        });
        };
        require(decoded(uncached.scene) != uncached.scene.textures.end() && decoded(uncached.scene)->identity != decoded(first.scene)->identity, "a fresh decode must get a new identity");
    }
    const auto& terrain = asset.scene.terrain;
    require(asset.battle.land && terrain.layer_count == 3 && terrain.vertices.size() == 12 && terrain.first_material + 3 <= asset.scene.materials.size(), "terrain mesh structure is wrong");
    require(asset.scene.materials[terrain.first_material].tint[3] == 1.0F && asset.scene.materials[terrain.first_material + 2].tint[3] == 0.0F, "an untextured terrain layer must carry zero coverage");
    require(std::fabs(asset.scene.materials[terrain.first_material].uv_scale - 1.0F / 64.0F) < 1.0e-7F, "terrain layer tiling was not applied");
    // First triangle: (0,0), (1,0), (1,1) with layers {0, 0, 0} -> sample (1,1) is index 4, layer 0.
    const auto& first = terrain.vertices[0];
    const auto& third = terrain.vertices[2];
    require(first.position == eaw::Vector3{0.0F, 0.0F, -0.5F} && third.position == eaw::Vector3{20.0F, 20.0F, 1.5F} && first.layers == std::array<std::uint32_t, 3>{0U, 0U, 0U} &&
                first.barycentric == std::array<float, 3>{1.0F, 0.0F, 0.0F} && third.barycentric == std::array<float, 3>{0.0F, 0.0F, 1.0F},
            "terrain vertex positions, layers, or barycentrics are wrong");
    // Second cell's first triangle uses samples (1,0), (2,0), (2,1): layers {0, 1, 1}.
    require(terrain.vertices[6].layers == std::array<std::uint32_t, 3>{0U, 1U, 1U}, "terrain corner layers were not taken from the samples");
    require(terrain.bounds_minimum[2] == -0.5F && terrain.bounds_maximum[0] == 40.0F && terrain.bounds_maximum[2] == 2.0F && asset.scene.bounds_maximum[1] == 20.0F,
            "terrain bounds were not applied to the scene");
    require(!asset.lighting, "an environment without lighting fields produced a light rig");
    // Water at level 1.5 crosses the terrain (-0.5..2.0) and becomes a blended grid instance: 40 x 20
    // units in cells of about 12 (4 x 2 cells, 5 x 3 vertices), each vertex keeping the depth beneath
    // it (the level less the ground's height: 2.0 at the corner where the ground is -0.5).
    const auto& water_material = asset.scene.materials[asset.scene.draws[asset.scene.meshes[0].first_draw].material];
    constexpr std::size_t water_vertices = 15;
    constexpr std::size_t water_indices = 4 * 2 * 6;
    require(asset.water == eaw::TedWaterType::water && asset.scene.meshes.size() == 2 && asset.scene.instances.size() == 2 && asset.scene.vertices.size() == water_vertices + 6 &&
                asset.scene.vertices[water_vertices - 1].position == eaw::Vector3{40.0F, 20.0F, 1.5F} && asset.scene.vertices[0].texture_coordinates_1[0] == 2.0F &&
                water_material.blend == eaw::VisualBlendMode::alpha_blend && water_material.diffuse == eaw::Color{0.4F, 0.6F, 0.8F, 0.75F},
            "water surface was not built over the terrain");
    // The track (5,5)->(35,15), widths 10->6, opacity 0/1/0 becomes a 3-sample draped ribbon.
    const auto* ribbon = asset.scene.vertices.data() + water_vertices;
    require(asset.tracks == 1 && asset.scene.indices.size() == water_indices + 12 && ribbon[0].color[3] == 0.0F && ribbon[2].color[3] == 1.0F && ribbon[5].color[3] == 0.0F &&
                ribbon[0].texture_coordinates[0] == 0.0F && ribbon[1].texture_coordinates[0] == 1.0F,
            "track ribbon structure or opacity fade is wrong");
    // First left edge vertex lies at (6.581, 0.257): the lower-right triangle of cell (0,0) gives
    // height -0.3163, plus the 0.6 lift.
    require(std::fabs(ribbon[0].position[0] - 6.5811F) < 1.0e-3F && std::fabs(ribbon[0].position[2] - 0.2837F) < 2.0e-3F, "track ribbon does not follow the terrain surface");
    std::filesystem::remove_all(root, ignored);
}

void test_intact_hidden_bones()
{
    // A hardpoint scorch bone (and so its meshes) collapses in the visibility half while intact.
    auto model = std::make_shared<eaw::Skeleton>();
    model->bones.push_back({"Root", -1, eaw::Matrix4::identity(), true});
    model->bones.push_back({"HP_Blast", 0, eaw::Matrix4::identity(), true});
    eaw::VisualRig rig;
    rig.skeleton = model;
    rig.palette_size = 4;
    rig.intact_hidden = {false, true};
    std::vector<eaw::Matrix4> palette;
    eaw::append_rig_local_palette(rig, {}, palette);
    require(palette.size() == 4 && palette[2].values[0] == 1.0F && palette[3].values[0] == 0.0F && palette[3].values[5] == 0.0F && palette[1].values[0] == 1.0F,
            "intact-hidden bone was not collapsed");
}

// The drawn bounds gameplay measures (footprints, radii) and the drawn mesh share one rule: hidden
// meshes and collision geometry are left out, rigid meshes sit on their bone's bind pose, and an
// attached model (a hardpoint's) at its host bone counts in its intact state only.
void test_drawn_model_bounds()
{
    const auto part = [](const eaw::Vector3& corner, const eaw::LegacyMaterialFamily family = eaw::LegacyMaterialFamily::unknown)
    {
        eaw::ModelSubmesh submesh;
        submesh.material.family = family;
        submesh.vertices.resize(3);
        submesh.vertices[0].position = corner;
        submesh.vertices[1].position = {corner.x + 1.0F, corner.y, corner.z};
        submesh.vertices[2].position = {corner.x, corner.y + 1.0F, corner.z};
        submesh.indices = {0, 1, 2};
        return submesh;
    };
    auto shifted = eaw::Matrix4::identity();
    eaw::set_translation(shifted, {0.0F, 0.0F, 5.0F});
    eaw::Model host;
    host.skeleton.bones.push_back({"Root", -1, eaw::Matrix4::identity(), true});
    host.skeleton.bones.push_back({"Mount", 0, shifted, true});
    host.meshes = {eaw::ModelMesh{"hull", 0U, false, std::nullopt, std::nullopt, {part({0.0F, 0.0F, 0.0F})}},
                   eaw::ModelMesh{"hidden", 0U, true, std::nullopt, std::nullopt, {part({100.0F, 0.0F, 0.0F})}},
                   eaw::ModelMesh{"collision", 0U, false, std::nullopt, std::nullopt, {part({-100.0F, 0.0F, 0.0F}, eaw::LegacyMaterialFamily::collision)}},
                   eaw::ModelMesh{"turret", 1U, false, std::nullopt, std::nullopt, {part({2.0F, 0.0F, 0.0F})}}};
    auto attached = std::make_shared<eaw::Model>();
    attached->skeleton.bones.push_back({"Root", -1, eaw::Matrix4::identity(), true});
    attached->meshes = {eaw::ModelMesh{"gun", 0U, false, std::nullopt, std::nullopt, {part({0.0F, -3.0F, 0.0F})}},
                        eaw::ModelMesh{"gun_ALT1", 0U, false, 1U, std::nullopt, {part({0.0F, 50.0F, 0.0F})}}};
    const std::array attachments{eaw::ModelAttachment{"GUN.ALO", attached, 1U}};
    const auto bounds = eaw::drawn_model_bounds(host, attachments, true);
    require(bounds.minimum == eaw::Vector3{0.0F, -3.0F, 0.0F} && bounds.maximum == eaw::Vector3{3.0F, 1.0F, 5.0F},
            "drawn bounds must skip hidden and collision geometry, place rigid meshes on their bone, and add the intact attachment");
    require(eaw::ModelBounds{}.empty() && !bounds.empty(), "bounds without a vertex must be empty");
    eaw::Model invisible;
    invisible.skeleton.bones.push_back({"Root", -1, eaw::Matrix4::identity(), true});
    invisible.meshes = {eaw::ModelMesh{"collision", 0U, false, std::nullopt, std::nullopt, {part({0.0F, 0.0F, 0.0F}, eaw::LegacyMaterialFamily::collision)}}};
    auto broken = part({0.0F, 0.0F, 0.0F});
    broken.indices = {0, 1, 7};
    invisible.meshes.push_back(eaw::ModelMesh{"broken", 0U, false, std::nullopt, std::nullopt, {broken}});
    require(eaw::has_drawn_geometry(host, {}, true) && !eaw::has_drawn_geometry(invisible, {}, true), "only a drawn submesh with a whole triangle makes a model drawable (and a placed object exist)");

    eaw::VirtualFileSystem vfs;
    eaw::VisualScene scene;
    eaw::scene_build::SceneBuilder builder{vfs, scene};
    builder.textures = false;
    const auto mesh = expect_ok(eaw::scene_build::append_model(builder, host, "host", true, attachments), "appending the host");
    require(scene.meshes[mesh].bounds_minimum == bounds.minimum && scene.meshes[mesh].bounds_maximum == bounds.maximum, "the drawn mesh must have the gameplay bounds");
}

// A model's damage states and detail levels become runs of draws: untagged parts in every run,
// tagged ones only in their state or level, states from 0 up, levels most detailed (highest
// _LOD number) first, and the default range is state 0 at full detail. A model without tags keeps
// one plain range.
void test_mesh_states_and_levels()
{
    eaw::Model model;
    model.skeleton.bones.push_back({"Root", -1, eaw::Matrix4::identity(), true});
    const auto mesh = [](const char* name, std::optional<std::uint32_t> state, std::optional<std::uint32_t> level, const bool hidden = false)
    {
        eaw::ModelSubmesh part;
        part.vertices.resize(3);
        part.vertices[1].position = {1.0F, 0.0F, 0.0F};
        part.vertices[2].position = {0.0F, 1.0F, 0.0F};
        part.indices = {0, 1, 2};
        return eaw::ModelMesh{name, 0U, hidden, state, level, {part}};
    };
    model.meshes = {mesh("hull", std::nullopt, std::nullopt), mesh("dish_ALT0", 0U, std::nullopt), mesh("dish_ALT1", 1U, std::nullopt),
                    mesh("body_LOD2", std::nullopt, 2U),      mesh("body_LOD1", std::nullopt, 1U), mesh("ghost", std::nullopt, std::nullopt, true)};
    eaw::VirtualFileSystem vfs;
    eaw::VisualScene scene;
    eaw::scene_build::SceneBuilder builder{vfs, scene};
    builder.textures = false;
    const auto index = expect_ok(eaw::scene_build::append_model(builder, model, "tagged"), "appending the tagged model");
    const auto& visual = scene.meshes[index];
    require(visual.states.size() == 2 && visual.states[0].alternate == 0 && visual.states[1].alternate == 1 && visual.states[0].lods.size() == 2 && visual.states[1].lods.size() == 2,
            "two states with two levels each");
    const auto materials_of = [&](const eaw::VisualMeshLod& level)
    {
        return level.draw_count;
    };
    require(materials_of(visual.states[0].lods[0]) == 3 && materials_of(visual.states[1].lods[1]) == 3, "each run holds the hull, its state's part, and its level's part (hidden meshes never)");
    require(visual.first_draw == visual.states[0].lods[0].first_draw && visual.draw_count == 3, "the default range is state 0 at full detail");
    // The full-detail run of state 0 draws what LOD2 drew: the second-to-last draw of three is LOD2's.
    const auto& first = scene.draws[visual.states[0].lods[0].first_draw + 2];
    const auto& coarse = scene.draws[visual.states[0].lods[1].first_draw + 2];
    require(first.first_index != coarse.first_index, "the levels must draw different parts, most detailed first");

    eaw::Model plain;
    plain.skeleton = model.skeleton;
    plain.meshes = {mesh("hull", std::nullopt, std::nullopt)};
    const auto plain_index = expect_ok(eaw::scene_build::append_model(builder, plain, "plain"), "appending the plain model");
    require(scene.meshes[plain_index].states.empty() && scene.meshes[plain_index].draw_count == 1, "a model without tags must keep one plain range");
}

// Resources load once per type and key, failures stay cached, and a particle effect is cached per
// fidelity profile.
void test_resource_manager()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-resource-manager-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    const auto models = root / "Data" / "Art" / "Models";
    std::filesystem::create_directories(models);
    const auto model_bytes = make_test_alo();
    const auto particle_bytes = make_test_particle_system(-1);
    expect_ok(eaw::write_file(models / "TEST.ALO", model_bytes, eaw::WriteMode::replace), "writing the test model");
    expect_ok(eaw::write_file(models / "P_TEST.ALO", particle_bytes, eaw::WriteMode::replace), "writing the test effect");
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    eaw::AssetCache cache;
    const eaw::ResourceManager resources(vfs, &cache);

    const auto model = resources.get<eaw::Model>("DATA/ART/MODELS/TEST.ALO");
    require(model->ok() && !model->value().skeleton.bones.empty() && !model->value().meshes.empty(), "a model must load through its loader");
    require(resources.get<eaw::Model>("DATA/ART/MODELS/TEST.ALO") == model && cache.stats().hits == 1, "a second request must reuse the cached model");
    const auto missing = resources.get<eaw::Model>("DATA/ART/MODELS/MISSING.ALO");
    require(!missing->ok() && resources.get<eaw::Model>("DATA/ART/MODELS/MISSING.ALO") == missing, "a failed load must be cached with its error");
    const auto not_a_model = resources.get<eaw::Model>("DATA/ART/MODELS/P_TEST.ALO");
    require(!not_a_model->ok(), "a particle file must not load as a model");

    const auto legacy = resources.get<eaw::ParticleEffect>("DATA/ART/MODELS/P_TEST.ALO", eaw::ParticleFidelity::legacy);
    const auto enhanced = resources.get<eaw::ParticleEffect>("DATA/ART/MODELS/P_TEST.ALO", eaw::ParticleFidelity::enhanced);
    require(legacy->effect && enhanced->effect && legacy != enhanced && legacy->effect->fidelity == eaw::ParticleFidelity::legacy && enhanced->effect->fidelity == eaw::ParticleFidelity::enhanced &&
                legacy->effect->name == "P_TEST",
            "a particle effect must load per fidelity profile, named after its file");
    const auto mesh_as_effect = resources.get<eaw::ParticleEffect>("DATA/ART/MODELS/TEST.ALO", eaw::ParticleFidelity::legacy);
    require(!mesh_as_effect->effect && mesh_as_effect->model_file && mesh_as_effect->error.empty(), "a model file asked for as an effect must say so without an error");

    // Without a cache every request loads anew.
    const eaw::ResourceManager uncached(vfs);
    require(uncached.get<eaw::Model>("DATA/ART/MODELS/TEST.ALO")->ok() && uncached.get<eaw::Model>("DATA/ART/MODELS/TEST.ALO") != uncached.get<eaw::Model>("DATA/ART/MODELS/TEST.ALO"),
            "an uncached manager must load on every request");
    std::filesystem::remove_all(root, ignored);
}

// UVScrollRate fills the two scroll components and nothing after them (a four-component value
// once spilled into uv_scale).
void test_visual_material_uv_scroll()
{
    eaw::LegacyMaterial material;
    material.features.uv_animation = eaw::LegacyUvAnimation::scroll;
    material.parameters.push_back({.name = "UVScrollRate", .type = eaw::AloShaderParameterType::vector4, .value = "0.1 0.2 7 9", .values = {0.1F, 0.2F, 7.0F, 9.0F}});
    eaw::VirtualFileSystem vfs;
    eaw::VisualScene scene;
    eaw::scene_build::SceneBuilder builder{vfs, scene};
    builder.textures = false;
    const auto visual = eaw::scene_build::make_visual_material(builder, material);
    require(visual.uv_scroll == std::array<float, 2>{0.1F, 0.2F} && visual.uv_scale == 1.0F, "UVScrollRate must fill the two scroll components only");
}

// A planet material keeps its layers: the atmosphere, city color, power, and cloud scroll as authored,
// the legacy defaults where a parameter is missing.
void test_visual_material_planet()
{
    eaw::LegacyMaterial material;
    material.family = eaw::LegacyMaterialFamily::planet;
    material.features.lighting = eaw::LegacyLightingModel::planet_atmosphere;
    material.features.gloss = eaw::LegacyGlossSource::normal_alpha;
    material.parameters.push_back({.name = "Atmosphere", .type = eaw::AloShaderParameterType::vector4, .value = "0.2 0.4 0.8 0.5", .values = {0.2F, 0.4F, 0.8F, 0.5F}});
    material.parameters.push_back({.name = "CloudScrollRate", .type = eaw::AloShaderParameterType::scalar, .value = "0.01", .values = {0.01F}});
    eaw::VirtualFileSystem vfs;
    eaw::VisualScene scene;
    eaw::scene_build::SceneBuilder builder{vfs, scene};
    builder.textures = false;
    const auto visual = eaw::scene_build::make_visual_material(builder, material);
    require(visual.lighting == eaw::VisualLightingModel::planet && visual.gloss == eaw::VisualGlossSource::normal_alpha && visual.planet.atmosphere == eaw::Color{0.2F, 0.4F, 0.8F, 0.5F} &&
                visual.planet.cloud_scroll == 0.01F && visual.planet.atmosphere_power == 4.5F && visual.planet.city_color == eaw::ColorRgb{1.0F, 1.0F, 1.0F},
            "a planet material must keep its atmosphere and cloud scroll, with the legacy defaults for the rest");
}

// A shield material keeps its layers, its distortion layer taking the wave's scale and scroll and
// the other way round (as the legacy shader pairs them), defaults where a parameter is missing.
void test_visual_material_shield()
{
    eaw::LegacyMaterial material;
    material.family = eaw::LegacyMaterialFamily::shield;
    material.blend = eaw::LegacyBlendMode::additive;
    material.features.uv_animation = eaw::LegacyUvAnimation::shield_layers;
    material.parameters.push_back({.name = "Color", .type = eaw::AloShaderParameterType::vector4, .value = "0.5 0.6 1 1", .values = {0.5F, 0.6F, 1.0F, 1.0F}});
    material.parameters.push_back({.name = "WaveUVScale", .type = eaw::AloShaderParameterType::scalar, .value = "3", .values = {3.0F}});
    material.parameters.push_back({.name = "DistortUVScrollRate", .type = eaw::AloShaderParameterType::scalar, .value = "-0.5", .values = {-0.5F}});
    eaw::VirtualFileSystem vfs;
    eaw::VisualScene scene;
    eaw::scene_build::SceneBuilder builder{vfs, scene};
    builder.textures = false;
    const auto visual = eaw::scene_build::make_visual_material(builder, material);
    require(visual.lighting == eaw::VisualLightingModel::shield && visual.blend == eaw::VisualBlendMode::additive && visual.diffuse == eaw::Color{0.5F, 0.6F, 1.0F, 1.0F} &&
                visual.shield.distortion_uv_scale == 3.0F && visual.shield.wave_scroll == -0.5F && visual.shield.base_uv_scale == 20.0F && visual.shield.edge_brightness == 0.5F,
            "a shield material must keep its layers in the legacy shader's pairing");
}

// Billboard clump centers: each connected piece takes the mean x, y of its vertices.
void test_billboard_centers()
{
    std::vector<eaw::VisualVertex> vertices(8);
    const std::array<eaw::Vector3, 8> corners{
        {{0.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 4.0F}, {0.0F, 0.0F, 4.0F}, {10.0F, 6.0F, 0.0F}, {12.0F, 6.0F, 0.0F}, {12.0F, 6.0F, 4.0F}, {10.0F, 6.0F, 4.0F}}};
    for (std::size_t index = 0; index < vertices.size(); ++index)
    {
        vertices[index].position = corners[index];
    }
    const std::vector<std::uint32_t> indices{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 99, 1, 2};
    eaw::assign_billboard_centers(vertices, indices);
    require(vertices[0].texture_coordinates_1 == eaw::Vector2{1.0F, 0.0F} && vertices[3].texture_coordinates_1 == eaw::Vector2{1.0F, 0.0F} &&
                vertices[5].texture_coordinates_1 == eaw::Vector2{11.0F, 6.0F},
            "each billboard must turn about the center of its own vertices");
}

const ModuleRegistrar registrar("scene", {
                                             {"test_billboard_centers", &test_billboard_centers},
                                             {"test_visual_material_shield", &test_visual_material_shield},
                                             {"test_visual_material_planet", &test_visual_material_planet},
                                             {"test_mesh_states_and_levels", &test_mesh_states_and_levels},
                                             {"test_asset_cache_dependencies", &test_asset_cache_dependencies},
                                             {"test_resource_manager", &test_resource_manager},
                                             {"test_visual_material_translation", &test_visual_material_translation},
                                             {"test_visual_material_uv_scroll", &test_visual_material_uv_scroll},
                                             {"test_drawn_model_bounds", &test_drawn_model_bounds},
                                             {"test_visual_terrain", &test_visual_terrain},
                                             {"test_intact_hidden_bones", &test_intact_hidden_bones},
                                         });

} // namespace
