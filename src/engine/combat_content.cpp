#include "combat_content.hpp"

#include "visual_scene_builder.hpp"

#include "assets/map_object_layout.hpp"

#include "formats/xml_document.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace eaw {
namespace {

constexpr std::string_view game_constants_path = "Data/XML/GameConstants.xml";

std::optional<float> parse_float(std::string_view text) noexcept
{
    text = ascii_trim(text);
    if (!text.empty() && text.front() == '+')
    {
        text.remove_prefix(1);
    }
    float value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end == text.data() || !std::isfinite(value))
    {
        return std::nullopt;
    }
    return value;
}

std::vector<std::string_view> split_list(const std::string_view text)
{
    std::vector<std::string_view> items;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const auto separator = text.find(',', start);
        const auto item = ascii_trim(text.substr(start, separator == std::string_view::npos ? std::string_view::npos : separator - start));
        if (!item.empty())
        {
            items.push_back(item);
        }
        if (separator == std::string_view::npos)
        {
            break;
        }
        start = separator + 1;
    }
    return items;
}

std::uint16_t intern(CombatContent& content, const std::string_view name)
{
    const auto key = ascii_lower(name);
    if (const auto found = content.type_ids.find(key); found != content.type_ids.end())
    {
        return found->second;
    }
    if (content.type_ids.size() >= std::numeric_limits<std::uint16_t>::max() - 1U)
    {
        content.warnings.push_back("too many damage and armor types; '" + std::string(name) + "' is treated as none");
        return 0;
    }
    const auto id = static_cast<std::uint16_t>(content.type_ids.size() + 1U);
    content.type_ids.emplace(key, id);
    return id;
}

// Reads one field of a type through its variant chain.
class FieldReader
{
public:
    FieldReader(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::vector<std::string>& warnings) : catalog_(catalog), definition_(definition), warnings_(warnings)
    {}

    [[nodiscard]] const XmlNode* node(const std::string_view field) const
    {
        return catalog_.field(definition_, field).field;
    }

    // A non-negative number, or `fallback` when the field is missing; bad values warn and give 0.
    [[nodiscard]] float amount(const std::string_view field, const float fallback = 0.0F) const
    {
        const auto* found = node(field);
        if (found == nullptr)
        {
            return fallback;
        }
        const auto value = parse_float(found->text);
        if (!value || *value < 0.0F)
        {
            warnings_.push_back(definition_.name + ": " + std::string(field) + " '" + found->text + "' is not a non-negative number");
            return 0.0F;
        }
        return *value;
    }

    // A finite number of either sign, 0 when the field is missing; bad values warn and give 0.
    [[nodiscard]] float signed_value(const std::string_view field) const
    {
        const auto* found = node(field);
        if (found == nullptr)
        {
            return 0.0F;
        }
        const auto value = parse_float(found->text);
        if (!value)
        {
            warnings_.push_back(definition_.name + ": " + std::string(field) + " '" + found->text + "' is not a number");
            return 0.0F;
        }
        return *value;
    }

    [[nodiscard]] bool flag(const std::string_view field, const bool fallback) const
    {
        const auto* found = node(field);
        if (found == nullptr)
        {
            return fallback;
        }
        const auto value = ascii_lower(ascii_trim(found->text));
        if (value == "yes" || value == "true" || value == "1")
        {
            return true;
        }
        if (value == "no" || value == "false" || value == "0")
        {
            return false;
        }
        warnings_.push_back(definition_.name + ": " + std::string(field) + " '" + found->text + "' is not a yes/no value");
        return fallback;
    }

    [[nodiscard]] std::uint16_t type_id(const std::string_view field, const CombatContent& content) const
    {
        const auto* found = node(field);
        if (found == nullptr)
        {
            return 0;
        }
        const auto name = ascii_trim(found->text);
        const auto id = content.type_ids.find(ascii_lower(name));
        // A type the damage table never names multiplies by 1 everywhere, exactly like "none".
        return id == content.type_ids.end() ? std::uint16_t{0} : id->second;
    }

private:
    const GameObjectCatalog& catalog_;
    const XmlDefinition& definition_;
    std::vector<std::string>& warnings_;
};

