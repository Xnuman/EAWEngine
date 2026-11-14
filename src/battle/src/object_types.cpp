#include "battle/object_types.hpp"

#include "math/scalar.hpp"
#include "math/transform.hpp"
#include "core/string_utils.hpp"
#include "assets/clip_names.hpp"
#include "assets/legacy_units.hpp"
#include "assets/map_object_layout.hpp"
#include "assets/skeleton.hpp"
#include "assets/type_fields.hpp"
#include "assets/type_hardpoints.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <ranges>
#include <span>
#include <system_error>

namespace eaw {

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
    return TypeFields(catalog, definition).number(field).value_or(0.0F);
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

CrushProfile type_crush(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto set = [&](const std::string_view field)
    {
        return TypeFields(catalog, definition).flag(field, false);
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
    // "type, weight" entries (split_list): each type is followed by its weight, or by the next
    // type when the weight is left out (weight 1).
    const auto items = split_list(list->text);
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const auto name = items[index];
        if (parse_xml_number(name))
        {
            continue;
        }
        float weight = 1.0F;
        if (index + 1U < items.size())
        {
            if (const auto value = parse_xml_number(items[index + 1U]))
            {
                weight = *value;
                ++index;
            }
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
                for (const auto name : split_list(list->text))
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
        return TypeFields(catalog, definition).flag(field, false);
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

float type_turn_rate(const GameObjectCatalog& catalog, const XmlDefinition& definition, const float mode_multiplier)
{
    const auto rate = type_float_field(catalog, definition, "Max_Rate_Of_Turn");
    return rate > 0.0F ? legacy_units::degrees_per_frame(rate) * mode_multiplier : 0.0F;
}

bool type_is_decoration(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    return TypeFields(catalog, definition, nullptr).flag("Is_Decoration", false);
}

float type_turn_in_place_slowdown(const GameObjectCatalog& catalog, const XmlDefinition& definition, const CombatContent& content)
{
    const auto* layer = catalog.field(definition, "Space_Layer").field;
    const auto items = layer != nullptr ? split_list(layer->text) : std::vector<std::string_view>{};
    if (items.empty())
    {
        return 1.0F;
    }
    const auto first = ascii_lower(items.front());
    return first == "corvette"  ? content.turn_in_place_slowdown_corvette
           : first == "frigate" ? content.turn_in_place_slowdown_frigate
           : first == "capital" ? content.turn_in_place_slowdown_capital
                                : 1.0F;
}

float walk_clip_playback(const float move_reference) noexcept
{
    return std::clamp(1.0F / std::max(move_reference, 1.0e-3F), minimum_walk_clip_playback, maximum_walk_clip_playback);
}

float turn_clip_playback(const float turn_rate, const float turn_reference) noexcept
{
    return turn_reference > 0.0F ? std::clamp(std::fabs(turn_rate) / turn_reference, minimum_walk_clip_playback, maximum_walk_clip_playback) : 1.0F;
}

void apply_animation_locomotion(MovementProfile& movement, const GameObjectCatalog& catalog, const XmlDefinition& definition, const ResourceManager& resources, const std::string_view model_path,
                                const float max_speed)
{
    if (!TypeFields(catalog, definition).flag("Locomotor_Has_Animation_Priority", false) || !(max_speed > 0.0F))
    {
        return;
    }
    const auto top = type_float_field(catalog, definition, "Max_Speed");
    const auto reference = type_float_field(catalog, definition, "Movement_Animation_Speed");
    if (!(top > 0.0F) || !(reference > 0.0F))
    {
        return;
    }
    const auto playback = walk_clip_playback(reference / top);
    const auto stem = ascii_upper(model_path.substr(0, model_path.rfind('.')));
    // A clip's length in seconds at `rate`; zero when the model has no such clip. The walk and turn
    // plans use each role's first variant (presentation plays those for them).
    const auto clip_seconds = [&](const AnimationRole role, const std::uint32_t variant, const float rate) -> float
    {
        const auto file = clip_file_stem(stem, role, variant);
        if (file.empty())
        {
            return 0.0F;
        }
        const auto animation = resources.get<Animation>(file + ".ALA");
        return animation->ok() ? animation_duration(animation->value()) / rate : 0.0F;
    };
    const auto seconds = [&](const AnimationRole role, const std::uint32_t variant = 0)
    {
        return clip_seconds(role, variant, playback);
    };
    if (movement.acceleration <= 0.0F)
    {
        if (const auto start = seconds(AnimationRole::move_start); start > 0.0F)
        {
            movement.acceleration = max_speed / start;
        }
        if (const auto stop = seconds(AnimationRole::move_end); stop > 0.0F)
        {
            movement.deceleration = max_speed / stop;
        }
    }
    // The walk as clips: MOVESTART, the MOVE stride, and the MOVE_END clips (every stock walker's
    // MOVE_END clips are as long as each other).
    std::uint32_t parts = 0;
    while (parts < max_walk_stop_parts && seconds(AnimationRole::move_end, parts) > 0.0F)
    {
        ++parts;
    }
    if (const auto start = seconds(AnimationRole::move_start), stride = seconds(AnimationRole::move), stop = seconds(AnimationRole::move_end);
        parts > 0 && start > 0.0F && stride > 0.0F && stop > 0.0F)
    {
        movement.walk_start_seconds = start;
        movement.walk_stride_seconds = stride;
        movement.walk_stop_seconds = stop;
        movement.walk_stop_parts = parts;
    }
    // Turning in place, at the stopped turn rate, its clips at their turn playback.
    const auto turn_rate = movement.stopped_turn_rate;
    const auto rotation = type_float_field(catalog, definition, "Rotation_Animation_Speed");
    if (turn_rate > 0.0F && rotation > 0.0F)
    {
        const auto turn_playback = turn_clip_playback(turn_rate, legacy_units::degrees_per_frame(rotation));
        // Left [0] and right [1]: BEGIN, the loop, END, and the HALF and QUARTER cycles.
        static constexpr std::array<std::array<AnimationRole, 5>, 2> sides{{
            {AnimationRole::turn_left_begin, AnimationRole::turn_left, AnimationRole::turn_left_end, AnimationRole::turn_left_half, AnimationRole::turn_left_quarter},
            {AnimationRole::turn_right_begin, AnimationRole::turn_right, AnimationRole::turn_right_end, AnimationRole::turn_right_half, AnimationRole::turn_right_quarter},
        }};
        std::array<float, 2> begin{};
        std::array<float, 2> loop{};
        std::array<float, 2> end{};
        std::array<float, 2> half{};
        std::array<float, 2> quarter{};
        for (std::size_t side = 0; side < sides.size(); ++side)
        {
            begin[side] = clip_seconds(sides[side][0], 0, turn_playback);
            loop[side] = clip_seconds(sides[side][1], 0, turn_playback);
            end[side] = clip_seconds(sides[side][2], 0, turn_playback);
            half[side] = clip_seconds(sides[side][3], 0, turn_playback);
            quarter[side] = clip_seconds(sides[side][4], 0, turn_playback);
        }
        if (std::ranges::all_of(begin,
                                [](const float value)
                                {
                                    return value > 0.0F;
                                }) &&
            std::ranges::all_of(loop,
                                [](const float value)
                                {
                                    return value > 0.0F;
                                }) &&
            std::ranges::all_of(end,
                                [](const float value)
                                {
                                    return value > 0.0F;
                                }))
        {
            movement.turn_begin_seconds = begin;
            movement.turn_loop_seconds = loop;
            movement.turn_end_seconds = end;
            movement.turn_half_seconds = half;
            movement.turn_quarter_seconds = quarter;
        }
    }
}

MovementProfile type_movement(const GameObjectCatalog& catalog, const XmlDefinition& definition, const bool land, const bool flyer, const float mode_multiplier)
{
    MovementProfile movement;
    if (flyer)
    {
        return movement;
    }
    const auto rate = [&](const std::string_view field, const auto convert)
    {
        const auto value = convert(type_float_field(catalog, definition, field));
        return value > 0.0F && std::isfinite(value) ? value : 0.0F;
    };
    // Team containers spell the fields with an underscore (Override_Acceleration, 40 stock uses);
    // no stock type uses both spellings.
    const auto either = [&](const std::string_view field, const std::string_view alias)
    {
        return catalog.field(definition, field).field != nullptr ? rate(field, legacy_units::acceleration_per_frame) : rate(alias, legacy_units::acceleration_per_frame);
    };
    movement.acceleration = either("OverrideAcceleration", "Override_Acceleration") * mode_multiplier;
    movement.deceleration = either("OverrideDeceleration", "Override_Deceleration") * mode_multiplier;
    const bool names_fraction = catalog.field(definition, "Min_Speed_Fraction_For_Turn").field != nullptr;
    // A fraction of top speed: no unit to convert.
    const auto fraction_of_speed = [](const float value)
    {
        return value;
    };
    const auto fraction = names_fraction ? std::clamp(rate("Min_Speed_Fraction_For_Turn", fraction_of_speed), 0.0F, 1.0F) : (land ? 0.0F : default_space_turn_speed_fraction);
    movement.turn_speed_fraction = fraction;
    if (land)
    {
        const auto stopped = rate("Stopped_Rate_Of_Turn", legacy_units::degrees_per_frame);
        movement.stopped_turn_rate = stopped > 0.0F ? stopped * mode_multiplier : type_turn_rate(catalog, definition, mode_multiplier);
        // Degrees; 180 and above never stop to turn (two stock types say 180, one 360).
        const auto* angle = catalog.field(definition, "Close_Enough_Angle_For_Move_Start").field;
        const auto degrees = angle != nullptr ? type_float_field(catalog, definition, "Close_Enough_Angle_For_Move_Start") : 0.0F;
        movement.turn_in_place_angle = degrees > 0.0F ? std::min(degrees, 180.0F) * pi / 180.0F : ground_turn_in_place_angle;
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
    const auto speed = legacy_units::speed_per_frame(type_float_field(catalog, definition, "Min_Speed"));
    return speed > 0.0F && std::isfinite(speed) ? std::min(speed, max_speed) : max_speed * 0.5F;
}

std::pair<std::string_view, std::string_view> death_clone_entry(const std::string_view text)
{
    const auto items = split_list(text);
    if (items.size() < 2U)
    {
        return {{}, items.empty() ? std::string_view{} : items.front()};
    }
    return {items[0], items[1]};
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
        for (const auto name : split_list(node->text))
        {
            info.categories.push_back(ascii_lower(name));
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
        for (const auto name : split_list(list->text))
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
    const auto value = TypeFields(catalog, definition, &warnings).number("Max_Speed").value_or(0.0F);
    return value > 0.0F ? legacy_units::speed_per_frame(value) * mode_multiplier : 0.0F;
}

std::vector<ModelAttachment> type_hardpoint_models(const ResourceManager& resources, std::vector<std::string>& warnings, const GameObjectCatalog& catalog, const XmlDefinition& type, const Model* host)
{
    std::vector<ModelAttachment> attachments;
    if (host == nullptr)
    {
        return attachments;
    }
    const auto text_of = [&](const XmlDefinition& definition, const std::string_view name)
    {
        const auto* node = catalog.field(definition, name).field;
        return node != nullptr ? std::string(ascii_trim(node->text)) : std::string{};
    };
    for (const auto& [name, hardpoint] : type_hardpoints(catalog, type))
    {
        if (hardpoint == nullptr)
        {
            continue;
        }
        const auto model_name = text_of(*hardpoint, "Model_To_Attach");
        if (model_name.empty())
        {
            continue;
        }
        const auto bone_name = ascii_lower(text_of(*hardpoint, "Attachment_Bone"));
        const auto bone = find_skeleton_bone(host->skeleton, bone_name);
        const auto path = resolve_model_path(resources.reader(), model_name);
        if (!bone || !path)
        {
            warnings.push_back(type.name + ": hardpoint " + name + " attaches " + model_name + (path ? " to missing bone '" + bone_name + "'" : " (file not found)"));
            continue;
        }
        const auto model = resources.get<Model>(*path);
        if (!model->ok())
        {
            warnings.push_back(*path + ": " + model->error().message);
            continue;
        }
        attachments.push_back({*path, std::shared_ptr<const Model>(model, &model->value()), *bone});
    }
    return attachments;
}

std::string attached_mesh_key(const std::string& model_path, const std::span<const ModelAttachment> attachments)
{
    auto key = ascii_lower(model_path);
    for (const auto& attachment : attachments)
    {
        key += "|" + ascii_lower(attachment.path) + "@" + std::to_string(attachment.bone);
    }
    return key;
}

// Expands the scene bounds by every instance's transformed mesh bounds and the terrain.
float model_footprint_radius(const ModelBounds& bounds, const Matrix4& transform)
{
    if (bounds.empty())
    {
        return 0.0F;
    }
    const auto width = bounds.maximum[0] - bounds.minimum[0];
    const auto depth = bounds.maximum[1] - bounds.minimum[1];
    const auto scale = std::sqrt(transform.values[0] * transform.values[0] + transform.values[1] * transform.values[1] + transform.values[2] * transform.values[2]);
    const auto radius = 0.25F * (width + depth) * scale;
    return std::isfinite(radius) && radius > 0.0F ? radius : 0.0F;
}

GroundFootprint model_ground_footprint(const ModelBounds& bounds, const Matrix4& transform, const std::uint32_t passability_type)
{
    GroundFootprint footprint;
    footprint.passability = passability_type;
    if (bounds.empty())
    {
        return footprint;
    }
    const auto scale = std::sqrt(transform.values[0] * transform.values[0] + transform.values[1] * transform.values[1] + transform.values[2] * transform.values[2]);
    const auto local_x = (bounds.minimum[0] + bounds.maximum[0]) * 0.5F;
    const auto local_y = (bounds.minimum[1] + bounds.maximum[1]) * 0.5F;
    // Row-vector transform: world = local.x * row0 + local.y * row1 + translation.
    footprint.center = {local_x * transform.values[0] + local_y * transform.values[4] + transform.values[12], local_x * transform.values[1] + local_y * transform.values[5] + transform.values[13],
                        transform.values[14]};
    footprint.half_length = std::max(0.0F, (bounds.maximum[0] - bounds.minimum[0]) * 0.5F * scale);
    footprint.half_width = std::max(0.0F, (bounds.maximum[1] - bounds.minimum[1]) * 0.5F * scale);
    footprint.heading = std::atan2(transform.values[1], transform.values[0]);
    if (!std::isfinite(footprint.center.x) || !std::isfinite(footprint.center.y) || !std::isfinite(footprint.half_length) || !std::isfinite(footprint.half_width) || !std::isfinite(footprint.heading))
    {
        footprint = GroundFootprint{};
        footprint.passability = passability_type;
    }
    return footprint;
}

float model_forward_heading(const Matrix4& transform) noexcept
{
    // Row 1 is the model's +Y axis in world space; forward is its negation.
    return std::atan2(-transform.values[5], -transform.values[4]);
}

} // namespace eaw
