// The battle side of a map: placed entities, spawnable types, ground, and combat data from the TED,
// the XML types, and the models' skeletons and geometry. Nothing here draws; the visual build
// (visual_map_builder.cpp) runs this walk and then adds the look.

#include "core/string_utils.hpp"
#include "assets/map_object_layout.hpp"
#include "assets/spawn_catalog.hpp"
#include "battle/battle_map.hpp"
#include "battle/battle_map_build.hpp"
#include "battle/combat_content.hpp"
#include "battle/object_types.hpp"

#include <algorithm>
#include <ranges>
#include <span>

namespace eaw {

namespace {

// A company member placed at its company's origin, arranged after the walk.
struct CompanyMember
{
    std::size_t object_index{};
    std::size_t entity{};
    const XmlDefinition* type{};
    const XmlDefinition* company{};
};

// Lays each company's members (consecutive entries with one object index) out with the formation
// used for group orders, centered on the company's placed position and facing its heading, and
// moves their spawns and models there (heights from the ground surface). The original arranges
// companies at runtime; this layout is an engine choice.
void arrange_companies(BattleMapAsset& asset, std::vector<BuiltModel>& models, std::span<const ModelBounds> bounds, const std::vector<CompanyMember>& members, const GameObjectCatalog& catalog)
{
    for (std::size_t first = 0; first < members.size();)
    {
        auto last = first;
        while (last < members.size() && members[last].object_index == members[first].object_index)
        {
            ++last;
        }
        const auto& origin = models[members[first].entity].transform;
        const auto center = translation(origin);
        const auto facing = model_forward_heading(origin);
        std::vector<FormationMember> formation;
        std::vector<float> raggedness;
        for (auto index = first; index < last; ++index)
        {
            const auto& member = members[index];
            const auto& spawn = asset.entities[member.entity].spawn;
            const auto radius = spawn.radius > 0.0F ? spawn.radius : model_footprint_radius(bounds[member.entity], models[member.entity].transform);
            formation.push_back({EntityId{static_cast<std::uint32_t>(index + 1U), 1}, center, radius, type_formation_spacing(catalog, *member.type), type_formation_order(catalog, *member.type)});
            raggedness.push_back(type_formation_raggedness(catalog, *member.type));
        }
        if (members[first].company != nullptr)
        {
            assign_teams(formation, raggedness, type_team_layout(catalog, *members[first].company), asset.next_team);
        }
        const auto slots = formation_slots(formation, center, facing);
        for (auto index = first; index < last; ++index)
        {
            auto position = slots[index - first].position;
            if (asset.ground.valid())
            {
                position.z = asset.ground.surface_height(position.x, position.y);
            }
            const auto entity = members[index].entity;
            set_translation(models[entity].transform, position);
            asset.entities[entity].spawn.position = position;
            asset.entities[entity].spawn.formation_team = formation[index - first].team;
        }
        ++asset.expanded_companies;
        first = last;
    }
}

// The map's ground from its data alone, never from what is drawn (a missing texture must not
// change the ground); invalid when the map has no usable elevation.
GroundMap map_ground(const TedMapData& map)
{
    GroundMap field;
    const auto samples = static_cast<std::size_t>(map.terrain_width) * map.terrain_height;
    if (map.terrain_width < 2 || map.terrain_height < 2 || map.elevation_map.size() < samples)
    {
        return field;
    }
    field.width = map.terrain_width;
    field.height = map.terrain_height;
    field.spacing = terrain_sample_spacing;
    field.heights.reserve(samples);
    for (std::size_t index = 0; index < samples; ++index)
    {
        field.heights.push_back(static_cast<float>(map.elevation_map[index]) / terrain_elevation_scale);
    }
    // A liquid surface at or above the lowest ground, as append_water draws it.
    const auto liquid = map.water.type == TedWaterType::water || map.water.type == TedWaterType::lava || map.water.type == TedWaterType::ice;
    if (liquid && !field.heights.empty() && map.water.level >= *std::ranges::min_element(field.heights))
    {
        field.liquid_level = map.water.level;
    }
    if (map.passability_map.size() == samples)
    {
        field.passability = map.passability_map;
    }
    return field;
}

} // namespace

std::optional<std::size_t> add_spawnable_type(SpawnableBuild& build, const std::string& type_name, const int depth)
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
    const auto path = model ? resolve_model_path(build.resources.reader(), model->value) : std::nullopt;
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
                names = split_list(company_units->text);
            }
            for (const auto* squadron_units : build.catalog.fields(*candidate, "Squadron_Units").fields)
            {
                const auto more = split_list(squadron_units->text);
                names.insert(names.end(), more.begin(), more.end());
            }
            if (names.empty() || depth > 0)
            {
                continue;
            }
            SpawnableType company;
            company.type_name = candidate->name;
            if (build.catalog.field(*candidate, "Company_Units").field != nullptr)
            {
                company.team = type_team_movement(build.catalog, *candidate, build.speed_multiplier);
                company.team_layout = type_team_layout(build.catalog, *candidate);
            }
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
            build.spawnable.push_back(std::move(company));
            build.models.push_back({.type = candidate});
            return build.spawnable.size() - 1U;
        }
        build.warnings.push_back("spawn type '" + type_name + "' has no resolvable " + std::string(to_string(build.context)) + " model");
        return std::nullopt;
    }
    const auto host = build.resources.get<Model>(*path);
    const auto* host_model = host->ok() ? &host->value() : nullptr;
    auto attachments = type_hardpoint_models(build.resources, build.warnings, build.catalog, *definition, host_model);
    // As for placed objects: the model decides whether the type can be spawned, not drawing.
    if (host_model == nullptr || !has_drawn_geometry(*host_model, attachments, true))
    {
        return std::nullopt;
    }
    SpawnableType spawnable;
    static_cast<TypeRecipe&>(spawnable) = build_type_recipe(build, *definition, *path, *host_model, attachments);
    const auto scale = spawnable.scale;
    build.spawnable.push_back(std::move(spawnable));
    build.models.push_back({*path, std::move(attachments), definition, Matrix4::identity(), scale});
    return build.spawnable.size() - 1U;
}