const XmlDefinition* find_projectile(const GameObjectCatalog& catalog, const std::string_view name)
{
    const auto candidates = catalog.find(name);
    for (const auto* candidate : candidates)
    {
        if (ascii_lower(candidate->type) == "projectile")
        {
            return candidate;
        }
    }
    return candidates.empty() ? nullptr : candidates.front();
}

} // namespace

CombatContent load_combat_content(const AssetReader& reader)
{
    CombatContent content;
    const auto bytes = reader.read(game_constants_path);
    if (!bytes)
    {
        content.warnings.push_back("damage modifiers unavailable: " + bytes.error().message);
        return content;
    }
    const auto document = read_xml(std::span<const std::byte>(*bytes));
    if (!document)
    {
        content.warnings.push_back(std::string(game_constants_path) + ": " + document.error().message);
        return content;
    }
    for (const auto& root : document->roots)
    {
        for (const auto& entry : root.children)
        {
            if (ascii_lower(entry.name) != "damage_to_armor_mod")
            {
                continue;
            }
            const auto parts = split_list(entry.text);
            const auto modifier = parts.size() == 3 ? parse_float(parts[2]) : std::nullopt;
            if (!modifier || *modifier < 0.0F)
            {
                content.warnings.push_back(std::string(game_constants_path) + ": Damage_To_Armor_Mod '" + entry.text + "' is not 'damage, armor, non-negative modifier'");
                continue;
            }
            const auto damage = intern(content, parts[0]);
            const auto armor = intern(content, parts[1]);
            content.damage_table.set(damage, armor, *modifier);
        }
    }
    return content;
}

namespace {

// The shot of a projectile type: damage, damage type, speed (scaled like unit speeds), flight limit,
// and what it damages.
void read_projectile(const FieldReader& projectile, const std::string_view name, const CombatContent& content, WeaponProfile& weapon)
{
    weapon.projectile_visual = game_object_name_crc(name);
    weapon.damage = projectile.amount("Projectile_Damage");
    weapon.damage_type = projectile.type_id("Damage_Type", content);
    weapon.projectile_speed = projectile.amount("Max_Speed") * provisional_max_speed_scale;
    weapon.max_flight_distance = projectile.amount("Projectile_Max_Flight_Distance");
    weapon.damages_shield = projectile.flag("Projectile_Does_Shield_Damage", true);
    weapon.damages_health = projectile.flag("Projectile_Does_Hitpoint_Damage", true);
}

// A bone of the model's bind pose by name (case-insensitive): its position in the entity frame
// (models face -Y, so forward = -Y and left = +X) and the horizontal angle of its X axis from
// forward, counter-clockwise.
struct BonePlacement
{
    std::uint32_t index{};
    LocalPoint point;
    float direction{};
};

std::optional<BonePlacement> find_bone(const Skeleton* skeleton, const std::vector<Matrix4>& pose, const std::string_view name, const float scale)
{
    if (skeleton == nullptr || name.empty())
    {
        return std::nullopt;
    }
    const auto wanted = ascii_lower(ascii_trim(name));
    for (std::size_t index = 0; index < skeleton->bones.size() && index < pose.size(); ++index)
    {
        if (ascii_lower(skeleton->bones[index].name) != wanted)
        {
            continue;
        }
        const auto& m = pose[index].values;
        BonePlacement placement;
        placement.index = static_cast<std::uint32_t>(index);
        placement.point = {-m[13] * scale, m[12] * scale, m[14] * scale};
        placement.direction = std::atan2(m[0], -m[1]);
        return placement;
    }
    return std::nullopt;
}

HardpointProfile::Kind hardpoint_kind(const std::string_view type)
{
    const auto folded = ascii_lower(ascii_trim(type));
    if (folded.find("weapon") != std::string::npos)
    {
        return HardpointProfile::Kind::weapon;
    }
    if (folded.find("shield_generator") != std::string::npos)
    {
        return HardpointProfile::Kind::shield_generator;
    }
    if (folded.find("engine") != std::string::npos)
    {
        return HardpointProfile::Kind::engine;
    }
    return HardpointProfile::Kind::other;
}

const XmlDefinition* find_hardpoint(const GameObjectCatalog& catalog, const std::string_view name)
{
    const auto candidates = catalog.find(name);
    for (const auto* candidate : candidates)
    {
        if (ascii_lower(candidate->type) == "hardpoint")
        {
            return candidate;
        }
    }
    return candidates.empty() ? nullptr : candidates.front();
}

} // namespace

