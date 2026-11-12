#pragma once

#include "math/scalar.hpp"
#include "formats/ted_map.hpp"
#include "render/visual_scene.hpp"
#include "assets/pose.hpp"
#include "assets/spawn_catalog.hpp"
#include "simulation/simulation.hpp"
#include "engine/battle_map.hpp"
#include "engine/entity_look.hpp"
#include "engine/object_types.hpp"
#include "engine/particle_effects.hpp"
#include "engine/projectile_visuals.hpp"
#include "engine/visual_rig.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

class AssetReader;
class AssetCache;

struct VisualModelAsset
{
    VisualScene scene;
    Skeleton skeleton;
    std::vector<Matrix4> inverse_bind_pose;
    // The scene's palette has a second, visibility-collapsed half for rigid attachments (see
    // sample_skin_palette).
    bool visibility_palette{};
};

// A stock or mod map built for drawing: its scene, its battle side, and how each entity and type is
// drawn, with counts of what could not be drawn and why.
struct VisualMapAsset
{
    VisualScene scene;
    // The battle side, built first (build_battle_map's walk); the rest is how it is drawn.
    BattleMapAsset battle;
    // Entities whose model has geometry but could not be added to the scene (hidden).
    std::size_t undrawn_entities{};
    // Resolved backdrop layers ("definition (model path)"), in draw order.
    std::vector<std::string> backdrops;
    // Lighting of the active environment, when it carries a complete lighting block.
    std::optional<VisualLightRig> lighting;
    std::string environment_name;
    // Instances colored by their type's affiliated faction versus the player-slot fallback.
    // Liquid surface added over the terrain (none when the map has none or it is not visible).
    TedWaterType water{TedWaterType::none};
    // Track ribbons drawn over the terrain.
    std::size_t tracks{};
    std::size_t faction_colored{};
    std::size_t player_colored{};
    // How each of battle.entities and battle.spawnable is drawn (same index).
    std::vector<MapEntityLook> entity_looks;
    std::vector<SpawnableLook> spawnable_looks;
    // One rig per scene mesh (same index), and the scene palette of every mesh's rest pose; each
    // instance's bone_offset is its mesh's rest offset. Upload it with the scene (the renderer
    // resets its palette to identity on load).
    std::vector<VisualRig> rigs;
    std::vector<Matrix4> rest_palette;
    // Textures of the rigs' and named effects' particle effects, as in SpawnablePackage.
    std::vector<VisualTexture> particle_textures;
    // The effects MapBuildOptions::effects named, in order (null where one could not be loaded).
    std::vector<std::shared_ptr<const ParticleEffect>> effects;
    // How shots look, as in SpawnablePackage.
    ProjectileVisualTable projectile_visuals;
    std::uint32_t projectile_atlas{invalid_visual_texture};
    // The Particle objects shot impacts and deaths name, as in SpawnablePackage.
    ParticleEffectObjectTable effect_objects;
};

// Builders read through `reader` (pass a RecordingAssetReader to collect the scene's file
// dependencies). With a cache, decoded models, textures, maps, and the XML object catalog are
// reused across builds until their source files change. A model or map that cannot be built (not
// found, malformed, or without renderable geometry) returns an error naming its virtual path;
// problems that only drop parts of a scene are listed in its warnings instead.
[[nodiscard]] Result<VisualModelAsset> build_visual_model(const AssetReader& reader, std::string_view model_path, AssetCache* cache = nullptr);
// `spawn_types` names XML object types whose models are added to the scene's meshes so they can be
// spawned at runtime; types without a model for the map's context are reported as warnings.
// What a map build decodes besides geometry and game data. Headless battles need neither textures
// nor animation clips; the scene then has no textures and its rigs no clips.
struct MapBuildOptions
{
    bool textures{true};
    bool animation_clips{true};
    // Particle effects to load by name (model references, as proxies name them) into
    // VisualMapAsset::effects, for viewing (sandbox --effect).
    std::vector<std::string> effects;
    // How particle effects are interpreted (docs/architecture.md, "Content contract versus legacy
    // implementation").
    ParticleFidelity particle_fidelity{ParticleFidelity::legacy};
};
[[nodiscard]] Result<VisualMapAsset> build_visual_map(const AssetReader& reader, std::string_view map_path, AssetCache* cache = nullptr, std::span<const std::string> spawn_types = {},
                                                      const MapBuildOptions& options = {});
// A spawnable type built on its own, to add to a loaded map at runtime: its meshes as a fragment for
// D3d12Renderer::add_meshes (no instances or terrain), their rigs and rest palette (rest offsets
// relative to it), and the types: the requested one last, after any company or squadron members,
// with mesh indices relative to the fragment and member indices relative to `types`.
struct SpawnablePackage
{
    VisualScene meshes;
    std::vector<VisualRig> rigs;
    std::vector<Matrix4> rest_palette;
    std::vector<SpawnableType> types;
    std::vector<SpawnableLook> looks;
    // Textures of the rigs' particle effects (register with D3d12Renderer::add_textures and renumber
    // the effects with renumber_effect_textures).
    std::vector<VisualTexture> particle_textures;
    // How the types' shots look, and the bolt atlas's index in particle_textures (or
    // invalid_visual_texture).
    ProjectileVisualTable projectile_visuals;
    std::uint32_t projectile_atlas{invalid_visual_texture};
    // The XML Particle objects the types' shot impacts and deaths name (effects index
    // particle_textures).
    ParticleEffectObjectTable effect_objects;
};
// Builds a type for a land or space map through the same cached XML catalog, movement classes, and
// combat data as build_visual_map (an error when the type has no model for that context).
[[nodiscard]] Result<SpawnablePackage> build_spawnable_package(const AssetReader& reader, std::string_view type_name, bool land, AssetCache* cache = nullptr, const MapBuildOptions& options = {});
// Points the rigs' effects at renderer texture indices: `renderer_textures[i]` is where the
// renderer placed particle texture i (D3d12Renderer::add_textures). Effects shared between rigs stay
// shared.
void renumber_effect_textures(std::span<VisualRig> rigs, std::span<std::shared_ptr<const ParticleEffect>> effects, std::span<const std::uint32_t> renderer_textures,
                              ParticleEffectObjectTable* objects = nullptr, ProjectileVisualTable* projectiles = nullptr);
[[nodiscard]] std::optional<std::string> default_model_animation(const AssetReader& reader, std::string_view model_path);
// Every <MODEL>_*.ALA beside the model, sorted by virtual path.
[[nodiscard]] std::vector<std::string> model_animations(const AssetReader& reader, std::string_view model_path);
// Skin matrices for the model's bones (bind pose without an animation). With a visibility palette,
// a second half repeats them, collapsed to a point for bones the pose hides.
[[nodiscard]] std::vector<Matrix4> sample_skin_palette(const VisualModelAsset& asset, const Animation* animation, float seconds);

} // namespace eaw
