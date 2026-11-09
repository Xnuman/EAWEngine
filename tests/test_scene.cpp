// Scene building from decoded assets: the asset cache, materials, terrain, and model visibility.
#include "test_support.hpp"

#include "battle.hpp"
#include "battle_effects.hpp"
#include "battle_log.hpp"
#include "particle_effects.hpp"
#include "projectile_visuals.hpp"
#include "scene_builder_internal.hpp"
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
    require(first.position == eaw::Vector3{0.0F, 0.0F, -0.5F} && third.position == eaw::Vector3{20.0F, 20.0F, 1.5F} && first.layers == std::array<std::uint32_t, 3>{0U, 0U, 0U} &&
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
                asset.scene.vertices[2].position == eaw::Vector3{40.0F, 20.0F, 1.5F} && water_material.blend == eaw::VisualBlendMode::alpha_blend &&
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

const ModuleRegistrar registrar("scene", {
                                             {"test_asset_cache_dependencies", &test_asset_cache_dependencies},
                                             {"test_visual_material_translation", &test_visual_material_translation},
                                             {"test_visual_terrain", &test_visual_terrain},
                                             {"test_intact_hidden_bones", &test_intact_hidden_bones},
                                         });

} // namespace
