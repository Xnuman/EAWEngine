#include "scene_builder_internal.hpp"

#include "combat_content.hpp"
#include "eaw/dds_texture.hpp"
#include "eaw/map_object_layout.hpp"
#include "eaw/xml_document.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <limits>
#include <ranges>
#include <span>
#include <system_error>

namespace eaw::scene_build {

// Whether a type walks on the ground of land maps. XML Space_Layer is "Land" for ground vehicles,
// infantry, and heroes, and "None" for walkers such as Ewoks; air units (snowspeeders, air whales)
// use "Frigate", "Capital", or "Corvette", and birds or space-only types have none. This reading is
// a corpus inference (see docs/compatibility-research.md).
bool type_follows_ground(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto layer = catalog.field(definition, "Space_Layer").field;
    if (layer == nullptr)
    {
        return false;
    }
    const auto value = ascii_lower(std::string(ascii_trim(layer->text)));
    return value == "land" || value == "none";
}

MovementClasses load_movement_classes(const AssetReader& reader)
{
    MovementClasses result;
    const auto bytes = reader.read(movement_class_path);
    if (!bytes)
    {
        result.warnings.push_back("movement classes unavailable: " + bytes.error().message);
        return result;
    }
    const auto document = XmlDocument::parse(std::span<const std::byte>(*bytes));
    if (!document)
    {
        result.warnings.push_back(std::string(movement_class_path) + ": " + document.error().message);
        return result;
    }
    for (const auto& root : document->roots)
    {
        for (const auto& movement_class : root.children)
        {
            const auto mask = parse_passability_list(movement_class.text);
            if (!mask)
            {
                result.warnings.push_back(std::string(movement_class_path) + " class " + movement_class.name + ": " + mask.error().message);
                continue;
            }
            result.classes[ascii_lower(movement_class.name)] = *mask;
        }
    }
    return result;
}

// Passability types a type may enter: its MovementClass's entry, or Clear | Shield (what most
// ground classes allow) when the type names no known class.
std::uint32_t type_passable_types(const GameObjectCatalog& catalog, const XmlDefinition& definition, const std::unordered_map<std::string, std::uint32_t>& classes)
{
    if (const auto node = catalog.field(definition, "MovementClass").field)
    {
        if (const auto found = classes.find(ascii_lower(std::string(ascii_trim(node->text)))); found != classes.end())
        {
            return found->second;
        }
    }
    return passability::clear | passability::shield;
}

float type_float_field(const GameObjectCatalog& catalog, const XmlDefinition& definition, const std::string_view field)
{
    const auto node = catalog.field(definition, field).field;
    if (node == nullptr)
    {
        return 0.0F;
    }
    const auto text = ascii_trim(node->text);
    float value = 0.0F;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && std::isfinite(value) ? value : 0.0F;
}

std::uint32_t type_formation_order(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto order = type_float_field(catalog, definition, "FormationOrder");
    return order >= 1.0F && order <= 1000.0F ? static_cast<std::uint32_t>(order) : 2U;
}

float type_formation_spacing(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto* node = catalog.field(definition, "FormationSpacing").field;
    const auto spacing = node != nullptr ? type_float_field(catalog, definition, "FormationSpacing") : 0.5F;
    return spacing >= 0.0F && spacing <= 100.0F ? spacing : 0.5F;
}

std::optional<std::uint32_t> type_obstacle_passability(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    if (const auto* override_node = catalog.field(definition, "OverridePassability").field)
    {
        if (const auto mask = parse_passability_list(override_node->text); mask && *mask != 0)
        {
            return *mask;
        }
    }
    const auto kind = ascii_lower(definition.type);
    if (kind == "groundstructure" || kind == "specialstructure" || kind == "secondarystructure")
    {
        return passability::obstacle;
    }
    if (const auto* collision = catalog.field(definition, "UnitCollisionClass").field; collision != nullptr && ascii_lower(collision->text).find("obstacle") != std::string::npos)
    {
        return passability::obstacle;
    }
    return std::nullopt;
}

float type_scale_factor(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::vector<std::string>& warnings)
{
    const auto* node = catalog.field(definition, "Scale_Factor").field;
    if (node == nullptr)
    {
        return 1.0F;
    }
    const auto scale = type_float_field(catalog, definition, "Scale_Factor");
    if (!std::isfinite(scale) || scale <= 0.0F)
    {
        warnings.push_back(definition.name + ": Scale_Factor '" + node->text + "' is not a positive number; drawn at 1");
        return 1.0F;
    }
    return scale;
}

Matrix4 scaled_model_transform(Matrix4 transform, const float scale) noexcept
{
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            transform(row, column) *= scale;
        }
    }
    return transform;
}

float type_soft_footprint_radius(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto radius = type_float_field(catalog, definition, "Custom_Soft_Footprint_Radius");
    return radius > 0.0F && radius < 10000.0F ? radius : 0.0F;
}

float type_turn_rate(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto rate = type_float_field(catalog, definition, "Max_Rate_Of_Turn");
    return rate > 0.0F ? rate * provisional_turn_rate_scale : 0.0F;
}