void build_battle_map(const ResourceManager resources, const BattleMapSources& sources, const std::span<const std::string> spawn_types, BattleMapAsset& asset, std::vector<BuiltModel>& entity_models,
                      std::vector<BuiltModel>& spawnable_models, std::vector<BuiltModel>* decoration_models)
{
    const auto& map = sources.map;
    const auto& catalog = sources.catalog;
    const auto& layout = sources.layout;
    const auto& combat = sources.combat;
    asset.land = map.type == TedMapType::land;
    asset.damage_table = combat.damage_table;
    asset.untyped = layout.untyped;
    asset.without_model = layout.without_model;
    asset.missing_model_files = layout.missing_model_files;
    asset.expanded_members = layout.expanded_members;
    asset.company_members = layout.company_members;
    asset.unexpanded_containers = layout.unexpanded_containers;
    asset.warnings.insert(asset.warnings.end(), combat.warnings.begin(), combat.warnings.end());
    if (asset.land)
    {
        asset.warnings.insert(asset.warnings.end(), sources.movement.warnings.begin(), sources.movement.warnings.end());
        asset.ground = map_ground(map);
    }
    const auto speed_multiplier = combat.max_speed_multiplier.of(asset.land);
    // One profile per type, so a type's warnings are reported once however often it is placed.
    std::unordered_map<const XmlDefinition*, CombatProfile> combat_profiles;
    const auto profile_of = [&](const XmlDefinition& definition, const Model* model) -> const CombatProfile&
    {
        auto found = combat_profiles.find(&definition);
        if (found == combat_profiles.end())
        {
            std::vector<std::string> scale_warnings;
            found = combat_profiles
                        .emplace(&definition, combat_profile(catalog, definition, combat, asset.warnings, model != nullptr ? &model->skeleton : nullptr,
                                                             type_scale_factor(catalog, definition, scale_warnings), combat.max_health_multiplier.of(asset.land)))
                        .first;
        }
        return found->second;
    };

    // How XML types are read for this map (their recipes): for runtime spawning and for the placed
    // objects alike.
    SpawnableBuild spawnable_build{resources,        catalog,       sources.movement.classes, asset.land ? GameModelContext::land : GameModelContext::space, asset.land, profile_of, asset.spawnable,
                                   spawnable_models, asset.warnings};
    spawnable_build.speed_multiplier = speed_multiplier;
    spawnable_build.combat = &combat;
    // Placed objects' recipes, once per type and model.
    std::unordered_map<std::string, TypeRecipe> recipes;

    // Each entity's drawn model bounds (once per model and attachments), which its footprint and
    // radius fall back to and the map's extent is measured from.
    std::unordered_map<std::string, ModelBounds> model_bounds;
    std::vector<ModelBounds> entity_bounds;
    std::vector<std::pair<ModelBounds, Matrix4>> decoration_extents;
    std::vector<CompanyMember> company_members;
    for (const auto& placement : layout.placements)
    {
        if (placement.model_path.empty())
        {
            continue;
        }
        const auto host = resources.get<Model>(placement.model_path);
        const auto* host_model = host->ok() ? &host->value() : nullptr;
        auto attachments = placement.type != nullptr ? type_hardpoint_models(resources, asset.warnings, catalog, *placement.type, host_model) : std::vector<ModelAttachment>{};
        // Whether the placement exists is decided from its model (assets/model_geometry), never
        // from drawing.
        if (host_model == nullptr || !has_drawn_geometry(*host_model, attachments, true))
        {
            ++asset.placements_without_geometry;
            continue;
        }
        auto [measured, fresh] = model_bounds.try_emplace(attached_mesh_key(placement.model_path, attachments));
        if (fresh)
        {
            measured->second = drawn_model_bounds(*host_model, attachments, true);
        }
        const auto& bounds = measured->second;
        // The type's Scale_Factor sizes the model; separation and obstacle footprints are measured
        // through it.
        const auto model_scale = placement.type != nullptr ? type_scale_factor(catalog, *placement.type, asset.warnings) : 1.0F;
        const auto matrix = scaled_model_transform(placement.transform, model_scale);
        const auto& placed = map.placed_objects[placement.object_index];
        // Decorations are drawn, never simulated; they still frame the map (its extent).
        if (placement.type != nullptr && type_is_decoration(catalog, *placement.type))
        {
            ++asset.decorations;
            decoration_extents.emplace_back(bounds, matrix);
            if (decoration_models != nullptr)
            {
                decoration_models->push_back({placement.model_path, std::move(attachments), placement.type, matrix, model_scale, placed.player});
            }
            continue;
        }
        // Heading is the direction of the model's forward (-Y) axis about +Z.
        const auto position = translation(matrix);
        const auto heading = model_forward_heading(matrix);
        EntitySpawn spawn;
        const TypeRecipe* recipe = nullptr;
        if (placement.type != nullptr)
        {
            auto [entry, fresh_recipe] = recipes.try_emplace(placement.type->name + '|' + attached_mesh_key(placement.model_path, attachments));
            if (fresh_recipe)
            {
                entry->second = build_type_recipe(spawnable_build, *placement.type, placement.model_path, *host_model, attachments);
            }
            recipe = &entry->second;
            spawn = make_spawn(*recipe, {position, heading, placed.player, 0, asset.ground.valid(), matrix});
            if (spawn.max_speed > 0.0F && placement.container != nullptr && placement.needs_formation)
            {
                apply_team_movement(type_team_movement(catalog, *placement.container, speed_multiplier), spawn);
            }
            asset.obstacle_footprints += spawn.footprint ? 1U : 0U;
            asset.space_obstacles += !asset.land && spawn.max_speed <= 0.0F && spawn.radius > 0.0F ? 1U : 0U;
        }
        else
        {
            spawn.owner = placed.player;
            spawn.position = position;
            spawn.heading = heading;
        }
        if (placement.needs_formation)
        {
            company_members.push_back({placement.object_index, asset.entities.size(), placement.type, placement.container});
        }
        asset.entities.push_back({std::move(spawn), recipe != nullptr ? recipe->script : ScriptTypeInfo{}, placed.hint, recipe != nullptr ? recipe->survivors : Survivors{}});
        entity_models.push_back({placement.model_path, std::move(attachments), placement.type, matrix, model_scale, placed.player});
        entity_bounds.push_back(bounds);
    }
    arrange_companies(asset, entity_models, entity_bounds, company_members, catalog);
    for (std::size_t index = 0; index < entity_models.size(); ++index)
    {
        add_placed_bounds(asset.extent, entity_bounds[index], entity_models[index].transform);
    }
    for (const auto& [bounds, transform] : decoration_extents)
    {
        add_placed_bounds(asset.extent, bounds, transform);
    }
    constexpr std::size_t maximum_reported_diagnostics = 50;
    for (std::size_t index = 0; index < layout.diagnostics.size() && index < maximum_reported_diagnostics; ++index)
    {
        asset.warnings.push_back(layout.diagnostics[index]);
    }
    if (layout.diagnostics.size() > maximum_reported_diagnostics)
    {
        asset.warnings.push_back(std::to_string(layout.diagnostics.size() - maximum_reported_diagnostics) + " more object diagnostics omitted");
    }

    // Types requested for runtime spawning, each name once: an existing entry is reused (company
    // members are often repeated).
    for (const auto& type_name : spawn_types)
    {
        (void)add_spawnable_type(spawnable_build, type_name, 0);
    }
    // What destroyed objects leave must be spawnable too (survivors of survivors included).
    for (const auto& entity : asset.entities)
    {
        for (const auto& survivor : entity.survivors.types)
        {
            (void)add_spawnable_type(spawnable_build, survivor, 0);
        }
    }
    for (std::size_t index = 0; index < asset.spawnable.size(); ++index)
    {
        const auto names = asset.spawnable[index].survivors.types;
        for (const auto& survivor : names)
        {
            (void)add_spawnable_type(spawnable_build, survivor, 0);
        }
    }
}

