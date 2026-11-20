#include "battle/combat_content.hpp"

#include "math/scalar.hpp"
#include "core/string_utils.hpp"
#include "formats/xml_document.hpp"
#include "assets/legacy_units.hpp"
#include "assets/skeleton.hpp"
#include "assets/type_fields.hpp"
#include "assets/type_hardpoints.hpp"
#include "battle/object_types.hpp"

#include "junk/local_point.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace eaw {
namespace {

constexpr std::string_view game_constants_path = "Data/XML/GameConstants.xml";
constexpr std::string_view category_enum_path = "Data/XML/Enum/GameObjectCategoryType.xml";
constexpr std::string_view property_enum_path = "Data/XML/Enum/GameObjectPropertiesType.xml";

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

// Reads a bit field enum (entries such as <Fighter> 0x00000001 </Fighter>) into `bits`.
void load_enum_bits(const AssetReader& reader, const std::string_view path, std::unordered_map<std::string, std::uint32_t>& bits_by_name, CombatContent& content)
{
    const auto bytes = reader.read(path);
    if (!bytes)
    {
        content.warnings.push_back(std::string(path) + " unavailable: " + bytes.error().message);
        return;
    }
    const auto document = read_xml(std::span<const std::byte>(*bytes));
    if (!document)
    {
        content.warnings.push_back(std::string(path) + ": " + document.error().message);
        return;
    }
    for (const auto& root : document->roots)
    {
        for (const auto& entry : root.children)
        {
            auto text = ascii_trim(entry.text);
            if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            {
                text.remove_prefix(2);
            }
            // 64-bit wide: the installed category enum adds All (0xFFFFFFFFFFFFFFFF) and None (0).
            std::uint64_t bits{};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), bits, 16);
            if (error != std::errc{} || end != text.data() + text.size())
            {
                content.warnings.push_back(std::string(path) + ": " + entry.name + " '" + entry.text + "' is not a bit mask");
                continue;
            }
            // The first definition of a name wins (the stock file keeps an older numbering only in a
            // comment). The stock bits fit in 32.
            bits_by_name.emplace(ascii_lower(entry.name), static_cast<std::uint32_t>(bits));
        }
    }
}

// Reads one field of a type through its variant chain.
// "category, distance" lines (Targeting_Fire_Inaccuracy, Fire_Inaccuracy_Distance).
// A run of names and the number after it, in a "names, number, names, number" list: a name run may
// be a mask written with '|' (several items). `names` is the run as written between them, for
// CombatContent::categories and properties; a run without a number ends the list without one.
struct NamedValue
{
    std::string names;
    std::optional<float> value;
};

std::vector<NamedValue> named_values(const std::vector<std::string_view>& items)
{
    std::vector<NamedValue> groups;
    NamedValue current;
    for (const auto item : items)
    {
        if (const auto number = parse_xml_number(item))
        {
            current.value = number;
            groups.push_back(std::move(current));
            current = {};
            continue;
        }
        current.names += current.names.empty() ? std::string(item) : "|" + std::string(item);
    }
    if (!current.names.empty())
    {
        groups.push_back(std::move(current));
    }
    return groups;
}

std::vector<CategoryInaccuracy> inaccuracy(const TypeFields& fields, const std::string_view field, const CombatContent& content)
{
    std::vector<CategoryInaccuracy> list;
    for (const auto* line : fields.nodes(field))
    {
        // "categories, distance": the categories may be a mask written with '|' (several names).
        const auto groups = named_values(split_list(line->text));
        const auto distance = groups.size() == 1U ? groups.front().value : std::nullopt;
        std::vector<std::string> unknown;
        const auto categories = groups.size() == 1U ? content.categories(groups.front().names, &unknown) : 0U;
        if (!distance || *distance < 0.0F || categories == 0)
        {
            fields.warn(field, line->text, "'category, non-negative distance'");
            continue;
        }
        list.push_back({categories, *distance});
    }
    return list;
}

