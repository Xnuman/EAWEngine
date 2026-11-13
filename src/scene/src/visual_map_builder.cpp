#include "core/string_utils.hpp"
#include "assets/map_object_layout.hpp"
#include "assets/spawn_catalog.hpp"
#include "scene/scene_builder_internal.hpp"

#include <algorithm>
#include <span>

namespace eaw {

using namespace scene_build;

namespace {

// The Particle objects a combat profile plays: its deaths (its own, its spin, its hardpoints') and
// its weapons' launches.
void collect_death_effect_keys(const CombatProfile& profile, std::vector<std::uint32_t>& keys)
{
    keys.push_back(profile.death_effect);
    keys.push_back(profile.spin_effect);
    for (const auto& weapon : profile.weapons)
    {
        keys.push_back(weapon.launch_effect);
    }
    for (const auto& part : profile.hardpoints)
    {
        if (part.death_effect != 0)
        {
            keys.push_back(part.death_effect);
        }
    }
}

} // namespace

void scene_build::add_spawnable_looks(SceneBuilder& builder, const GameObjectCatalog& catalog, std::unordered_map<std::string, std::optional<std::uint32_t>>& meshes,
                                      const std::span<const SpawnableType> types, const std::span<const BuiltModel> models, std::vector<SpawnableLook>& looks, const bool land)
{
    for (auto index = looks.size(); index < types.size(); ++index)
    {
        const auto& type = types[index];
        const auto& model = models[index];
        SpawnableLook look;
        if (model.path.empty())
        {
            // A company (its members come before it).
            look.mesh = looks[type.members.front()].mesh;
            look.hidden = looks[type.members.front()].hidden;
            looks.push_back(look);
            continue;
        }
        auto [entry, inserted] = meshes.try_emplace(attached_mesh_key(model.path, model.attachments));
        if (inserted)
        {
            entry->second = append_scene_model(builder, model.path, model.attachments);
        }
        note_animation_source(builder, model.path, catalog, *model.type);
        note_damage_bones(builder, model.path, type.combat);
        if (entry->second)
        {
            look.mesh = *entry->second;
        }
        else
        {
            builder.scene.warnings.push_back(model.path + ": has drawable geometry but could not be added to the scene; '" + type.type_name + "' spawns undrawn");
            look.hidden = true;
        }
        look.team_color = catalog.affiliation_color(*model.type).value_or(player_team_color(1));
        look.scale = model.scale;
        look.lod_bias = type_lod_bias(catalog, *model.type);
        look.receives_shadows = type_receives_shadows(catalog, *model.type);
        look.death = type_death_style(catalog, *model.type);
        look.bank_limit = type_bank_limit(catalog, *model.type);
        look.animation = type_animation_profile(catalog, *model.type);
        if (land)
        {
            look.damage = type_damage_states(catalog, *model.type, &builder.scene.warnings);
        }
        looks.push_back(look);
    }
}

Result<VisualMapAsset> build_visual_map(const AssetReader& vfs, const std::string_view map_path, AssetCache* cache, const std::span<const std::string> spawn_types, const MapBuildOptions& options)
{
    const auto resolved = vfs.resolve(map_path);
    if (!resolved)
    {
        return make_error("map was not found: " + std::string(map_path));
    }
    // The walk and the scene share decoded models; without a cache of the caller's, one for this
    // build.
    AssetCache build_cache;
    if (cache == nullptr)
    {
        cache = &build_cache;
    }
    const ResourceManager resources(vfs, cache);
    VisualMapAsset asset;
    const auto map_entry = resources.get<TedMapData>(resolved->virtual_path);
    if (!map_entry->ok())
    {
        return with_context(resolved->virtual_path, map_entry->error());
    }
    const auto& map = map_entry->value();
    const auto catalog_entry = resources.get<CachedCatalog>(xml_catalog_path);
    const auto& catalog = catalog_entry->catalog;
    const auto layout = build_map_object_layout(map, catalog, vfs);
    const auto movement_entry = resources.get<MovementClasses>(movement_class_path);
    const auto combat_entry = resources.get<CombatContent>(combat_content_path);
    auto& battle = asset.battle;
    battle.name = resolved->virtual_path;
    std::vector<BuiltModel> entity_models;
    std::vector<BuiltModel> spawnable_models;
    std::vector<BuiltModel> decoration_models;
    build_battle_map(resources, {map, catalog, layout, *movement_entry, *combat_entry}, spawn_types, battle, entity_models, spawnable_models, &decoration_models);

    auto& scene = asset.scene;
    scene.name = resolved->virtual_path;
    scene.warnings = battle.warnings;
    SceneBuilder builder{vfs, scene, {}, cache, options.textures, options.animation_clips};
    builder.particle_fidelity = options.particle_fidelity;
    if (battle.land)
    {
        append_terrain(builder, map);
        asset.water = append_water(builder, map).has_value() ? map.water.type : TedWaterType::none;
        asset.tracks = append_tracks(builder, map);
    }
    std::unordered_map<std::string, std::optional<std::uint32_t>> meshes;
    // How a placed model is drawn: its mesh (once per model and attachments), its instance, and its
    // look; `combat` (null for decorations) names its damage bones.
    const auto draw_placed = [&](const BuiltModel& model, const CombatProfile* combat)
    {
        auto [entry, inserted] = meshes.try_emplace(attached_mesh_key(model.path, model.attachments));
        if (inserted)
        {
            entry->second = append_scene_model(builder, model.path, model.attachments);
        }
        MapEntityLook look;
        if (model.type != nullptr)
        {
            note_animation_source(builder, model.path, catalog, *model.type);
            if (combat != nullptr)
            {
                note_damage_bones(builder, model.path, *combat);
            }
            look.death = type_death_style(catalog, *model.type);
            look.bank_limit = type_bank_limit(catalog, *model.type);
            look.hidden = type_is_marker(catalog, *model.type);
            look.animation = type_animation_profile(catalog, *model.type);
            // Land_Damage_*: the land model's states.
            if (battle.land)
            {
                look.damage = type_damage_states(catalog, *model.type, &scene.warnings);
            }
        }
        if (!entry->second)
        {
            // The object exists (its model has geometry) but is not drawn.
            scene.warnings.push_back(model.path + ": has drawable geometry but could not be added to the scene; the object is not drawn");
            look.hidden = true;
            ++asset.undrawn_entities;
            return look;
        }
        // Instances start in their mesh's rest pose (build_map_rigs sets the bone offsets).
        VisualInstance instance{*entry->second, model.transform};
        instance.lod_bias = model.type != nullptr ? type_lod_bias(catalog, *model.type) : 1.0F;
        instance.receives_shadows = model.type == nullptr || type_receives_shadows(catalog, *model.type);
        if (const auto faction = model.type != nullptr ? catalog.affiliation_color(*model.type) : std::nullopt)
        {
            instance.team_color = *faction;
            ++asset.faction_colored;
        }
        else
        {
            instance.team_color = player_team_color(model.player);
            ++asset.player_colored;
        }
        look.instance = static_cast<std::uint32_t>(scene.instances.size());
        scene.instances.push_back(instance);
        return look;
    };
    for (std::size_t index = 0; index < battle.entities.size(); ++index)
    {
        asset.entity_looks.push_back(draw_placed(entity_models[index], &battle.entities[index].spawn.combat));
    }
    for (const auto& model : decoration_models)
    {
        asset.decorations.push_back({draw_placed(model, nullptr), translation(model.transform), model_forward_heading(model.transform), model.player});
    }

    // Backdrop: the active environment names primary and secondary sky domes. Each name is an XML
    // object (SpacePrimarySkydome, LandPrimarySkydome, ...) whose model is drawn as a camera-
    // centered background layer, primary first. The stored index can be out of range in
    // malformed data; the first environment is used then.
    if (!map.environments.empty())
    {
        auto environment_index = static_cast<std::size_t>(map.active_environment);
        if (environment_index >= map.environments.size())
        {
            scene.warnings.push_back("active environment " + std::to_string(map.active_environment) + " is out of range; using environment 0");
            environment_index = 0;
        }
        const auto& environment = map.environments[environment_index];
        asset.environment_name = environment.name;
        // Fog the environment switches on, with a usable range.
        const bool fog_usable = environment.has_fog && environment.fog_enabled && std::isfinite(environment.fog_start) && std::isfinite(environment.fog_end) &&
                                environment.fog_end > environment.fog_start &&
                                std::ranges::all_of(environment.fog_color,
                                                    [](const float channel)
                                                    {
                                                        return std::isfinite(channel);
                                                    });
        if (environment.has_lighting)
        {
            const auto rig = environment_light_rig(environment);
            if (light_rig_valid(rig))
            {
                asset.lighting = rig;
            }
            else
            {
                scene.warnings.push_back("environment '" + environment.name + "' has non-finite lighting; using the engine default");
            }
        }
        if (fog_usable)
        {
            // On the engine's default lights when the environment's are unusable.
            if (!asset.lighting)
            {
                asset.lighting = VisualLightRig{};
            }
            asset.lighting->fog = {true, {environment.fog_color[0], environment.fog_color[1], environment.fog_color[2]}, environment.fog_start, environment.fog_end};
        }
        const auto context = battle.land ? GameModelContext::land : GameModelContext::space;
        for (const auto& name : {environment.skybox_primary, environment.skybox_secondary})
        {
            if (name.empty())
            {
                continue;
            }
            const XmlDefinition* definition = nullptr;
            std::optional<GameObjectModelReference> model;
            for (const auto* candidate : catalog.find(name))
            {
                if ((model = catalog.model(*candidate, context)))
                {
                    definition = candidate;
                    break;
                }
            }
            const auto path = model ? resolve_model_path(vfs, model->value) : std::nullopt;
            if (definition == nullptr || !path)
            {
                scene.warnings.push_back("backdrop '" + name + "' has no resolvable " + std::string(to_string(context)) + " model");
                continue;
            }
            auto [entry, inserted] = meshes.try_emplace(*path);
            if (inserted)
            {
                entry->second = append_scene_model(builder, *path);
            }
            if (!entry->second)
            {
                continue;
            }
            VisualInstance backdrop{*entry->second};
            backdrop.background = true;
            scene.instances.push_back(backdrop);
            asset.backdrops.push_back(definition->name + " (" + *path + ")");
        }
    }

    // Types requested for runtime spawning: their models join the mesh library without instances.
    add_spawnable_looks(builder, catalog, meshes, battle.spawnable, spawnable_models, asset.spawnable_looks, battle.land);

    // How shots look; model projectiles add meshes, so this comes before the rigs (headless builds
    // skip it, so the simulation's meshes never depend on it).
    if (options.textures)
    {
        std::vector<std::uint32_t> keys;
        for (const auto& entity : battle.entities)
        {
            collect_projectile_keys(entity.spawn.combat, keys);
        }
        for (const auto& type : battle.spawnable)
        {
            collect_projectile_keys(type.combat, keys);
        }
        load_projectile_visuals(builder, catalog, keys, asset.projectile_visuals, asset.projectile_atlas);
        load_projectile_models(builder, catalog, battle.land, meshes, asset.projectile_visuals);
    }
    build_map_rigs(builder, asset.rigs, asset.rest_palette);
    if (options.textures)
    {
        for (const auto& name : options.effects)
        {
            asset.effects.push_back(particle_effect(builder, name));
        }
        std::vector<std::uint32_t> keys;
        collect_impact_effect_keys(asset.projectile_visuals, keys);
        for (const auto& entity : battle.entities)
        {
            collect_death_effect_keys(entity.spawn.combat, keys);
        }
        for (const auto& type : battle.spawnable)
        {
            collect_death_effect_keys(type.combat, keys);
        }
        for (const auto& look : asset.entity_looks)
        {
            keys.push_back(look.death.end_effect);
        }
        for (const auto& look : asset.spawnable_looks)
        {
            keys.push_back(look.death.end_effect);
        }
        load_effect_objects(builder, catalog, keys, battle.land, asset.effect_objects);
    }
    asset.particle_textures = std::move(builder.particles.textures);
    if (scene.terrain.vertices.empty() && std::ranges::none_of(scene.instances,
                                                               [](const VisualInstance& instance)
                                                               {
                                                                   return !instance.background;
                                                               }))
    {
        return make_error("map has no placed objects with renderable models: " + resolved->virtual_path);
    }
    update_scene_bounds(scene);
    return asset;
}

Result<SpawnablePackage> build_spawnable_package(const AssetReader& vfs, const std::string_view type_name, const bool land, AssetCache* cache, const MapBuildOptions& options)
{
    AssetCache build_cache;
    if (cache == nullptr)
    {
        cache = &build_cache;
    }
    const ResourceManager resources(vfs, cache);
    const auto catalog_entry = resources.get<CachedCatalog>(xml_catalog_path);
    const auto movement_entry = resources.get<MovementClasses>(movement_class_path);
    const auto combat_entry = resources.get<CombatContent>(combat_content_path);
    SpawnablePackage package;
    package.meshes.name = std::string(type_name);
    std::unordered_map<const XmlDefinition*, CombatProfile> profiles;
    const auto profile_of = [&](const XmlDefinition& definition, const Model* model) -> const CombatProfile&
    {
        auto found = profiles.find(&definition);
        if (found == profiles.end())
        {
            std::vector<std::string> scale_warnings;
            found = profiles
                        .emplace(&definition, combat_profile(catalog_entry->catalog, definition, *combat_entry, package.meshes.warnings, model != nullptr ? &model->skeleton : nullptr,
                                                             type_scale_factor(catalog_entry->catalog, definition, scale_warnings), combat_entry->max_health_multiplier.of(land)))
                        .first;
        }
        return found->second;
    };
    std::vector<BuiltModel> models;
    SpawnableBuild build{resources, catalog_entry->catalog, movement_entry->classes, land ? GameModelContext::land : GameModelContext::space, land, profile_of, package.types,
                         models,    package.meshes.warnings};
    build.speed_multiplier = combat_entry->max_speed_multiplier.of(land);
    build.combat = &*combat_entry;
    const auto index = add_spawnable_type(build, std::string(type_name), 0);
    SceneBuilder builder{vfs, package.meshes, {}, cache, options.textures, options.animation_clips};
    builder.particle_fidelity = options.particle_fidelity;
    std::unordered_map<std::string, std::optional<std::uint32_t>> meshes;
    add_spawnable_looks(builder, catalog_entry->catalog, meshes, package.types, models, package.looks, land);
    if (!index || package.meshes.meshes.empty())
    {
        return make_error("spawn type '" + std::string(type_name) + "' has no resolvable " + std::string(land ? "land" : "space") + " model");
    }
    // The requested type comes last (members are added before their company).
    if (*index + 1U != package.types.size())
    {
        return make_error("spawn type '" + std::string(type_name) + "' did not resolve to a single new type");
    }
    if (options.textures)
    {
        std::vector<std::uint32_t> keys;
        for (const auto& type : package.types)
        {
            collect_projectile_keys(type.combat, keys);
        }
        load_projectile_visuals(builder, catalog_entry->catalog, keys, package.projectile_visuals, package.projectile_atlas);
        load_projectile_models(builder, catalog_entry->catalog, land, meshes, package.projectile_visuals);
    }
    build_map_rigs(builder, package.rigs, package.rest_palette);
    if (options.textures)
    {
        std::vector<std::uint32_t> keys;
        collect_impact_effect_keys(package.projectile_visuals, keys);
        for (const auto& type : package.types)
        {
            collect_death_effect_keys(type.combat, keys);
        }
        for (const auto& look : package.looks)
        {
            keys.push_back(look.death.end_effect);
        }
        load_effect_objects(builder, catalog_entry->catalog, keys, land, package.effect_objects);
    }
    package.particle_textures = std::move(builder.particles.textures);
    return package;
}

} // namespace eaw