Result<BattleMapAsset> build_battle_map(const AssetReader& reader, const std::string_view map_path, AssetCache* cache, const std::span<const std::string> spawn_types)
{
    const auto resolved = reader.resolve(map_path);
    if (!resolved)
    {
        return make_error("map was not found: " + std::string(map_path));
    }
    // Models are shared by many placements; without a cache of the caller's, one for this build.
    AssetCache build_cache;
    const ResourceManager resources(reader, cache != nullptr ? cache : &build_cache);
    const auto map_entry = resources.get<TedMapData>(resolved->virtual_path);
    if (!map_entry->ok())
    {
        return with_context(resolved->virtual_path, map_entry->error());
    }
    const auto& map = map_entry->value();
    const auto catalog_entry = resources.get<CachedCatalog>(xml_catalog_path);
    const auto layout = build_map_object_layout(map, catalog_entry->catalog, reader);
    const auto movement_entry = resources.get<MovementClasses>(movement_class_path);
    const auto combat_entry = resources.get<CombatContent>(combat_content_path);
    BattleMapAsset asset;
    asset.name = resolved->virtual_path;
    std::vector<BuiltModel> entity_models;
    std::vector<BuiltModel> spawnable_models;
    build_battle_map(resources, {map, catalog_entry->catalog, layout, *movement_entry, *combat_entry}, spawn_types, asset, entity_models, spawnable_models);
    return asset;
}