float type_min_flight_speed(const GameObjectCatalog& catalog, const XmlDefinition& definition, const float max_speed)
{
    const auto behavior = catalog.field(definition, "SpaceBehavior").field;
    if (behavior == nullptr || max_speed <= 0.0F || ascii_lower(behavior->text).find("fighter_locomotor") == std::string::npos)
    {
        return 0.0F;
    }
    const auto speed = type_float_field(catalog, definition, "Min_Speed") * provisional_max_speed_scale;
    return speed > 0.0F && std::isfinite(speed) ? std::min(speed, max_speed) : max_speed * 0.5F;
}

std::string type_animation_source(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    for (const auto* field : {"Land_Model_Anim_Override_Name", "Space_Model_Anim_Override_Name"})
    {
        if (const auto* value = catalog.field(definition, field).field)
        {
            const auto name = ascii_trim(value->text);
            if (!name.empty())
            {
                return std::string(name);
            }
        }
    }
    return {};
}

void note_animation_source(SceneBuilder& builder, const std::string& model_path, const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    if (auto source = type_animation_source(catalog, definition); !source.empty())
    {
        builder.animation_sources.try_emplace(ascii_lower(model_path), std::move(source));
    }
}

namespace {

// The clone type a Death_Clone entry ("Damage_Normal, AT_ST_Walker_Death_Clone_00") names, and its
// damage type, both trimmed.
std::pair<std::string_view, std::string_view> death_clone_entry(const std::string_view text)
{
    const auto comma = text.find(',');
    if (comma == std::string_view::npos)
    {
        return {{}, ascii_trim(text)};
    }
    return {ascii_trim(text.substr(0, comma)), ascii_trim(text.substr(comma + 1U))};
}

} // namespace

DeathStyle type_death_style(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    DeathStyle style;
    std::string_view chosen;
    for (const auto* entry : catalog.fields(definition, "Death_Clone").fields)
    {
        const auto [damage, clone] = death_clone_entry(entry->text);
        if (clone.empty())
        {
            continue;
        }
        if (chosen.empty() || ascii_lower(std::string(damage)) == "damage_normal")
        {
            chosen = clone;
        }
        if (ascii_lower(std::string(damage)) == "damage_normal")
        {
            break;
        }
    }
    if (chosen.empty())
    {
        return style;
    }
    const auto clones = catalog.find(chosen);
    if (clones.empty())
    {
        return style;
    }
    const auto& clone = *clones.front();
    if (catalog.field(clone, "Specific_Death_Anim_Index").field != nullptr)
    {
        const auto index = type_float_field(catalog, clone, "Specific_Death_Anim_Index");
        if (index >= 0.0F && index < 100.0F)
        {
            style.die_variant = static_cast<std::uint32_t>(index);
        }
    }
    if (const auto seconds = type_float_field(catalog, clone, "Death_Persistence_Duration"); seconds > 0.0F && seconds < 3600.0F)
    {
        style.persistence_seconds = seconds;
    }
    return style;
}

std::unordered_set<std::string> death_clone_names(const XmlAssetDatabase& database, const GameObjectCatalog& catalog)
{
    std::unordered_set<std::string> names;
    for (const auto& definition : database.definitions())
    {
        for (const auto* entry : catalog.fields(definition, "Death_Clone").fields)
        {
            if (const auto clone = death_clone_entry(entry->text).second; !clone.empty())
            {
                names.insert(ascii_lower(std::string(clone)));
            }
        }
    }
    return names;
}

bool type_is_death_clone(const GameObjectCatalog& catalog, const XmlDefinition& definition, const std::unordered_set<std::string>& clone_names)
{
    return clone_names.contains(ascii_lower(definition.name)) || catalog.field(definition, "Specific_Death_Anim_Index").field != nullptr ||
           catalog.field(definition, "Death_Persistence_Duration").field != nullptr;
}

bool type_aims_with_body(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    for (const auto* field : {"LandBehavior", "Behavior"})
    {
        const auto list = catalog.field(definition, field).field;
        if (list == nullptr)
        {
            continue;
        }
        for (const auto name : split_type_names(list->text))
        {
            if (ascii_lower(std::string(name)) == "turret")
            {
                return false;
            }
        }
    }
    return true;
}

float type_max_speed(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::vector<std::string>& warnings)
{
    const auto speed = catalog.field(definition, "Max_Speed").field;
    if (speed == nullptr)
    {
        return 0.0F;
    }
    // Like the previous std::stof parse: leading whitespace is skipped and trailing text ignored.
    const auto& text = speed->text;
    const auto* first = text.data();
    const auto* const last = text.data() + text.size();
    while (first != last && std::isspace(static_cast<unsigned char>(*first)) != 0)
    {
        ++first;
    }
    if (first != last && *first == '+')
    {
        ++first;
    }
    float value{};
    const auto [end, error] = std::from_chars(first, last, value);
    if (error == std::errc::invalid_argument || end == first)
    {
        warnings.push_back(definition.name + ": Max_Speed '" + speed->text + "' is not a number");
        return 0.0F;
    }
    if (error == std::errc::result_out_of_range)
    {
        warnings.push_back(definition.name + ": Max_Speed '" + speed->text + "' is out of range");
        return 0.0F;
    }
    return std::isfinite(value) && value > 0.0F ? value * provisional_max_speed_scale : 0.0F;
}

} // namespace eaw::scene_build