CombatProfile combat_profile(const GameObjectCatalog& catalog, const XmlDefinition& definition, const CombatContent& content, std::vector<std::string>& warnings, const Skeleton* skeleton,
                             const float model_scale)
{
    const auto pose = skeleton != nullptr ? skeleton_bind_pose(*skeleton) : std::vector<Matrix4>{};
    const FieldReader type(catalog, definition, warnings);
    CombatProfile profile;
    profile.health = type.amount("Tactical_Health");
    profile.shield = type.amount("Shield_Points");
    profile.shield_regen_per_second = type.amount("Shield_Refresh_Rate");
    profile.armor_type = type.type_id("Armor_Type", content);
    profile.shield_armor_type = type.type_id("Shield_Armor_Type", content);
    // Below the origin is legitimate (stock Underworld_Hero_Shuttle_Landing uses -5.0).
    profile.aim_height = type.signed_value("Ranged_Target_Z_Adjust");
    const auto range = type.amount("Targeting_Max_Attack_Distance");
    // Presentation: the Particle object its death plays (the first Death_Explosions entry).
    if (const auto* explosions = type.node("Death_Explosions"))
    {
        if (const auto names = split_list(explosions->text); !names.empty())
        {
            profile.death_effect = game_object_name_crc(names.front());
        }
    }

    // The type's own weapon: the first of Projectile_Types.
    if (const auto* projectiles = type.node("Projectile_Types"); projectiles != nullptr && range > 0.0F)
    {
        if (const auto names = split_list(projectiles->text); !names.empty())
        {
            if (const auto* projectile_type = find_projectile(catalog, names.front()))
            {
                WeaponProfile weapon;
                weapon.range = range;
                weapon.recharge_seconds = type.amount("Projectile_Fire_Recharge_Seconds");
                weapon.pulse_count = std::max(1U, static_cast<std::uint32_t>(std::min(type.amount("Projectile_Fire_Pulse_Count", 1.0F), 1000.0F)));
                weapon.pulse_delay_seconds = type.amount("Projectile_Fire_Pulse_Delay_Seconds");
                read_projectile(FieldReader(catalog, *projectile_type, warnings), projectile_type->name, content, weapon);
                weapon.mount.up = profile.aim_height;
                profile.weapons.push_back(weapon);
            }
            else
            {
                warnings.push_back(definition.name + ": projectile type '" + std::string(names.front()) + "' is not defined");
            }
        }
    }

    // Weapon hardpoints (ships and space structures): every HardPoints entry whose Type is a weapon.
    // The recharge is the midpoint of Fire_Min/Max_Recharge_Seconds (the original presumably picks
    // within the range at random; a fixed value keeps the simulation deterministic until a seeded
    // random stream exists). A hardpoint's Damage_Type overrides its projectile's.
    if (const auto* hardpoints = type.node("HardPoints"))
    {
        for (const auto name : split_type_names(hardpoints->text))
        {
            const auto* hardpoint = find_hardpoint(catalog, name);
            if (hardpoint == nullptr)
            {
                warnings.push_back(definition.name + ": hardpoint '" + std::string(name) + "' is not defined");
                continue;
            }
            const FieldReader point(catalog, *hardpoint, warnings);
            const auto* kind_node = point.node("Type");
            const auto kind = kind_node != nullptr ? hardpoint_kind(kind_node->text) : HardpointProfile::Kind::other;
            const auto bone_name = [&](const std::string_view field)
            {
                const auto* node = point.node(field);
                return node != nullptr ? std::string(ascii_trim(node->text)) : std::string{};
            };
            const auto attachment = find_bone(skeleton, pose, bone_name("Attachment_Bone"), model_scale);
            const auto fire_bone = find_bone(skeleton, pose, bone_name("Fire_Bone_A"), model_scale);
            HardpointProfile part;
            part.kind = kind;
            part.health = point.flag("Is_Destroyable", true) ? point.amount("Health") : 0.0F;
            part.targetable = point.flag("Is_Targetable", true);
            part.offset = attachment ? attachment->point : fire_bone ? fire_bone->point : LocalPoint{};
            if (const auto damage = find_bone(skeleton, pose, bone_name("Damage_Particles"), model_scale))
            {
                part.damage_bone = static_cast<std::int32_t>(damage->index);
            }
            if (const auto decal = find_bone(skeleton, pose, bone_name("Damage_Decal"), model_scale))
            {
                part.decal_bone = static_cast<std::int32_t>(decal->index);
            }
            if (const auto engine = find_bone(skeleton, pose, bone_name("Engine_Particles"), model_scale))
            {
                part.engine_bone = static_cast<std::int32_t>(engine->index);
            }
            profile.hardpoints.push_back(part);
            const auto part_index = static_cast<std::int32_t>(profile.hardpoints.size() - 1U);
            if (kind != HardpointProfile::Kind::weapon)
            {
                continue;
            }
            const auto* projectile_name = point.node("Fire_Projectile_Type");
            const auto* projectile_type = projectile_name != nullptr ? find_projectile(catalog, ascii_trim(projectile_name->text)) : nullptr;
            if (projectile_type == nullptr)
            {
                warnings.push_back(hardpoint->name + ": weapon hardpoint without a defined Fire_Projectile_Type");
                continue;
            }
            WeaponProfile weapon;
            weapon.range = point.amount("Fire_Range_Distance", range);
            const auto minimum = point.amount("Fire_Min_Recharge_Seconds");
            weapon.recharge_seconds = (minimum + point.amount("Fire_Max_Recharge_Seconds", minimum)) * 0.5F;
            weapon.pulse_count = std::max(1U, static_cast<std::uint32_t>(std::min(point.amount("Fire_Pulse_Count", 1.0F), 1000.0F)));
            weapon.pulse_delay_seconds = point.amount("Fire_Pulse_Delay_Seconds");
            read_projectile(FieldReader(catalog, *projectile_type, warnings), projectile_type->name, content, weapon);
            if (point.node("Damage_Type") != nullptr)
            {
                weapon.damage_type = point.type_id("Damage_Type", content);
            }
            weapon.hardpoint = part_index;
            weapon.mount = fire_bone ? fire_bone->point : part.offset;
            const auto cone = point.amount("Fire_Cone_Width");
            if (fire_bone && cone > 0.0F && cone < 360.0F)
            {
                weapon.cone_direction = fire_bone->direction;
                weapon.cone_half_angle = cone * 0.5F * 3.14159265F / 180.0F;
            }
            if (weapon.range > 0.0F)
            {
                profile.weapons.push_back(weapon);
            }
        }
    }
    return profile;
}

Hostility provisional_map_hostility() noexcept
{
    Hostility hostility;
    for (std::uint32_t first = 0; first < Hostility::max_owners; ++first)
    {
        for (std::uint32_t second = first + 1; second < Hostility::max_owners; ++second)
        {
            hostility.set(first, second, first != neutral_map_owner && second != neutral_map_owner);
        }
    }
    return hostility;
}

} // namespace eaw