std::vector<SpawnTypeInfo> spawnable_types(const AssetReader& vfs, const bool land, AssetCache* cache)
{
    const auto catalog_entry = ResourceManager(vfs, cache).get<CachedCatalog>(xml_catalog_path);
    // Death clones are wrecks other types leave behind, not units to spawn.
    const auto clones = death_clone_names(*catalog_entry->database, catalog_entry->catalog);
    std::vector<SpawnTypeInfo> types;
    for (auto& type : list_spawn_types(*catalog_entry->database, catalog_entry->catalog))
    {
        const auto kind = ascii_lower(type.kind);
        const bool prop = kind.starts_with("prop") || kind == "spaceprop";
        if ((!type.unit && !prop) || !(land ? type.land_model : type.space_model))
        {
            continue;
        }
        const auto definitions = catalog_entry->catalog.find(type.name);
        if (!definitions.empty() && type_is_death_clone(catalog_entry->catalog, *definitions.front(), clones))
        {
            continue;
        }
        types.push_back(std::move(type));
    }
    return types;
}

std::string ResourceLoader<CachedCatalog>::key(const std::string_view path)
{
    return std::string(path);
}

// The catalog borrows the database, so both are built together behind one allocation.
CachedCatalog ResourceLoader<CachedCatalog>::load(const AssetReader& reader, std::string_view)
{
    auto database = std::make_unique<XmlAssetDatabase>(XmlAssetDatabase::build(reader));
    auto catalog = GameObjectCatalog::build(*database);
    return CachedCatalog{std::move(database), std::move(catalog)};
}

std::string ResourceLoader<MovementClasses>::key(const std::string_view path)
{
    return std::string(path);
}

MovementClasses ResourceLoader<MovementClasses>::load(const AssetReader& reader, std::string_view)
{
    return load_movement_classes(reader);
}

std::string ResourceLoader<CombatContent>::key(const std::string_view path)
{
    return std::string(path);
}

CombatContent ResourceLoader<CombatContent>::load(const AssetReader& reader, std::string_view)
{
    return load_combat_content(reader);
}

} // namespace eaw
