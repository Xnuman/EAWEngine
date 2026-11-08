#include "scene_builder_internal.hpp"

#include "combat_content.hpp"
#include "eaw/dds_texture.hpp"
#include "eaw/map_object_layout.hpp"
#include "eaw/xml_document.hpp"

#include <algorithm>
#include <functional>
#include <atomic>
#include <cctype>
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
        const SimVector3 center{origin[12], origin[13], origin[14]};
        const auto facing = std::atan2(origin[1], origin[0]);
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
            transform[12] = position.x;
            transform[13] = position.y;
            transform[14] = position.z;
            asset.entities[members[index].entity].spawn.position = position;
        }
        ++asset.expanded_companies;
        first = last;
    }
}

} // namespace

Result<VisualMapAsset> build_visual_map(const AssetReader& vfs, const std::string_view map_path, AssetCache* cache, const std::span<const std::string> spawn_types)
{
    const auto resolved = vfs.resolve(map_path);
    if (!resolved)
    {
        return make_error("map was not found: " + std::string(map_path));
    }
    VisualMapAsset asset;
    const auto map_entry = cached<Result<TedMap>>(cache, "ted", resolved->virtual_path, vfs,
                                                  [&](const AssetReader& reader) -> Result<TedMap>
                                                  {
                                                      EAW_TRY_ASSIGN(const auto bytes, reader.read(resolved->virtual_path));
                                                      return TedMap::parse(bytes);
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
    const auto profile_of = [&](const XmlDefinition& definition) -> const CombatProfile&
    {
        auto found = combat_profiles.find(&definition);
        if (found == combat_profiles.end())
        {
            found = combat_profiles.emplace(&definition, combat_profile(catalog, definition, *combat_entry, asset.scene.warnings)).first;
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
    SceneBuilder builder{vfs, scene, {}, cache};
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
            if (asset.water != TedWaterType::none)
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
        auto [entry, inserted] = meshes.try_emplace(placement.model_path);
        if (inserted)
        {
            entry->second = append_scene_model(builder, placement.model_path);
        }
        if (!entry->second)
        {
            ++asset.unrenderable_placements;
            continue;
        }
        // Every instance shares the identity (bind-pose) palette; per-instance animation is a
        // simulation-driven feature for later milestones.
        VisualInstance instance{*entry->second, placement.transform.values};
        {
            const auto& matrix = placement.transform.values;
            EntitySpawn spawn;
            spawn.type_name = placement.type != nullptr ? placement.type->name : std::string{};
            spawn.owner = map.placed_objects[placement.object_index].player;
            spawn.position = {matrix[12], matrix[13], matrix[14]};
            // Heading is the rotation of the model's X axis about +Z (row-vector transforms).
            spawn.heading = std::atan2(matrix[1], matrix[0]);
            spawn.visual = *entry->second;
            spawn.follows_ground = asset.land && placement.type != nullptr && type_follows_ground(catalog, *placement.type);
            if (spawn.follows_ground)
            {
                spawn.passable_types = type_passable_types(catalog, *placement.type, movement_classes);
            }
            if (placement.type != nullptr)
            {
                spawn.max_speed = type_max_speed(catalog, *placement.type, scene.warnings);
                spawn.combat = profile_of(*placement.type);
                // Only mobile units take space for now; structures and props keep overlapping
                // until their collision footprints are read.
                if (spawn.max_speed > 0.0F)
                {
                    spawn.radius = mesh_footprint_radius(scene, *entry->second, matrix);
                    spawn.turn_rate = type_turn_rate(catalog, *placement.type);
                    spawn.formation_order = type_formation_order(catalog, *placement.type);
                    spawn.formation_spacing = type_formation_spacing(catalog, *placement.type);
                }
            }
            asset.entities.push_back({std::move(spawn), static_cast<std::uint32_t>(scene.instances.size())});
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
    std::function<std::optional<std::size_t>(const std::string&, int)> add_spawnable = [&](const std::string& type_name, const int depth) -> std::optional<std::size_t>
    {
        for (std::size_t index = 0; index < asset.spawnable.size(); ++index)
        {
            if (fold_ascii(asset.spawnable[index].type_name) == fold_ascii(type_name))
            {
                return index;
            }
        }

        // A company is its members even when it names a model of its own.
        const auto is_company = std::ranges::any_of(catalog.find(type_name),
                                                    [&](const XmlDefinition* candidate)
                                                    {
                                                        return catalog.field(*candidate, "Company_Units").field != nullptr;
                                                    });
        const XmlDefinition* definition = nullptr;
        std::optional<GameObjectModelReference> model;
        for (const auto* candidate : catalog.find(type_name))
        {
            if ((model = catalog.model(*candidate, spawn_context)))
            {
                definition = candidate;
                break;
            }
        }
        const auto path = model ? resolve_model_path(vfs, model->value) : std::nullopt;
        if (definition == nullptr || !path || (is_company && depth == 0))
        {
            // A company spawns its members instead (each resolved like a requested type).
            for (const auto* candidate : catalog.find(type_name))
            {
                const auto* units = catalog.field(*candidate, "Company_Units").field;
                if (units == nullptr || depth > 0)
                {
                    continue;
                }
                SpawnableType company;
                company.type_name = candidate->name;
                for (const auto member : split_type_names(units->text))
                {
                    if (const auto index = add_spawnable(std::string(member), depth + 1))
                    {
                        company.members.push_back(*index);
                    }
                }
                if (company.members.empty())
                {
                    break;
                }
                company.mesh = asset.spawnable[company.members.front()].mesh;
                asset.spawnable.push_back(std::move(company));
                return asset.spawnable.size() - 1U;
            }
            scene.warnings.push_back("spawn type '" + type_name + "' has no resolvable " + std::string(to_string(spawn_context)) + " model");
            return std::nullopt;
        }
        auto [entry, inserted] = meshes.try_emplace(*path);
        if (inserted)
        {
            entry->second = append_scene_model(builder, *path);
        }
        if (!entry->second)
        {
            return std::nullopt;
        }
        SpawnableType spawnable;
        spawnable.type_name = definition->name;
        spawnable.mesh = *entry->second;
        spawnable.max_speed = type_max_speed(catalog, *definition, scene.warnings);
        spawnable.team_color = catalog.affiliation_color(*definition).value_or(player_team_color(1));
        spawnable.follows_ground = asset.land && type_follows_ground(catalog, *definition);
        spawnable.passable_types = spawnable.follows_ground ? type_passable_types(catalog, *definition, movement_classes) : 0U;
        spawnable.combat = profile_of(*definition);
        spawnable.hover_offset = type_float_field(catalog, *definition, "Hover_Offset");
        if (spawnable.max_speed > 0.0F)
        {
            static constexpr std::array<float, 16> identity{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
            spawnable.radius = mesh_footprint_radius(scene, spawnable.mesh, identity);
            spawnable.turn_rate = type_turn_rate(catalog, *definition);
            spawnable.formation_order = type_formation_order(catalog, *definition);
            spawnable.formation_spacing = type_formation_spacing(catalog, *definition);
        }
        asset.spawnable.push_back(std::move(spawnable));
        return asset.spawnable.size() - 1U;
    };
    for (const auto& type_name : spawn_types)
    {
        (void)add_spawnable(type_name, 0);
    }

    for (const auto& mesh : scene.meshes)
    {
        scene.bone_count = std::max(scene.bone_count, mesh.bone_count);
    }
    scene.bone_count = std::max(scene.bone_count, 1U);
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

} // namespace eaw
