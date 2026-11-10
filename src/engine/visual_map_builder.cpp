#include "scene_builder_internal.hpp"

#include "combat_content.hpp"
#include "assets/spawn_catalog.hpp"
#include "formats/dds_texture.hpp"
#include "assets/map_object_layout.hpp"
#include "formats/xml_document.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <functional>
#include <atomic>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <limits>
#include <ranges>
#include <span>
#include <system_error>

namespace eaw {

using namespace scene_build;

namespace {

// Lays each company's members (consecutive entries with one object index) out with the formation
// used for group orders, centered on the company's placed position and facing its heading, and
// moves their instances and spawns there (heights from the ground surface). The original arranges
// companies at runtime; this layout is an engine choice.
template <class Member> void arrange_companies(VisualScene& scene, VisualMapAsset& asset, const std::vector<Member>& members, const GameObjectCatalog& catalog)
{
    for (std::size_t first = 0; first < members.size();)
    {
        auto last = first;
        while (last < members.size() && members[last].object_index == members[first].object_index)
        {
            ++last;
        }
        const auto& origin = scene.instances[members[first].instance].transform;
        const auto center = translation(origin);
        const auto facing = model_forward_heading(origin);
        std::vector<FormationMember> formation;
        for (auto index = first; index < last; ++index)
        {
            const auto& member = members[index];
            const auto& spawn = asset.entities[member.entity].spawn;
            const auto radius = spawn.radius > 0.0F ? spawn.radius : mesh_footprint_radius(scene, scene.instances[member.instance].mesh, scene.instances[member.instance].transform);
            formation.push_back({EntityId{static_cast<std::uint32_t>(index + 1U), 1}, center, radius, type_formation_spacing(catalog, *member.type), type_formation_order(catalog, *member.type)});
        }
        const auto slots = formation_slots(formation, center, facing);
        for (auto index = first; index < last; ++index)
        {
            auto position = slots[index - first].position;
            if (asset.ground.valid())
            {
                position.z = asset.ground.surface_height(position.x, position.y);
            }
            auto& transform = scene.instances[members[index].instance].transform;
            set_translation(transform, position);
            asset.entities[members[index].entity].spawn.position = position;
        }
        ++asset.expanded_companies;
        first = last;
    }
}

} // namespace

std::optional<std::size_t> scene_build::add_spawnable_type(SpawnableBuild& build, const std::string& type_name, const int depth)
{
    for (std::size_t index = 0; index < build.spawnable.size(); ++index)
    {
        if (ascii_lower(build.spawnable[index].type_name) == ascii_lower(type_name))
        {
            return index;
        }
    }

    // A company is its members even when it names a model of its own.
    const auto is_company = std::ranges::any_of(build.catalog.find(type_name),
                                                [&](const XmlDefinition* candidate)
                                                {
                                                    return build.catalog.field(*candidate, "Company_Units").field != nullptr || build.catalog.field(*candidate, "Squadron_Units").field != nullptr;
                                                });
    const XmlDefinition* definition = nullptr;
    std::optional<GameObjectModelReference> model;
    for (const auto* candidate : build.catalog.find(type_name))
    {
        if ((model = build.catalog.model(*candidate, build.context)))
        {
            definition = candidate;
            break;
        }
    }
    const auto path = model ? resolve_model_path(build.builder.vfs, model->value) : std::nullopt;
    if (definition == nullptr || !path || (is_company && depth == 0))
    {
        // A company spawns its members instead (each resolved like a requested type).
        for (const auto* candidate : build.catalog.find(type_name))
        {
            // Land companies and space squadrons both spawn as their members (a repeated
            // Squadron_Units continues the list).
            std::vector<std::string_view> names;
            if (const auto* company_units = build.catalog.field(*candidate, "Company_Units").field)
            {
                names = split_type_names(company_units->text);
            }
            for (const auto* squadron_units : build.catalog.fields(*candidate, "Squadron_Units").fields)
            {
                const auto more = split_type_names(squadron_units->text);
                names.insert(names.end(), more.begin(), more.end());
            }
            if (names.empty() || depth > 0)
            {
                continue;
            }
            SpawnableType company;
            company.type_name = candidate->name;
            for (const auto member : names)
            {
                if (const auto index = add_spawnable_type(build, std::string(member), depth + 1))
                {
                    company.members.push_back(*index);
                }
            }
            if (company.members.empty())
            {
                break;
            }
            company.mesh = build.spawnable[company.members.front()].mesh;
            build.spawnable.push_back(std::move(company));
            return build.spawnable.size() - 1U;
        }
        build.builder.scene.warnings.push_back("spawn type '" + type_name + "' has no resolvable " + std::string(to_string(build.context)) + " model");
        return std::nullopt;
    }
    const auto host = cached_model(build.builder, *path);
    const auto attachments = type_hardpoint_models(build.builder, build.catalog, *definition, host->ok() ? &host->value() : nullptr);
    auto [entry, inserted] = build.meshes.try_emplace(attached_mesh_key(*path, attachments));
    if (inserted)
    {
        entry->second = append_scene_model(build.builder, *path, attachments);
    }
    note_animation_source(build.builder, *path, build.catalog, *definition);
    if (!entry->second)
    {
        return std::nullopt;
    }
    SpawnableType spawnable;
    spawnable.type_name = definition->name;
    spawnable.mesh = *entry->second;
    spawnable.max_speed = type_max_speed(build.catalog, *definition, build.builder.scene.warnings);
    spawnable.team_color = build.catalog.affiliation_color(*definition).value_or(player_team_color(1));
    spawnable.follows_ground = build.land && type_follows_ground(build.catalog, *definition);
    spawnable.passable_types = spawnable.follows_ground ? type_passable_types(build.catalog, *definition, build.movement_classes) : 0U;
    const auto decoded = cached_model(build.builder, *path);
    spawnable.combat = build.profile_of(*definition, decoded && decoded->ok() ? &decoded->value() : nullptr);
    note_damage_bones(build.builder, *path, spawnable.combat);
    spawnable.hover_offset = type_float_field(build.catalog, *definition, "Hover_Offset");
    spawnable.scale = type_scale_factor(build.catalog, *definition, build.builder.scene.warnings);
    spawnable.death = type_death_style(build.catalog, *definition);
    if (spawnable.max_speed > 0.0F)
    {
        const auto soft_radius = type_soft_footprint_radius(build.catalog, *definition);
        spawnable.radius = soft_radius > 0.0F ? soft_radius : mesh_footprint_radius(build.builder.scene, spawnable.mesh, scaled_model_transform(Matrix4::identity(), spawnable.scale));
        spawnable.turn_rate = type_turn_rate(build.catalog, *definition);
        spawnable.min_speed = build.land ? 0.0F : type_min_flight_speed(build.catalog, *definition, spawnable.max_speed);
        spawnable.aims_with_body = type_aims_with_body(build.catalog, *definition);
        spawnable.formation_order = type_formation_order(build.catalog, *definition);
        spawnable.formation_spacing = type_formation_spacing(build.catalog, *definition);
    }
    build.spawnable.push_back(std::move(spawnable));
    return build.spawnable.size() - 1U;
}

Result<VisualMapAsset> build_visual_map(const AssetReader& vfs, const std::string_view map_path, AssetCache* cache, const std::span<const std::string> spawn_types, const MapBuildOptions& options)
{
    const auto resolved = vfs.resolve(map_path);
    if (!resolved)
    {
        return make_error("map was not found: " + std::string(map_path));
    }
    VisualMapAsset asset;
    const auto map_entry = cached<Result<TedMapData>>(cache, "ted", resolved->virtual_path, vfs,
                                                      [&](const AssetReader& reader) -> Result<TedMapData>
                                                      {
                                                          EAW_TRY_ASSIGN(const auto bytes, reader.read(resolved->virtual_path));
                                                          return read_ted(bytes);
                                                      });
    if (!map_entry->ok())
    {
        return with_context(resolved->virtual_path, map_entry->error());
    }
    const auto& map = map_entry->value();
    asset.land = map.type == TedMapType::land;
    const auto catalog_entry = cached<CachedCatalog>(cache, "xml-catalog", "Data/XML", vfs,
                                                     [](const AssetReader& reader)
                                                     {
                                                         auto database = std::make_unique<XmlAssetDatabase>(XmlAssetDatabase::build(reader));
                                                         auto catalog = GameObjectCatalog::build(*database);
                                                         return CachedCatalog{std::move(database), std::move(catalog)};
                                                     });
    const auto& catalog = catalog_entry->catalog;
    const auto layout = build_map_object_layout(map, catalog, vfs);
    const auto movement_entry = cached<MovementClasses>(cache, "movement-classes", std::string(movement_class_path), vfs, load_movement_classes);
    const auto& movement_classes = movement_entry->classes;
    const auto combat_entry = cached<CombatContent>(cache, "combat-content", "Data/XML/GameConstants.xml", vfs, load_combat_content);
    asset.damage_table = combat_entry->damage_table;
    // One profile per type, so a type's warnings are reported once however often it is placed.
    std::unordered_map<const XmlDefinition*, CombatProfile> combat_profiles;
    const auto profile_of = [&](const XmlDefinition& definition, const AloModelData* model) -> const CombatProfile&
    {
        auto found = combat_profiles.find(&definition);
        if (found == combat_profiles.end())
        {
            std::vector<std::string> scale_warnings;
            found = combat_profiles.emplace(&definition, combat_profile(catalog, definition, *combat_entry, asset.scene.warnings, model, type_scale_factor(catalog, definition, scale_warnings))).first;
        }
        return found->second;
    };
    asset.untyped = layout.untyped;
    asset.without_model = layout.without_model;
    asset.missing_model_files = layout.missing_model_files;
    asset.expanded_members = layout.expanded_members;
    asset.company_members = layout.company_members;
    asset.unexpanded_containers = layout.unexpanded_containers;

    auto& scene = asset.scene;
    scene.name = resolved->virtual_path;
    SceneBuilder builder{vfs, scene, {}, cache, options.textures, options.animation_clips};
    builder.particle_fidelity = options.particle_fidelity;
    scene.warnings.insert(scene.warnings.end(), combat_entry->warnings.begin(), combat_entry->warnings.end());
    if (asset.land)
    {
        scene.warnings.insert(scene.warnings.end(), movement_entry->warnings.begin(), movement_entry->warnings.end());
        append_terrain(builder, map);
        asset.water = append_water(builder, map).has_value() ? map.water.type : TedWaterType::none;
        const auto samples = static_cast<std::size_t>(map.terrain_width) * map.terrain_height;
        if (map.terrain_width >= 2 && map.terrain_height >= 2 && map.elevation_map.size() >= samples)
        {
            auto& field = asset.ground;
            field.width = map.terrain_width;
            field.height = map.terrain_height;
            field.spacing = terrain_sample_spacing;
            field.heights.reserve(samples);
            for (std::size_t index = 0; index < samples; ++index)
            {
                field.heights.push_back(static_cast<float>(map.elevation_map[index]) / terrain_elevation_scale);
            }
            // From map data alone, never from what is drawn (a missing texture must not change the
            // ground): a liquid surface at or above the lowest ground, as append_water draws it.
            const auto liquid = map.water.type == TedWaterType::water || map.water.type == TedWaterType::lava || map.water.type == TedWaterType::ice;
            if (liquid && !field.heights.empty() && map.water.level >= *std::ranges::min_element(field.heights))
            {
                field.liquid_level = map.water.level;
            }
            if (map.passability_map.size() == samples)
            {
                field.passability = map.passability_map;
            }
        }
        asset.tracks = append_tracks(builder, map);
    }
    std::unordered_map<std::string, std::optional<std::uint32_t>> meshes;
    // Company members (placed at their company's origin) are arranged after the loop.
    struct CompanyMember
    {
        std::size_t object_index{};
        std::size_t entity{};
        std::size_t instance{};
        const XmlDefinition* type{};
    };
    std::vector<CompanyMember> company_members;
    for (const auto& placement : layout.placements)
    {
        if (placement.model_path.empty())
        {
            continue;
        }
        std::vector<ModelAttachment> attachments;
        if (placement.type != nullptr)
        {
            const auto host = cached_model(builder, placement.model_path);
            attachments = type_hardpoint_models(builder, catalog, *placement.type, host->ok() ? &host->value() : nullptr);
        }
        auto [entry, inserted] = meshes.try_emplace(attached_mesh_key(placement.model_path, attachments));
        if (inserted)
        {
            entry->second = append_scene_model(builder, placement.model_path, attachments);
        }
        if (placement.type != nullptr)
        {
            note_animation_source(builder, placement.model_path, catalog, *placement.type);
        }
        if (!entry->second)
        {
            ++asset.unrenderable_placements;
            continue;
        }
        // Instances start in their mesh's rest pose (build_map_rigs sets the bone offsets). The type's
        // Scale_Factor sizes the model; separation and obstacle footprints are measured through it.
        const auto model_scale = placement.type != nullptr ? type_scale_factor(catalog, *placement.type, scene.warnings) : 1.0F;
        VisualInstance instance{*entry->second, scaled_model_transform(placement.transform, model_scale)};
        {
            const auto& matrix = instance.transform;
            EntitySpawn spawn;
            spawn.type_name = placement.type != nullptr ? placement.type->name : std::string{};
            spawn.owner = map.placed_objects[placement.object_index].player;
            spawn.position = translation(matrix);
            // Heading is the direction of the model's forward (-Y) axis about +Z.
            spawn.heading = model_forward_heading(matrix);
            spawn.visual = *entry->second;
            spawn.follows_ground = asset.land && placement.type != nullptr && type_follows_ground(catalog, *placement.type);
            if (spawn.follows_ground)
            {
                spawn.passable_types = type_passable_types(catalog, *placement.type, movement_classes);
            }
            if (placement.type != nullptr)
            {
                spawn.max_speed = type_max_speed(catalog, *placement.type, scene.warnings);
                const auto decoded = cached_model(builder, placement.model_path);
                spawn.combat = profile_of(*placement.type, decoded && decoded->ok() ? &decoded->value() : nullptr);
                note_damage_bones(builder, placement.model_path, spawn.combat);
                // Only mobile units take space for now; structures and props keep overlapping
                // until their collision footprints are read.
                if (spawn.max_speed <= 0.0F && asset.land)
                {
                    if (const auto blocks = type_obstacle_passability(catalog, *placement.type))
                    {
                        spawn.footprint = mesh_ground_footprint(scene, *entry->second, matrix, *blocks);
                        ++asset.obstacle_footprints;
                    }
                }
                if (spawn.max_speed > 0.0F)
                {
                    const auto soft_radius = type_soft_footprint_radius(catalog, *placement.type);
                    spawn.radius = soft_radius > 0.0F ? soft_radius : mesh_footprint_radius(scene, *entry->second, matrix);
                    spawn.turn_rate = type_turn_rate(catalog, *placement.type);
                    spawn.min_speed = asset.land ? 0.0F : type_min_flight_speed(catalog, *placement.type, spawn.max_speed);
                    spawn.aims_with_body = spawn.follows_ground && type_aims_with_body(catalog, *placement.type);
                    spawn.formation_order = type_formation_order(catalog, *placement.type);
                    spawn.formation_spacing = type_formation_spacing(catalog, *placement.type);
                }
            }
            asset.entities.push_back({std::move(spawn), static_cast<std::uint32_t>(scene.instances.size()), placement.type != nullptr ? type_death_style(catalog, *placement.type) : DeathStyle{}});
            if (placement.needs_formation)
            {
                company_members.push_back({placement.object_index, asset.entities.size() - 1U, scene.instances.size(), placement.type});
            }
        }
        if (const auto faction = placement.type != nullptr ? catalog.affiliation_color(*placement.type) : std::nullopt)
        {
            instance.team_color = *faction;
            ++asset.faction_colored;
        }
        else
        {
            instance.team_color = player_team_color(map.placed_objects[placement.object_index].player);
            ++asset.player_colored;
        }
        scene.instances.push_back(instance);
    }
    arrange_companies(scene, asset, company_members, catalog);

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
        const auto context = asset.land ? GameModelContext::land : GameModelContext::space;
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
    const auto spawn_context = asset.land ? GameModelContext::land : GameModelContext::space;
    // Each requested name once: an existing entry is reused (company members are often repeated).
    SpawnableBuild spawnable_build{builder, catalog, movement_classes, spawn_context, asset.land, meshes, profile_of, asset.spawnable};
    for (const auto& type_name : spawn_types)
    {
        (void)add_spawnable_type(spawnable_build, type_name, 0);
    }

    // How shots look; model projectiles add meshes, so this comes before the rigs (headless builds
    // skip it, so the simulation's meshes never depend on it).
    if (options.textures)
    {
        std::vector<std::uint32_t> keys;
        for (const auto& entity : asset.entities)
        {
            collect_projectile_keys(entity.spawn.combat, keys);
        }
        for (const auto& type : asset.spawnable)
        {
            collect_projectile_keys(type.combat, keys);
        }
        load_projectile_visuals(builder, catalog, keys, asset.projectile_visuals, asset.projectile_atlas);
        load_projectile_models(builder, catalog, asset.land, meshes, asset.projectile_visuals);
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
        for (const auto& entity : asset.entities)
        {
            keys.push_back(entity.spawn.combat.death_effect);
        }
        for (const auto& type : asset.spawnable)
        {
            keys.push_back(type.combat.death_effect);
        }
        load_effect_objects(builder, catalog, keys, asset.land, asset.effect_objects);
    }
    asset.particle_textures = std::move(builder.particles.textures);
    constexpr std::size_t maximum_reported_diagnostics = 50;
    for (std::size_t index = 0; index < layout.diagnostics.size() && index < maximum_reported_diagnostics; ++index)
    {
        scene.warnings.push_back(layout.diagnostics[index]);
    }
    if (layout.diagnostics.size() > maximum_reported_diagnostics)
    {
        scene.warnings.push_back(std::to_string(layout.diagnostics.size() - maximum_reported_diagnostics) + " more object diagnostics omitted");
    }
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
    const auto catalog_entry = cached<CachedCatalog>(cache, "xml-catalog", "Data/XML", vfs,
                                                     [](const AssetReader& reader)
                                                     {
                                                         auto database = std::make_unique<XmlAssetDatabase>(XmlAssetDatabase::build(reader));
                                                         auto catalog = GameObjectCatalog::build(*database);
                                                         return CachedCatalog{std::move(database), std::move(catalog)};
                                                     });
    const auto movement_entry = cached<MovementClasses>(cache, "movement-classes", std::string(movement_class_path), vfs, load_movement_classes);
    const auto combat_entry = cached<CombatContent>(cache, "combat-content", "Data/XML/GameConstants.xml", vfs, load_combat_content);
    SpawnablePackage package;
    package.meshes.name = std::string(type_name);
    SceneBuilder builder{vfs, package.meshes, {}, cache, options.textures, options.animation_clips};
    builder.particle_fidelity = options.particle_fidelity;
    std::unordered_map<std::string, std::optional<std::uint32_t>> meshes;
    std::unordered_map<const XmlDefinition*, CombatProfile> profiles;
    const auto profile_of = [&](const XmlDefinition& definition, const AloModelData* model) -> const CombatProfile&
    {
        auto found = profiles.find(&definition);
        if (found == profiles.end())
        {
            std::vector<std::string> scale_warnings;
            found = profiles
                        .emplace(&definition, combat_profile(catalog_entry->catalog, definition, *combat_entry, package.meshes.warnings, model,
                                                             type_scale_factor(catalog_entry->catalog, definition, scale_warnings)))
                        .first;
        }
        return found->second;
    };
    SpawnableBuild build{builder, catalog_entry->catalog, movement_entry->classes, land ? GameModelContext::land : GameModelContext::space, land, meshes, profile_of, package.types};
    const auto index = add_spawnable_type(build, std::string(type_name), 0);
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
            keys.push_back(type.combat.death_effect);
        }
        load_effect_objects(builder, catalog_entry->catalog, keys, land, package.effect_objects);
    }
    package.particle_textures = std::move(builder.particles.textures);
    return package;
}

std::vector<std::string> spawnable_type_names(const AssetReader& vfs, const bool land, AssetCache* cache)
{
    const auto catalog_entry = cached<CachedCatalog>(cache, "xml-catalog", "Data/XML", vfs,
                                                     [](const AssetReader& reader)
                                                     {
                                                         auto database = std::make_unique<XmlAssetDatabase>(XmlAssetDatabase::build(reader));
                                                         auto catalog = GameObjectCatalog::build(*database);
                                                         return CachedCatalog{std::move(database), std::move(catalog)};
                                                     });
    // Death clones are wrecks other types leave behind, not units to spawn.
    const auto clones = death_clone_names(*catalog_entry->database, catalog_entry->catalog);
    std::vector<std::string> names;
    for (const auto& type : list_spawn_types(*catalog_entry->database, catalog_entry->catalog))
    {
        if (!type.unit || !(land ? type.land_model : type.space_model))
        {
            continue;
        }
        const auto definitions = catalog_entry->catalog.find(type.name);
        if (!definitions.empty() && type_is_death_clone(catalog_entry->catalog, *definitions.front(), clones))
        {
            continue;
        }
        names.push_back(type.name);
    }
    return names;
}

} // namespace eaw