// A damage or armor type's id (CombatContent::type_ids); 0 when absent.
std::uint16_t type_id(const TypeFields& fields, const std::string_view field, const CombatContent& content)
{
    const auto* found = fields.node(field);
    if (found == nullptr)
    {
        return 0;
    }
    const auto id = content.type_ids.find(ascii_lower(ascii_trim(found->text)));
    // A type the damage table never names multiplies by 1 everywhere, exactly like "none".
    return id == content.type_ids.end() ? std::uint16_t{0} : id->second;
}

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
    const auto multiplier = [&](const XmlNode& entry, float& value)
    {
        const auto parsed = parse_xml_number(ascii_trim(entry.text));
        if (!parsed || *parsed <= 0.0F)
        {
            content.warnings.push_back(std::string(game_constants_path) + ": " + entry.name + " '" + entry.text + "' is not a positive number");
            return;
        }
        value = *parsed;
    };
    for (const auto& root : document->roots)
    {
        for (const auto& entry : root.children)
        {
            const auto name = ascii_lower(entry.name);
            if (name == "object_max_speed_multiplier_land")
            {
                multiplier(entry, content.max_speed_multiplier.land);
            }
            else if (name == "object_max_speed_multiplier_space")
            {
                multiplier(entry, content.max_speed_multiplier.space);
            }
            else if (name == "shieldrechargeintervalinsecs")
            {
                multiplier(entry, content.shield_recharge_interval_seconds);
            }
            else if (name == "turninplaceslowdowncorvette")
            {
                multiplier(entry, content.turn_in_place_slowdown_corvette);
            }
            else if (name == "turninplaceslowdownfrigate")
            {
                multiplier(entry, content.turn_in_place_slowdown_frigate);
            }
            else if (name == "turninplaceslowdowncapital")
            {
                multiplier(entry, content.turn_in_place_slowdown_capital);
            }
            else if (name == "object_max_health_multiplier_land")
            {
                multiplier(entry, content.max_health_multiplier.land);
            }
            else if (name == "object_max_health_multiplier_space")
            {
                multiplier(entry, content.max_health_multiplier.space);
            }
            if (name != "damage_to_armor_mod")
            {
                continue;
            }
            const auto parts = split_list(entry.text);
            const auto modifier = parts.size() == 3 ? parse_xml_number(parts[2]) : std::nullopt;
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
    load_enum_bits(reader, category_enum_path, content.category_bits, content);
    load_enum_bits(reader, property_enum_path, content.property_bits, content);
    return content;
}

namespace {

// The bits of the names in a list (separated by `|`, commas, or whitespace).
std::uint32_t list_bits(const std::unordered_map<std::string, std::uint32_t>& bits_by_name, const std::string_view list, std::vector<std::string>* unknown)
{
    std::uint32_t bits = 0;
    for (const auto name : split_list(list))
    {
        if (const auto found = bits_by_name.find(ascii_lower(name)); found != bits_by_name.end())
        {
            bits |= found->second;
        }
        else if (unknown != nullptr)
        {
            unknown->emplace_back(name);
        }
    }
    return bits;
}

// The type's Targeting_Priority_Set, resolved against the Priority_Set definitions.
TargetingPriorities targeting_priorities(const GameObjectCatalog& catalog, const XmlDefinition& type, const CombatContent& content, std::vector<std::string>& warnings)
{
    TargetingPriorities priorities;
    const auto* name = catalog.field(type, "Targeting_Priority_Set").field;
    const auto set_name = name != nullptr ? ascii_trim(name->text) : std::string_view{};
    if (set_name.empty())
    {
        return priorities;
    }
    const XmlDefinition* set = nullptr;
    for (const auto* candidate : catalog.find(set_name))
    {
        if (ascii_equal_ignoring_case(candidate->type, "Priority_Set"))
        {
            set = candidate;
        }
    }
    if (set == nullptr)
    {
        warnings.push_back(type.name + ": Targeting_Priority_Set '" + std::string(set_name) + "' is not defined");
        return priorities;
    }
    const auto text_of = [&](const std::string_view field)
    {
        const auto* node = catalog.field(*set, field).field;
        return node != nullptr ? std::string_view(node->text) : std::string_view{};
    };
    // "name, rank, name, rank, ...": a name is a category or a property (or a mask of them written
    // with '|', several names before one rank).
    for (const auto& group : named_values(split_list(text_of("Attack_Priorities"))))
    {
        TargetPriority entry{content.categories(group.names), content.properties(group.names), group.value.value_or(0.0F)};
        if (!group.value || (entry.categories == 0 && entry.properties == 0))
        {
            warnings.push_back(set->name + ": Attack_Priorities entry '" + group.names + "' is not 'category or property, rank'");
            continue;
        }
        priorities.ranks.push_back(entry);
    }
    std::ranges::stable_sort(priorities.ranks, {}, &TargetPriority::rank);
    priorities.excluded_categories = content.categories(text_of("Category_Exclusions"));
    priorities.excluded_properties = content.properties(text_of("Property_Exclusions"));
    for (const auto excluded : split_list(text_of("Unit_Exclusions")))
    {
        priorities.excluded_types.push_back(game_object_name_crc(excluded));
    }
    return priorities;
}

} // namespace

std::uint32_t CombatContent::categories(const std::string_view list, std::vector<std::string>* unknown) const
{
    return list_bits(category_bits, list, unknown);
}

std::uint32_t CombatContent::properties(const std::string_view list, std::vector<std::string>* unknown) const
{
    return list_bits(property_bits, list, unknown);
}

namespace {

// The shot of a projectile type: damage, damage type, speed (scaled like unit speeds), flight limit,
// and what it damages.
void read_projectile(const TypeFields& projectile, const std::string_view name, const CombatContent& content, WeaponProfile& weapon)
{
    weapon.projectile_visual = game_object_name_crc(name);
    weapon.damage = projectile.amount("Projectile_Damage");
    weapon.damage_type = type_id(projectile, "Damage_Type", content);
    weapon.projectile_speed = legacy_units::speed_per_frame(projectile.amount("Max_Speed"));
    weapon.max_flight_distance = projectile.amount("Projectile_Max_Flight_Distance");
    weapon.damages_shield = projectile.flag("Projectile_Does_Shield_Damage", true);
    weapon.damages_health = projectile.flag("Projectile_Does_Hitpoint_Damage", true);
    weapon.blast.damage = projectile.amount("Projectile_Blast_Area_Damage");
    weapon.blast.friendly_damage = projectile.amount("Friendly_Damage_Amount");
    weapon.max_lifetime = projectile.amount("Projectile_Max_Lifetime");
    weapon.scan_range = projectile.amount("Projectile_Max_Scan_Range");
    weapon.random_hardpoints = projectile.flag("Projectile_Damages_Random_Hard_Points", false);
    if (const auto* launch = projectile.node("Projectile_Object_Creation_Particle"); launch != nullptr && !ascii_trim(launch->text).empty())
    {
        weapon.launch_effect = game_object_name_crc(ascii_trim(launch->text));
    }
    weapon.blast.range = projectile.amount("Projectile_Blast_Area_Range");
    if (projectile.flag("Projectile_Blast_Area_Dropoff", false))
    {
        // Stock tiers are 3, 4, or 5, always given with the dropoff; without them, 3 (an engine choice).
        weapon.blast.tiers = static_cast<std::uint32_t>(std::clamp(projectile.amount("Projectile_Blast_Area_Dropoff_Tiers", 3.0F), 1.0F, 100.0F));
    }
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

// A model's MuzzleA_nn bones (the unit's own weapon's muzzles, by the stock naming: B and later
// letters belong to hardpoints and second weapons), in number order.
std::vector<BonePlacement> muzzle_bones(const Skeleton* skeleton, const std::vector<Matrix4>& pose, const float scale)
{
    constexpr std::string_view prefix = "muzzlea_";
    std::vector<std::pair<std::uint32_t, BonePlacement>> found;
    for (std::size_t index = 0; skeleton != nullptr && index < skeleton->bones.size(); ++index)
    {
        const auto name = ascii_lower(ascii_trim(skeleton->bones[index].name));
        std::uint32_t number = 0;
        if (!name.starts_with(prefix))
        {
            continue;
        }
        const auto digits = std::string_view(name).substr(prefix.size());
        if (const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number); error != std::errc{} || end != digits.data() + digits.size())
        {
            continue;
        }
        if (const auto placement = find_bone(skeleton, pose, skeleton->bones[index].name, scale))
        {
            found.emplace_back(number, *placement);
        }
    }
    std::ranges::stable_sort(found, {}, &std::pair<std::uint32_t, BonePlacement>::first);
    std::vector<BonePlacement> muzzles;
    for (const auto& [number, placement] : found)
    {
        muzzles.push_back(placement);
    }
    return muzzles;
}

// A turret's rate (XML Turret_Rotate_Speed per frame, default_turret_rotate_speed without it) and
// limit (half of Turret_Rotate_Extent_Degrees either way, a guess; all around from 360).
void read_turret_motion(const TypeFields& fields, TurretProfile& turret)
{
    const auto speed = fields.amount("Turret_Rotate_Speed");
    turret.rate = legacy_units::degrees_per_frame(speed > 0.0F ? speed : default_turret_rotate_speed);
    if (const auto arc = fields.amount("Turret_Rotate_Extent_Degrees"); arc > 0.0F && arc < 360.0F)
    {
        turret.yaw_limit = arc * 0.5F * pi / 180.0F;
    }
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

} // namespace

CombatProfile combat_profile(const GameObjectCatalog& catalog, const XmlDefinition& definition, const CombatContent& content, std::vector<std::string>& warnings, const Skeleton* skeleton,
                             const float model_scale, const float health_multiplier, const std::span<const ModelAttachment> attachments)
{
    const auto pose = skeleton != nullptr ? skeleton_bind_pose(*skeleton) : std::vector<Matrix4>{};
    const TypeFields type(catalog, definition, &warnings);
    CombatProfile profile;
    profile.health = type.amount("Tactical_Health") * health_multiplier;
    profile.collidable = type.flag("Collidable_By_Projectile_Living", true);
    profile.targetable = type.flag("Is_Valid_Target", true);
    profile.immune = type.flag("Immune_To_Damage", false);
    profile.dies_with_hardpoints = type.flag("Should_Be_Destroyed_When_All_Hardpoints_Destroyed", true);
    profile.attack_move_response_range = type.amount("Attack_Move_Response_Range");
    profile.guard_chase_range = type.amount("Guard_Chase_Range");
    profile.stickiness_seconds = type.amount("Targeting_Stickiness_Time_Threshold");
    // The turret's bone and the bones it carries (weapons there turn with it).
    std::vector<bool> on_turret;
    std::optional<BonePlacement> barrel;
    if (const auto* bone = type.node("Turret_Bone_Name"); bone != nullptr && !ascii_trim(bone->text).empty())
    {
        TurretProfile turret;
        if (const auto pivot = find_bone(skeleton, pose, bone->text, model_scale))
        {
            turret.pivot = pivot->point;
            std::vector<std::int32_t> parents;
            for (const auto& each : skeleton->bones)
            {
                parents.push_back(each.parent);
            }
            const std::array roots{pivot->index};
            on_turret = bones_in_subtrees(parents, roots);
            if (const auto* barrel_name = type.node("Barrel_Bone_Name"))
            {
                barrel = find_bone(skeleton, pose, barrel_name->text, model_scale);
            }
        }
        read_turret_motion(type, turret);
        turret.turns_body = type.flag("Should_Cause_Limited_Turrets_To_Turn", true);
        profile.turret = turret;
    }
    if (type.flag("Spin_Away_On_Death", false))
    {
        profile.spin_chance = std::min(type.amount("Spin_Away_On_Death_Chance"), 1.0F);
        profile.spin_seconds = type.amount("Spin_Away_On_Death_Time");
        if (const auto* explosion = type.node("Spin_Away_On_Death_Explosion"); explosion != nullptr && !ascii_trim(explosion->text).empty())
        {
            profile.spin_effect = game_object_name_crc(ascii_trim(explosion->text));
        }
    }
    if (const auto* death_projectiles = type.node("Death_Projectiles"))
    {
        if (const auto names = split_list(death_projectiles->text); !names.empty())
        {
            if (const auto* blast = find_projectile(catalog, names.front()))
            {
                WeaponProfile shot;
                read_projectile(TypeFields(catalog, *blast, &warnings), blast->name, content, shot);
                profile.death_blast = shot.blast;
                profile.death_blast_damage_type = shot.damage_type;
                profile.death_blast_damages_shield = shot.damages_shield;
                profile.death_blast_damages_health = shot.damages_health;
                profile.death_blast_visual = shot.projectile_visual;
            }
            else
            {
                warnings.push_back(definition.name + ": Death_Projectiles names undefined projectile '" + std::string(names.front()) + "'");
            }
        }
    }
    profile.type_key = game_object_name_crc(definition.name);
    profile.targeting = targeting_priorities(catalog, definition, content, warnings);
    if (const auto* flags = type.node("Property_Flags"))
    {
        std::vector<std::string> unknown;
        profile.properties = content.properties(flags->text, &unknown);
        for (const auto& name : unknown)
        {
            warnings.push_back(definition.name + ": Property_Flags names unknown property '" + name + "'");
        }
    }
    if (const auto* mask = type.node("CategoryMask"))
    {
        std::vector<std::string> unknown;
        profile.categories = content.categories(mask->text, &unknown);
        for (const auto& name : unknown)
        {
            warnings.push_back(definition.name + ": CategoryMask names unknown category '" + name + "'");
        }
    }
    // The first of a reader's Projectile_Appearance_Delay_Frames ("15" or "15,20"), in seconds.
    const auto appearance_delay = [](const TypeFields& reader)
    {
        const auto* node = reader.node("Projectile_Appearance_Delay_Frames");
        const auto items = node != nullptr ? split_list(node->text) : std::vector<std::string_view>{};
        const auto frames = items.empty() ? std::nullopt : parse_xml_number(items.front());
        return frames && *frames > 0.0F ? legacy_units::frames(std::min(*frames, 3000.0F)) : 0.0F;
    };
    // Categories a reader's weapons never fire at (unknown names are reported).
    const auto restricted = [&](const TypeFields& reader, const std::string_view owner, const std::string_view field)
    {
        const auto* node = reader.node(field);
        if (node == nullptr)
        {
            return 0U;
        }
        std::vector<std::string> unknown;
        const auto bits = content.categories(node->text, &unknown);
        for (const auto& name : unknown)
        {
            warnings.push_back(std::string(owner) + ": " + std::string(field) + " names unknown category '" + name + "'");
        }
        return bits;
    };
    const auto unit_excluded = restricted(type, definition.name, "Fire_Category_Restrictions") | restricted(type, definition.name, "Attack_Category_Restrictions");
    // Where the type's own weapons stray, by target category; a fixed radius covers the rest.
    auto unit_inaccuracy = inaccuracy(type, "Targeting_Fire_Inaccuracy", content);
    if (unit_inaccuracy.empty())
    {
        unit_inaccuracy = inaccuracy(type, "Fire_Inaccuracy_Distance", content);
    }
    if (const auto fixed = type.amount("Targeting_Fire_Inaccuracy_Fixed_Radius"); fixed > 0.0F)
    {
        unit_inaccuracy.push_back({all_categories, fixed});
    }
    profile.shield = type.amount("Shield_Points");
    profile.shield_refresh = type.amount("Shield_Refresh_Rate");
    profile.shield_refresh_seconds = content.shield_recharge_interval_seconds;
    profile.armor_type = type_id(type, "Armor_Type", content);
    profile.shield_armor_type = type_id(type, "Shield_Armor_Type", content);
    // Below the origin is legitimate (stock Underworld_Hero_Shuttle_Landing uses -5.0).
    profile.aim_height = type.signed_value("Ranged_Target_Z_Adjust");
    const auto range = type.amount("Targeting_Max_Attack_Distance");
    // Presentation: the sound event of a shot (a weapon hardpoint's Fire_SFXEvent, else the type's
    // SFXEvent_Fire).
    const auto sound_of = [](const TypeFields& reader, const std::string_view field, const std::uint32_t fallback)
    {
        const auto* node = reader.node(field);
        const auto name = node != nullptr ? ascii_trim(node->text) : std::string_view{};
        return name.empty() ? fallback : game_object_name_crc(name);
    };
    const auto type_fire_sound = sound_of(type, "SFXEvent_Fire", 0U);
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
                // The cadence is the shooter's; units with several Projectile_Types (Bossk, MAL, the
                // Disruptor merc) set none and carry it on each projectile instead.
                const TypeFields shot(catalog, *projectile_type, &warnings);
                const auto cadence = [&](const std::string_view field, const float fallback)
                {
                    return type.node(field) != nullptr ? type.amount(field, fallback) : shot.amount(field, fallback);
                };
                weapon.recharge_seconds = cadence("Projectile_Fire_Recharge_Seconds", 0.0F);
                weapon.pulse_count = std::max(1U, static_cast<std::uint32_t>(std::min(cadence("Projectile_Fire_Pulse_Count", 1.0F), 1000.0F)));
                weapon.pulse_delay_seconds = cadence("Projectile_Fire_Pulse_Delay_Seconds", 0.0F);
                read_projectile(shot, projectile_type->name, content, weapon);
                weapon.fire_sound = type_fire_sound;
                // Where the shots leave: the model's MuzzleA_nn bones in turn, else a turret's barrel
                // bone, else the body at the aim height. Infantry and heroes keep the body: their
                // bind pose is not a firing pose (arms spread), so their muzzle bones are far off
                // (an engine choice until shots leave from the animated pose).
                weapon.mount.up = profile.aim_height;
                const auto muzzles = muzzle_bones(skeleton, pose, model_scale);
                if (!muzzles.empty() && (profile.categories & content.categories("Infantry | LandHero")) == 0)
                {
                    weapon.mount = muzzles.front().point;
                    for (std::size_t muzzle = 1; muzzle < muzzles.size(); ++muzzle)
                    {
                        weapon.extra_mounts.push_back(muzzles[muzzle].point);
                    }
                }
                else if (barrel)
                {
                    weapon.mount = barrel->point;
                }
                // A turret carries the unit's weapon and turns it.
                weapon.on_turret = profile.turret.has_value();
                weapon.inaccuracy = unit_inaccuracy;
                weapon.min_range = std::min(type.amount("Targeting_Min_Attack_Distance"), range);
                weapon.appearance_delay_seconds = appearance_delay(type);
                weapon.excluded_categories = unit_excluded;
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
    for (const auto& [name, hardpoint] : type_hardpoints(catalog, definition))
    {
        if (hardpoint == nullptr)
        {
            warnings.push_back(definition.name + ": hardpoint '" + name + "' is not defined");
            continue;
        }
        const TypeFields point(catalog, *hardpoint, &warnings);
        const auto* kind_node = point.node("Type");
        const auto kind = kind_node != nullptr ? hardpoint_kind(kind_node->text) : HardpointProfile::Kind::other;
        const auto bone_name = [&](const std::string_view field)
        {
            const auto* node = point.node(field);
            return node != nullptr ? std::string(ascii_trim(node->text)) : std::string{};
        };
        const auto attachment = find_bone(skeleton, pose, bone_name("Attachment_Bone"), model_scale);
        // The hardpoint's attached model, its bones placed through the attachment bone: fire bones
        // missing from the host may be its (the Gargantuan's and the Underworld station's turrets).
        const auto attached = attachment ? std::ranges::find(attachments, attachment->index, &ModelAttachment::bone) : attachments.end();
        std::vector<Matrix4> attached_pose;
        if (attached != attachments.end() && attached->model)
        {
            attached_pose = skeleton_bind_pose(attached->model->skeleton);
            for (auto& bone : attached_pose)
            {
                bone = bone * pose[attachment->index];
            }
        }
        const auto hardpoint_bone = [&](const std::string_view field)
        {
            auto found = find_bone(skeleton, pose, bone_name(field), model_scale);
            if (!found && !attached_pose.empty())
            {
                found = find_bone(&attached->model->skeleton, attached_pose, bone_name(field), model_scale);
            }
            return found;
        };
        const auto host_fire_bone = find_bone(skeleton, pose, bone_name("Fire_Bone_A"), model_scale);
        const auto fire_bone = hardpoint_bone("Fire_Bone_A");
        const auto second_bone = fire_bone ? hardpoint_bone("Fire_Bone_B") : std::nullopt;
        HardpointProfile part;
        part.kind = kind;
        part.type_key = kind_node != nullptr ? game_object_name_crc(ascii_trim(kind_node->text)) : 0U;
        part.health = point.flag("Is_Destroyable", true) ? point.amount("Health") * health_multiplier : 0.0F;
        part.targetable = point.flag("Is_Targetable", true);
        part.offset = attachment ? attachment->point : fire_bone ? fire_bone->point : LocalPoint{};
        if (point.flag("Is_Turret", false) && attachment)
        {
            TurretProfile turret;
            turret.pivot = attachment->point;
            read_turret_motion(point, turret);
            turret.turns_body = false;
            part.turret = turret;
        }
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
        if (attachment && !bone_name("Model_To_Attach").empty())
        {
            part.attachment_bone = static_cast<std::int32_t>(attachment->index);
        }
        if (const auto prop = bone_name("Death_Breakoff_Prop"); !prop.empty())
        {
            part.breakoff_prop = game_object_name_crc(prop);
        }
        part.death_sound = sound_of(point, "Death_Explosion_SFXEvent", 0U);
        if (const auto* explosions = point.node("Death_Explosion_Particles"))
        {
            if (const auto names = split_list(explosions->text); !names.empty())
            {
                part.death_effect = game_object_name_crc(names.front());
            }
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
        read_projectile(TypeFields(catalog, *projectile_type, &warnings), projectile_type->name, content, weapon);
        if (point.node("Damage_Type") != nullptr)
        {
            weapon.damage_type = type_id(point, "Damage_Type", content);
        }
        weapon.hardpoint = part_index;
        weapon.appearance_delay_seconds = appearance_delay(point);
        weapon.excluded_categories = restricted(point, hardpoint->name, "Fire_Category_Restrictions") | unit_excluded;
        weapon.inaccuracy = inaccuracy(point, "Fire_Inaccuracy_Distance", content);
        if (weapon.inaccuracy.empty())
        {
            weapon.inaccuracy = unit_inaccuracy;
        }
        weapon.fire_sound = sound_of(point, "Fire_SFXEvent", type_fire_sound);
        weapon.mount = fire_bone ? fire_bone->point : part.offset;
        weapon.on_turret = profile.turret && host_fire_bone && host_fire_bone->index < on_turret.size() && on_turret[host_fire_bone->index];
        if (second_bone)
        {
            weapon.extra_mounts.push_back(second_bone->point);
        }
        const auto cone = point.amount("Fire_Cone_Width");
        if (fire_bone && cone > 0.0F && cone < 360.0F)
        {
            weapon.cone_direction = fire_bone->direction;
            weapon.cone_half_angle = cone * 0.5F * pi / 180.0F;
        }
        if (weapon.range > 0.0F)
        {
            profile.weapons.push_back(weapon);
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
