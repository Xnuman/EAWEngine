#pragma once

// Translates XML combat data into the simulation's engine-native combat profiles. Field meanings
// follow the stock XML (docs/compatibility-research.md); where the original's semantics are not
// established (shield refresh units, how shields and armor combine), the translation is marked as
// provisional.

#include "eaw/combat.hpp"
#include "eaw/game_object_catalog.hpp"
#include "eaw/virtual_file_system.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eaw {

struct CombatContent
{
    // Damage and armor type names (ASCII-folded) to simulation ids, from 1; 0 is "none".
    std::unordered_map<std::string, std::uint16_t> type_ids;
    DamageTable damage_table;
    std::vector<std::string> warnings;
};

// Reads the Damage_To_Armor_Mod entries ("Damage_Blaster_Hand, Armor_Infantry, 1.3") of
// Data/XML/GameConstants.xml, the winning layer. Problems are warnings; a missing file leaves
// every modifier at 1.
[[nodiscard]] CombatContent load_combat_content(const AssetReader& reader);

// Health, shields, armor, and the first projectile weapon of an object type:
//   Tactical_Health, Shield_Points, Shield_Refresh_Rate (provisionally points per second),
//   Armor_Type, Shield_Armor_Type, Ranged_Target_Z_Adjust (aim height);
//   Projectile_Types (first entry), Targeting_Max_Attack_Distance, Projectile_Fire_Recharge_Seconds,
//   Projectile_Fire_Pulse_Count, Projectile_Fire_Pulse_Delay_Seconds;
//   and from the projectile type: Projectile_Damage, Damage_Type, Max_Speed (same provisional scale
//   as unit speeds), Projectile_Max_Flight_Distance, Projectile_Does_Shield_Damage,
//   Projectile_Does_Hitpoint_Damage.
// Unparsable or negative values become 0 with a warning naming the type and field. Weapons on
// hardpoints (space ships) are not read yet.
[[nodiscard]] CombatProfile combat_profile(const GameObjectCatalog& catalog, const XmlDefinition& definition, const CombatContent& content, std::vector<std::string>& warnings);

// TED owner slot that holds the world's props and markers (179k of the stock placements, nearly
// all of them props, markers, and decorations; see docs/compatibility-research.md).
inline constexpr std::uint32_t neutral_map_owner = 3;

// Provisional relation between TED owner slots until player and faction assignment exists: the
// neutral slot fights nobody, every other pair of distinct slots is hostile.
[[nodiscard]] Hostility provisional_map_hostility() noexcept;

} // namespace eaw
