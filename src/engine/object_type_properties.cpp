#include "scene_builder_internal.hpp"

#include "combat_content.hpp"
#include "formats/dds_texture.hpp"
#include "assets/map_object_layout.hpp"
#include "formats/xml_document.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>
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
    const auto document = read_xml(std::span<const std::byte>(*bytes));
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

float type_lod_bias(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto bias = type_float_field(catalog, definition, "LOD_Bias");
    return bias > 0.0F ? bias : 1.0F;
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

float type_bank_limit(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    if (catalog.field(definition, "Bank_Turn_Angle").field == nullptr)
    {
        return default_bank_limit;
    }
    const auto degrees = std::clamp(type_float_field(catalog, definition, "Bank_Turn_Angle"), 0.0F, 90.0F);
    return degrees * std::numbers::pi_v<float> / 180.0F;
}

CrushProfile type_crush(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto set = [&](const std::string_view field)
    {
        const auto* node = catalog.field(definition, field).field;
        return node != nullptr && parse_xml_bool(node->text).value_or(false);
    };
    CrushProfile crush;
    crush.squashable = set("Is_Squashable");
    crush.squashable_by_supercrusher = set("Is_Squashable_By_Supercrusher");
    if (set("Is_Supercrusher"))
    {
        crush.crusher = Crusher::supercrusher;
    }
    else if (const auto* collision = catalog.field(definition, "UnitCollisionClass").field)
    {
        const auto name = ascii_lower(ascii_trim(collision->text));
        if (name == "vehicle" || name == "large vehicle" || name == "giant vehicle")
        {
            crush.crusher = Crusher::crusher;
        }
    }
    return crush;
}

Survivors type_survivors(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    Survivors survivors;
    const auto* list = catalog.field(definition, "Destruction_Survivors").field;
    if (list == nullptr)
    {
        return survivors;
    }
    // Lines of "type, weight".
    std::string_view text = list->text;
    while (!text.empty())
    {
        const auto end = std::min(text.find('\n'), text.size());
        const auto line = text.substr(0, end);
        text.remove_prefix(std::min(end + 1, text.size()));
        const auto comma = line.find(',');
        const auto name = ascii_trim(line.substr(0, comma));
        if (name.empty())
        {
            continue;
        }
        float weight = 1.0F;
        if (comma != std::string_view::npos)
        {
            const auto value = ascii_trim(line.substr(comma + 1));
            (void)std::from_chars(value.data(), value.data() + value.size(), weight);
        }
        if (weight > 0.0F)
        {
            survivors.types.emplace_back(name);
        }
    }
    if (const auto minimum = type_float_field(catalog, definition, "Minimum_Destruction_Survivor_Count"); minimum >= 1.0F)
    {
        survivors.count = static_cast<std::uint32_t>(std::min(minimum, 100.0F));
    }
    return survivors;
}

TeamLayout type_team_layout(const GameObjectCatalog& catalog, const XmlDefinition& company)
{
    TeamLayout layout;
    const auto size = type_float_field(catalog, company, "Max_Squad_Size");
    layout.size = size >= 1.0F && size <= 1000.0F ? static_cast<std::uint32_t>(size) : 0U;
    const auto* name = catalog.field(company, "Create_Team_Type").field;
    const auto team_name = name != nullptr ? ascii_trim(name->text) : std::string_view{};
    const auto containers = team_name.empty() ? std::vector<const XmlDefinition*>{} : catalog.find(team_name);
    if (containers.empty())
    {
        return layout;
    }
    const auto& container = *containers.front();
    if (const auto* arrangement = catalog.field(container, "ContainerArrangement").field)
    {
        layout.arrangement = ascii_equal_ignoring_case(ascii_trim(arrangement->text), "Rows") ? TeamArrangement::rows : TeamArrangement::dartboard;
    }
    layout.spacing = type_formation_spacing(catalog, container);
    return layout;
}

float type_formation_raggedness(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    return std::clamp(type_float_field(catalog, definition, "FormationRaggedness"), 0.0F, 10.0F);
}

AnimationProfile type_animation_profile(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto rate = [&](const std::string_view field)
    {
        const auto value = type_float_field(catalog, definition, field);
        return value > 0.0F && value <= 100.0F ? value : 1.0F;
    };
    AnimationProfile profile;
    // Reference speeds as fractions of Max_Speed; without a top speed they cannot be placed.
    if (const auto top = type_float_field(catalog, definition, "Max_Speed"); top > 0.0F && std::isfinite(top))
    {
        const auto reference = [&](const std::string_view field)
        {
            const auto value = type_float_field(catalog, definition, field);
            return value > 0.0F && std::isfinite(value) ? value / top : 0.0F;
        };
        const auto move = reference("Movement_Animation_Speed");
        profile.move_reference = move > 0.0F ? move : 1.0F;
        profile.walk_reference = reference("Walk_Animation_Speed");
    }
    profile.idle_rate = rate("Idle_Anim_00_Rate_Mod");
    if (const auto* idle = catalog.field(definition, "Allow_Idle_When_Moving").field)
    {
        profile.idle_when_moving = parse_xml_bool(idle->text).value_or(false);
    }
    const auto flag = [&](const std::string_view field)
    {
        const auto* node = catalog.field(definition, field).field;
        return node != nullptr && parse_xml_bool(node->text).value_or(false);
    };
    profile.pre_turn = flag("Has_Pre_Turn_Anim");
    profile.walk_transition = flag("Walk_Transition");
    const auto bone = [&](const std::string_view field)
    {
        const auto* node = catalog.field(definition, field).field;
        const auto name = node != nullptr ? ascii_trim(node->text) : std::string_view{};
        return name.empty() ? 0U : game_object_name_crc(name);
    };
    profile.turret_bone = bone("Turret_Bone_Name");
    profile.barrel_bone = bone("Barrel_Bone_Name");
    if (profile.barrel_bone == profile.turret_bone)
    {
        // Spore plants name one bone as both: it only turns.
        profile.barrel_bone = 0U;
    }
    const auto speed = type_float_field(catalog, definition, "Turret_Rotate_Speed");
    profile.turret_rate = (speed > 0.0F ? speed : default_turret_rotate_speed) * provisional_turn_rate_scale;
    const auto half_arc = [&](const std::string_view field, const float fallback)
    {
        const auto degrees = type_float_field(catalog, definition, field);
        return degrees > 0.0F && degrees < 360.0F ? degrees * 0.5F * std::numbers::pi_v<float> / 180.0F : fallback;
    };
    profile.turret_pitch_limit = half_arc("Turret_Elevate_Extent_Degrees", profile.turret_pitch_limit);
    return profile;
}

bool type_is_marker(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto* node = catalog.field(definition, "Is_Marker").field;
    return node != nullptr && parse_xml_bool(node->text).value_or(false);
}

bool type_formation_solo(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto* node = catalog.field(definition, "FormationGrouping").field;
    return node != nullptr && ascii_equal_ignoring_case(ascii_trim(node->text), "Solo");
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

std::optional<GroundFootprint> type_ground_footprint(const GameObjectCatalog& catalog, const XmlDefinition& definition, const Matrix4& placement, const std::uint32_t passability_type)
{
    const auto* source = &definition;
    if (const auto* proxy = catalog.field(definition, "Obstacle_Proxy_Type").field)
    {
        if (const auto found = catalog.find(ascii_trim(proxy->text)); !found.empty())
        {
            source = found.back();
        }
    }
    const auto value = [&](const std::string_view field)
    {
        return type_float_field(catalog, *source, field);
    };
    // In the type's frame: x forward, y to the side.
    float half_x = 0.0F;
    float half_y = 0.0F;
    float center_x = 0.0F;
    float center_y = 0.0F;
    if (value("Obstacle_Width") > 0.0F && value("Obstacle_Height") > 0.0F)
    {
        // A rectangle from its corner offset; without offsets it is centered.
        half_x = value("Obstacle_Width") * 0.5F;
        half_y = value("Obstacle_Height") * 0.5F;
        center_x = (catalog.field(*source, "Obstacle_X_Offset").field != nullptr ? value("Obstacle_X_Offset") : -half_x) + half_x;
        center_y = (catalog.field(*source, "Obstacle_Y_Offset").field != nullptr ? value("Obstacle_Y_Offset") : -half_y) + half_y;
    }
    else if (value("Custom_Hard_XExtent") > 0.0F && value("Custom_Hard_YExtent") > 0.0F)
    {
        half_x = value("Custom_Hard_XExtent");
        half_y = value("Custom_Hard_YExtent");
        center_x = value("Custom_Hard_XExtent_Offset");
        center_y = value("Custom_Hard_YExtent_Offset");
    }
    else
    {
        return std::nullopt;
    }
    // The type's x runs along the model's -Y (its forward) and its y along the model's +X, matched
    // against the mesh bounds of off-center props (W_ROCKCLSTR01, W_RUINS03, W_TEMPROCKCLSTR03).
    // Sizes are world units, not scaled by Scale_Factor (TIE crawler and MPTL: an inference).
    const auto model_x = center_y;
    const auto model_y = -center_x;
    const auto heading = std::atan2(placement.values[1], placement.values[0]);
    const auto cos_heading = std::cos(heading);
    const auto sin_heading = std::sin(heading);
    GroundFootprint footprint;
    footprint.passability = passability_type;
    footprint.center = {placement.values[12] + model_x * cos_heading - model_y * sin_heading, placement.values[13] + model_x * sin_heading + model_y * cos_heading, placement.values[14]};
    // GroundFootprint's length runs along its heading, the model's +X.
    footprint.half_length = half_y;
    footprint.half_width = half_x;
    footprint.heading = heading;
    return footprint;
}

float type_space_obstacle_radius(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto behaves = [&](const std::string_view behavior)
    {
        for (const auto* field : {"Behavior", "SpaceBehavior"})
        {
            if (const auto* list = catalog.field(definition, field).field)
            {
                for (const auto name : split_type_names(list->text))
                {
                    if (ascii_equal_ignoring_case(name, behavior))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    };
    const auto set = [&](const std::string_view field)
    {
        const auto* node = catalog.field(definition, field).field;
        return node != nullptr && parse_xml_bool(node->text).value_or(false);
    };
    // Fields and clouds are entered (their effects wait for space hazards).
    if (!behaves("SPACE_OBSTACLE") || set("Is_Asteroid_Field") || set("Is_Nebula") || set("Is_Ion_Storm"))
    {
        return 0.0F;
    }
    const auto radius = type_float_field(catalog, definition, "Space_Obstacle_Radius");
    return radius > 0.0F ? radius : 0.0F;
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
    for (const auto* field : {"Custom_Soft_Footprint_Radius", "Custom_Footprint_Radius"})
    {
        if (const auto radius = type_float_field(catalog, definition, field); radius > 0.0F && radius < 10000.0F)
        {
            return radius;
        }
    }
    return 0.0F;
}

float type_turn_rate(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto rate = type_float_field(catalog, definition, "Max_Rate_Of_Turn");
    return rate > 0.0F ? rate * provisional_turn_rate_scale : 0.0F;
}

MovementProfile type_movement(const GameObjectCatalog& catalog, const XmlDefinition& definition, const bool land, const bool flyer)
{
    MovementProfile movement;
    if (flyer)
    {
        return movement;
    }
    const auto rate = [&](const std::string_view field, const float scale)
    {
        const auto value = type_float_field(catalog, definition, field) * scale;
        return value > 0.0F && std::isfinite(value) ? value : 0.0F;
    };
    // Team containers spell the fields with an underscore (Override_Acceleration, 40 stock uses);
    // no stock type uses both spellings.
    const auto either = [&](const std::string_view field, const std::string_view alias)
    {
        return catalog.field(definition, field).field != nullptr ? rate(field, provisional_acceleration_scale) : rate(alias, provisional_acceleration_scale);
    };
    movement.acceleration = either("OverrideAcceleration", "Override_Acceleration");
    movement.deceleration = either("OverrideDeceleration", "Override_Deceleration");
    const bool names_fraction = catalog.field(definition, "Min_Speed_Fraction_For_Turn").field != nullptr;
    const auto fraction = names_fraction ? std::clamp(rate("Min_Speed_Fraction_For_Turn", 1.0F), 0.0F, 1.0F) : (land ? 0.0F : default_space_turn_speed_fraction);
    movement.turn_speed_fraction = fraction;
    if (land)
    {
        const auto stopped = rate("Stopped_Rate_Of_Turn", provisional_turn_rate_scale);
        movement.stopped_turn_rate = stopped > 0.0F ? stopped : type_turn_rate(catalog, definition);
        // Degrees; 180 and above never stop to turn (two stock types say 180, one 360).
        const auto* angle = catalog.field(definition, "Close_Enough_Angle_For_Move_Start").field;
        const auto degrees = angle != nullptr ? type_float_field(catalog, definition, "Close_Enough_Angle_For_Move_Start") : 0.0F;
        movement.turn_in_place_angle = degrees > 0.0F ? std::min(degrees, 180.0F) * std::numbers::pi_v<float> / 180.0F : ground_turn_in_place_angle;
    }
    return movement;
}

TeamMovement type_team_movement(const GameObjectCatalog& catalog, const XmlDefinition& company, const float speed_multiplier)
{
    TeamMovement team;
    const auto* name = catalog.field(company, "Create_Team_Type").field;
    const auto team_name = name != nullptr ? ascii_trim(name->text) : std::string_view{};
    if (team_name.empty())
    {
        return team;
    }
    const XmlDefinition* container = nullptr;
    for (const auto* candidate : catalog.find(team_name))
    {
        if (container == nullptr || ascii_equal_ignoring_case(candidate->type, "Container"))
        {
            container = candidate;
        }
    }
    if (container == nullptr)
    {
        return team;
    }
    const auto sets = [&](const std::string_view field)
    {
        return catalog.field(*container, field).field != nullptr;
    };
    std::vector<std::string> ignored;
    if (sets("Max_Speed"))
    {
        team.max_speed = type_max_speed(catalog, *container, ignored, speed_multiplier);
    }
    if (sets("Max_Rate_Of_Turn"))
    {
        team.turn_rate = type_turn_rate(catalog, *container);
    }
    const auto movement = type_movement(catalog, *container, true, false);
    if (sets("OverrideAcceleration") || sets("Override_Acceleration"))
    {
        team.acceleration = movement.acceleration;
    }
    if (sets("OverrideDeceleration") || sets("Override_Deceleration"))
    {
        team.deceleration = movement.deceleration;
    }
    return team;
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
    if (const auto* removed = catalog.field(definition, "Remove_Upon_Death").field)
    {
        style.removed = parse_xml_bool(removed->text).value_or(false);
    }
    std::string_view chosen;
    for (const auto* entry : catalog.fields(definition, "Death_Clone").fields)
    {
        // The crush clone's clip, for deaths by crushing.
        if (const auto [crush_damage, crush_clone] = death_clone_entry(entry->text); ascii_equal_ignoring_case(crush_damage, "Damage_Crush") && !crush_clone.empty())
        {
            if (const auto found = catalog.find(crush_clone); !found.empty())
            {
                const auto* kind = catalog.field(*found.front(), "Specific_Death_Anim_Type").field;
                DeathClip clip;
                clip.role = kind != nullptr && ascii_equal_ignoring_case(ascii_trim(kind->text), "Crushed") ? AnimationRole::crushed : AnimationRole::die;
                const auto index = type_float_field(catalog, *found.front(), "Specific_Death_Anim_Index");
                clip.variant = index >= 0.0F && index < 100.0F ? static_cast<std::uint32_t>(index) : 0U;
                style.crushed = clip;
            }
        }
        const auto [damage, clone] = death_clone_entry(entry->text);
        if (clone.empty())
        {
            continue;
        }
        if (chosen.empty() || ascii_lower(std::string(damage)) == "damage_normal")
        {
            chosen = clone;
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

ScriptTypeInfo type_script_info(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    ScriptTypeInfo info;
    if (const auto* node = catalog.field(definition, "Lua_Script").field)
    {
        info.lua_script = std::string(ascii_trim(node->text));
    }
    info.combat_rating = type_float_field(catalog, definition, "AI_Combat_Power");
    // CategoryMask: names separated by '|' (stock), commas, or whitespace.
    if (const auto* node = catalog.field(definition, "CategoryMask").field)
    {
        std::string name;
        for (const char character : node->text + " ")
        {
            if (character == '|' || character == ',' || ascii_is_space(character))
            {
                if (!name.empty())
                {
                    info.categories.push_back(ascii_lower(name));
                    name.clear();
                }
                continue;
            }
            name += character;
        }
    }
    return info;
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

float type_max_speed(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::vector<std::string>& warnings, const float mode_multiplier)
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
    return std::isfinite(value) && value > 0.0F ? value * provisional_max_speed_scale * mode_multiplier : 0.0F;
}

} // namespace eaw::scene_build
