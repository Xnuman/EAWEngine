#pragma once

// Translates XML combat data into the simulation's engine-native combat profiles. Field meanings
// follow the stock XML (docs/compatibility-research.md); where the original's semantics are not
// established (shield refresh units, how shields and armor combine), the translation is marked as
// provisional.

#include "assets/skeleton.hpp"
#include "simulation/combat.hpp"
#include "assets/game_object_catalog.hpp"
#include "vfs/virtual_file_system.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eaw {

struct ModeMultipliers
{
    float land{1.0F};
    float space{1.0F};

    [[nodiscard]] float of(const bool land_mode) const noexcept
    {
        return land_mode ? land : space;
    }
};

struct CombatContent
{
    // Damage and armor type names (ASCII-folded) to simulation ids, from 1; 0 is "none".
    std::unordered_map<std::string, std::uint16_t> type_ids;
    DamageTable damage_table;
    // GameConstants' per-mode multipliers of every object type's maximum speed and of object and
    // hardpoint maximum health (developer comments in the source XML state both; stock: speed 1.56
    // on land and 1.2 in space, health 1.0 on land and 1.5 in space). 1 when absent.
    ModeMultipliers max_speed_multiplier;
    ModeMultipliers max_health_multiplier;
    // GameObjectCategoryType names (ASCII-folded) to their bits, from Data/XML/Enum (the names
    // CategoryMask and the inaccuracy lists use).
    std::unordered_map<std::string, std::uint32_t> category_bits;
    // GameObjectPropertiesType names (ASCII-folded) to their bits (Property_Flags, priority sets).
    std::unordered_map<std::string, std::uint32_t> property_bits;
    std::vector<std::string> warnings;

    // The bits of a category list ("Infantry | AntiVehicle"; `|`, commas, or spaces); unknown
    // names are skipped and named in `unknown`.
    [[nodiscard]] std::uint32_t categories(std::string_view list, std::vector<std::string>* unknown = nullptr) const;
    [[nodiscard]] std::uint32_t properties(std::string_view list, std::vector<std::string>* unknown = nullptr) const;
};

// Reads the Damage_To_Armor_Mod entries ("Damage_Blaster_Hand, Armor_Infantry, 1.3") and the
// Object_Max_Speed_Multiplier_* and Object_Max_Health_Multiplier_* values of
// Data/XML/GameConstants.xml, the winning layer, and the category bits of
// Data/XML/Enum/GameObjectCategoryType.xml and GameObjectPropertiesType.xml. Problems are warnings; a missing file leaves every
// modifier at 1 and no categories.
[[nodiscard]] CombatContent load_combat_content(const AssetReader& reader);

// Health, shields, armor, and the first projectile weapon of an object type:
//   Tactical_Health, Shield_Points, Shield_Refresh_Rate (provisionally points per second),
//   Armor_Type, Shield_Armor_Type, Ranged_Target_Z_Adjust (aim height);
//   Projectile_Types (first entry), Targeting_Max_Attack_Distance, Projectile_Fire_Recharge_Seconds,
//   Projectile_Fire_Pulse_Count, Projectile_Fire_Pulse_Delay_Seconds;
//   CategoryMask and Property_Flags (the entity's categories and properties), its type key, and
//   Targeting_Priority_Set (the named Priority_Set's Attack_Priorities, Category_, Property_, and
//   Unit_Exclusions; its Hard_Point_Priorities are not read); a weapon's inaccuracy from the shooter's
//   Targeting_Fire_Inaccuracy, else Fire_Inaccuracy_Distance ("category, distance" per line), then
//   Targeting_Fire_Inaccuracy_Fixed_Radius for every other target; a weapon hardpoint's from its own
//   Fire_Inaccuracy_Distance, else its host's;
//   and from the projectile type: Projectile_Damage, Damage_Type, Max_Speed (same provisional scale
//   as unit speeds), Projectile_Max_Flight_Distance, Projectile_Does_Shield_Damage,
//   Projectile_Does_Hitpoint_Damage.
// Unparsable or negative values become 0 with a warning naming the type and field.
//
// Hardpoints (HardPoints entries) become targetable parts: Type gives the kind (weapon, shield
// generator, engine, other), Health their hit points (none or Is_Destroyable No: indestructible),
// Is_Targetable whether shots aim at them, and the model's Attachment_Bone their position. A weapon
// hardpoint's weapon fires from Fire_Bone_A within Fire_Cone_Width, centered on that bone's X axis
// (a fire bone's X axis is its firing direction in the stock models). Bone positions come from
// `model`'s bind pose (models face -Y; see model_forward_heading); without a model or bone they
// are at the origin and the weapon covers all around.
// `model_scale` is the type's Scale_Factor: hardpoint and fire-bone positions come from the model's
// bones and scale with it. `health_multiplier` scales the object's and its hardpoints' health
// (CombatContent::max_health_multiplier for the battle's mode).
[[nodiscard]] CombatProfile combat_profile(const GameObjectCatalog& catalog, const XmlDefinition& definition, const CombatContent& content, std::vector<std::string>& warnings,
                                           const Skeleton* skeleton = nullptr, float model_scale = 1.0F, float health_multiplier = 1.0F);

// TED owner slot that holds the world's props and markers (179k of the stock placements, nearly
// all of them props, markers, and decorations; see docs/compatibility-research.md).
inline constexpr std::uint32_t neutral_map_owner = 3;

// Provisional relation between TED owner slots until player and faction assignment exists: the
// neutral slot fights nobody, every other pair of distinct slots is hostile.
[[nodiscard]] Hostility provisional_map_hostility() noexcept;

} // namespace eaw
