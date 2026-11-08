#include "combat_content.hpp"

#include "visual_scene_builder.hpp"

#include "eaw/map_object_layout.hpp"

#include "eaw/xml_document.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace eaw {
namespace {

constexpr std::string_view game_constants_path = "Data/XML/GameConstants.xml";

std::string_view trim(std::string_view text) noexcept
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

std::string fold(const std::string_view text)
{
    std::string folded(text);
    std::ranges::transform(folded, folded.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return folded;
}

std::optional<float> parse_float(std::string_view text) noexcept
{
    text = trim(text);
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
        const auto item = trim(text.substr(start, separator == std::string_view::npos ? std::string_view::npos : separator - start));
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
    const auto key = fold(name);
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
        const auto value = fold(trim(found->text));
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
        const auto name = trim(found->text);
        const auto id = content.type_ids.find(fold(name));
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
        if (fold(candidate->type) == "projectile")
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
    const auto document = XmlDocument::parse(std::span<const std::byte>(*bytes));
    if (!document)
    {
        content.warnings.push_back(std::string(game_constants_path) + ": " + document.error().message);
        return content;
    }
    for (const auto& root : document->roots)
    {
        for (const auto& entry : root.children)
        {
            if (fold(entry.name) != "damage_to_armor_mod")
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
void read_projectile(const FieldReader& projectile, const CombatContent& content, WeaponProfile& weapon)
{
    weapon.damage = projectile.amount("Projectile_Damage");
    weapon.damage_type = projectile.type_id("Damage_Type", content);
    weapon.projectile_speed = projectile.amount("Max_Speed") * provisional_max_speed_scale;
    weapon.max_flight_distance = projectile.amount("Projectile_Max_Flight_Distance");
    weapon.damages_shield = projectile.flag("Projectile_Does_Shield_Damage", true);
    weapon.damages_health = projectile.flag("Projectile_Does_Hitpoint_Damage", true);
}

const XmlDefinition* find_hardpoint(const GameObjectCatalog& catalog, const std::string_view name)
{
    const auto candidates = catalog.find(name);
    for (const auto* candidate : candidates)
    {
        if (fold(candidate->type) == "hardpoint")
        {
            return candidate;
        }
    }
    return candidates.empty() ? nullptr : candidates.front();
}

} // namespace

CombatProfile combat_profile(const GameObjectCatalog& catalog, const XmlDefinition& definition, const CombatContent& content, std::vector<std::string>& warnings)
{
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
                read_projectile(FieldReader(catalog, *projectile_type, warnings), content, weapon);
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
            const auto* kind = point.node("Type");
            if (kind == nullptr || fold(trim(kind->text)).find("weapon") == std::string::npos)
            {
                continue;
            }
            const auto* projectile_name = point.node("Fire_Projectile_Type");
            const auto* projectile_type = projectile_name != nullptr ? find_projectile(catalog, trim(projectile_name->text)) : nullptr;
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
            read_projectile(FieldReader(catalog, *projectile_type, warnings), content, weapon);
            if (point.node("Damage_Type") != nullptr)
            {
                weapon.damage_type = point.type_id("Damage_Type", content);
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
