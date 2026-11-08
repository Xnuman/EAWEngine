#include "scene_builder_internal.hpp"

#include "combat_content.hpp"
#include "eaw/dds_texture.hpp"
#include "eaw/map_object_layout.hpp"
#include "eaw/xml_document.hpp"

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

namespace {

// Movement speed of an object type from its XML Max_Speed (zero when absent or invalid).
std::string_view trim_ascii(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

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
    const auto value = fold_ascii(std::string(trim_ascii(layer->text)));
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
            result.classes[fold_ascii(movement_class.name)] = *mask;
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
        if (const auto found = classes.find(fold_ascii(std::string(trim_ascii(node->text)))); found != classes.end())
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
    const auto text = trim_ascii(node->text);
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
    const auto kind = fold_ascii(definition.type);
    if (kind == "groundstructure" || kind == "specialstructure" || kind == "secondarystructure")
    {
        return passability::obstacle;
    }
    if (const auto* collision = catalog.field(definition, "UnitCollisionClass").field; collision != nullptr && fold_ascii(collision->text).find("obstacle") != std::string::npos)
    {
        return passability::obstacle;
    }
    return std::nullopt;
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
