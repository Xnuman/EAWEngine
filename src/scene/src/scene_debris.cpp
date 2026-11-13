// Debris types: the props hardpoints break off when destroyed (XML Death_Breakoff_Prop, SpaceBehavior
// DEBRIS), their models added to the scene's mesh library.

#include "core/string_utils.hpp"
#include "assets/legacy_units.hpp"
#include "assets/type_fields.hpp"
#include "scene/scene_builder_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace eaw::scene_build {

namespace {

// A three-number XML vector ("x, y, z"); zero where it is missing or malformed.
Vector3 vector_field(const TypeFields& fields, const std::string_view name)
{
    const auto items = fields.list(name);
    std::array<float, 3> values{};
    for (std::size_t index = 0; index < items.size() && index < values.size(); ++index)
    {
        const auto parsed = parse_xml_number(items[index]);
        values[index] = parsed && std::isfinite(*parsed) ? *parsed : 0.0F;
    }
    return {values[0], values[1], values[2]};
}

std::uint32_t first_name_crc(const TypeFields& fields, const std::string_view name)
{
    const auto items = fields.list(name);
    return items.empty() || ascii_trim(items.front()).empty() ? 0U : game_object_name_crc(ascii_trim(items.front()));
}

} // namespace

void load_debris_types(SceneBuilder& builder, const GameObjectCatalog& catalog, const std::span<const CombatProfile* const> combats, const bool land,
                       std::unordered_map<std::string, std::optional<std::uint32_t>>& meshes, DebrisTable& table)
{
    const auto primary = land ? GameModelContext::land : GameModelContext::space;
    const auto secondary = land ? GameModelContext::space : GameModelContext::land;
    for (const auto* combat : combats)
    {
        for (const auto& hardpoint : combat->hardpoints)
        {
            const auto key = hardpoint.breakoff_prop;
            if (key == 0 || table.contains(key))
            {
                continue;
            }
            const auto definitions = catalog.find_crc(key);
            if (definitions.empty())
            {
                continue;
            }
            const auto& definition = *definitions.front();
            auto model = catalog.model(definition, primary);
            if (!model)
            {
                model = catalog.model(definition, secondary);
            }
            const auto path = model ? resolve_model_path(builder.vfs, model->value) : std::nullopt;
            if (!path)
            {
                builder.scene.warnings.push_back(definition.name + ": debris model " + (model ? model->value : std::string("(none)")) + " was not found");
                continue;
            }
            auto [entry, inserted] = meshes.try_emplace(attached_mesh_key(*path, {}));
            if (inserted)
            {
                entry->second = append_scene_model(builder, *path);
            }
            if (!entry->second)
            {
                continue;
            }
            const TypeFields fields(catalog, definition);
            DebrisType type;
            type.mesh = *entry->second;
            // Per 30 Hz frame in the file (an engine choice: legacy_units), per second here.
            type.velocity = vector_field(fields, "Debris_Movement_Vector") * legacy_units::speed_per_frame(1.0F);
            type.spin = vector_field(fields, "Debris_Facing_Rotate_Vector") * legacy_units::degrees_per_frame(1.0F);
            type.min_seconds = fields.amount("Debris_Min_Lifetime_Seconds", 15.0F);
            type.max_seconds = std::max(fields.amount("Debris_Max_Lifetime_Seconds", type.min_seconds), type.min_seconds);
            type.trail_effect = first_name_crc(fields, "Debris_Attached_Particle");
            type.death_effect = first_name_crc(fields, "Death_Explosions");
            table.emplace(key, type);
        }
    }
}

void collect_debris_effect_keys(const DebrisTable& table, std::vector<std::uint32_t>& keys)
{
    for (const auto& [key, type] : table)
    {
        keys.push_back(type.trail_effect);
        keys.push_back(type.death_effect);
    }
}

} // namespace eaw::scene_build
