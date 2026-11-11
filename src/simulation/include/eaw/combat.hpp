#pragma once

// Engine-native combat data for the tactical simulation. Values come from content (the engine
// translates XML object, projectile, and GameConstants definitions into these), but the
// simulation never reads files: armor and damage types are small integer ids interned by the
// caller, with 0 meaning "none".

#include <cstdint>
#include <optional>
#include <vector>
#include <unordered_map>

namespace eaw {

// Positions on an entity are given in its own frame: x forward (along its heading), y to its
// left, z up, in world units.
struct LocalPoint
{
    float forward{};
    float left{};
    float up{};

    friend bool operator==(const LocalPoint&, const LocalPoint&) = default;
};

// A part of an entity that can be targeted and destroyed on its own (ship hardpoints).
struct HardpointProfile
{
    enum class Kind : std::uint8_t
    {
        weapon,
        shield_generator,
        engine,
        other,
    };
    Kind kind{Kind::other};
    // Hit points; zero means it cannot be destroyed.
    float health{};
    // Shots may aim at it.
    bool targetable{true};
    LocalPoint offset;
    // Presentation only (not simulation state): the model bone its XML Damage_Particles names, under
    // which the model's damage-effect proxies hang, or -1.
    std::int32_t damage_bone{-1};
    // Presentation only: the bone its XML Damage_Decal names (scorch meshes shown once it is
    // destroyed), or -1.
    std::int32_t decal_bone{-1};
    // Presentation only: the bone its XML Engine_Particles names (engine glows), or -1.
    std::int32_t engine_bone{-1};
    // Presentation only: the sound of its destruction (CRC-32 of its XML Death_Explosion_SFXEvent,
    // as game_object_name_crc), or 0.
    std::uint32_t death_sound{};
    // Presentation only: the Particle object its destruction plays (the first entry of its XML
    // Death_Explosion_Particles, as game_object_name_crc), or 0.
    std::uint32_t death_effect{};
};

// How far a weapon's shots stray from targets of some categories: the first entry whose
// categories the target has applies (bits of the game's GameObjectCategoryType; all bits for a
// fixed radius: all_categories).
// CategoryInaccuracy::categories that applies to every target, even one without categories.
inline constexpr std::uint32_t all_categories = 0xffffffffU;

struct CategoryInaccuracy
{
    std::uint32_t categories{};
    float distance{};
};

// Whom an entity prefers to shoot (its XML Targeting_Priority_Set). A target matching an entry
// (by category or property bits) takes that entry's rank, lower first; otherwise excluded
// categories, properties, and types are never chosen, and the rest rank after every entry. That a
// match outranks an exclusion is read from the data: AntiInfantry ranks TacticalStructure while
// excluding Structure. No ranks and no exclusions: every hostile ranks alike (the nearest wins).
struct TargetPriority
{
    std::uint32_t categories{};
    std::uint32_t properties{};
    float rank{};
};

struct TargetingPriorities
{
    std::vector<TargetPriority> ranks;
    std::uint32_t excluded_categories{};
    std::uint32_t excluded_properties{};
    // Type keys (CombatProfile::type_key) never chosen.
    std::vector<std::uint32_t> excluded_types;

    // The rank of a target, or a negative value when it is excluded.
    [[nodiscard]] float rank(const std::uint32_t categories, const std::uint32_t properties, const std::uint32_t type) const noexcept
    {
        for (const auto& entry : ranks)
        {
            if ((entry.categories & categories) != 0 || (entry.properties & properties) != 0)
            {
                return entry.rank;
            }
        }
        if ((excluded_categories & categories) != 0 || (excluded_properties & properties) != 0)
        {
            return -1.0F;
        }
        for (const auto excluded : excluded_types)
        {
            if (excluded == type)
            {
                return -1.0F;
            }
        }
        return unlisted_rank;
    }

    // The rank of targets no entry names: after every stock rank (the highest is 9).
    static constexpr float unlisted_rank = 1.0e6F;
};

// Damage a shot deals around the point where it ends, hit or not (XML Projectile_Blast_Area_Damage
// and _Range): every hostile damageable entity whose footprint comes within `range` takes `damage`,
// or with `tiers` (Projectile_Blast_Area_Dropoff with _Dropoff_Tiers) the damage of its ring: of
// `tiers` equal rings, the n-th from the center takes (tiers - n) / tiers of it. The direct victim
// takes the blast besides the shot (many blast projectiles deal no shot damage). Hostile only, and
// the reading of the tiers, are engine choices.
struct AreaBlast
{
    float damage{};
    float range{};
    std::uint32_t tiers{};
};

// One projectile weapon: fires bursts of `pulse_count` shots `pulse_delay_seconds` apart, one
// burst every `recharge_seconds` (measured from the start of the previous burst), at targets
// within `range`.
struct WeaponProfile
{
    float range{};
    // Targets closer than this are not fired at (XML Targeting_Min_Attack_Distance: artillery).
    float min_range{};
    // Category bits it never fires at (XML Fire_Category_Restrictions and Attack_Category_Restrictions:
    // torpedoes at fighters, anti-air turrets at infantry).
    std::uint32_t excluded_categories{};
    float recharge_seconds{};
    std::uint32_t pulse_count{1};
    float pulse_delay_seconds{};
    // Per shot.
    float damage{};
    std::uint16_t damage_type{};
    // World units per second; zero hits at once.
    float projectile_speed{};
    // A shot that has flown this far without hitting is lost; zero means unlimited.
    float max_flight_distance{};
    bool damages_shield{true};
    bool damages_health{true};
    AreaBlast blast;
    // Where shots leave the entity, and the horizontal arc the weapon covers: centered
    // `cone_direction` radians from the entity's heading (counter-clockwise), `cone_half_angle` to
    // either side (pi or more covers all around).
    LocalPoint mount;
    // A second muzzle its shots alternate with (a hardpoint's XML Fire_Bone_B), first shot from
    // `mount`.
    std::optional<LocalPoint> alternate_mount;
    float cone_direction{};
    float cone_half_angle{3.14159265F};
    // The hardpoint the weapon is mounted on (its index in CombatProfile::hardpoints), or -1. The
    // weapon is silent once that hardpoint is destroyed.
    std::int32_t hardpoint{-1};
    // Each shot lands a random distance up to this from its aim point, in the horizontal plane, and
    // misses when that is outside the target's footprint radius; empty: every shot hits.
    std::vector<CategoryInaccuracy> inaccuracy;
    // Presentation only (not simulation state): which projectile type its shots look like (the
    // CRC-32 of the type name, as game_object_name_crc), or 0.
    std::uint32_t projectile_visual{};
    // Presentation only: the sound event a shot plays (the CRC-32 of its name, as
    // game_object_name_crc), or 0.
    std::uint32_t fire_sound{};
};

struct CombatProfile
{
    // Hit points; zero means the entity cannot be damaged or targeted.
    float health{};
    float shield{};
    float shield_regen_per_second{};
    std::uint16_t armor_type{};
    std::uint16_t shield_armor_type{};
    // Height above the entity's position that shots aim at.
    float aim_height{};
    // What the entity is, for weapons' inaccuracy (bits of GameObjectCategoryType, from its XML
    // CategoryMask).
    std::uint32_t categories{};
    // Stray shots meant for others hit it where they land on it (XML Collidable_By_Projectile_Living).
    bool collidable{true};
    // Never chosen as a target (XML Is_Valid_Target No: ambient creatures, markers), and never
    // damaged (XML Immune_To_Damage: build pads, markers).
    bool targetable{true};
    bool immune{};
    // Standing orders (world units; 0: the engine's defaults, see simulation.hpp): how far an
    // attack-mover reacts to hostiles (XML Attack_Move_Response_Range) and how far a guard chases
    // (XML Guard_Chase_Range).
    float attack_move_response_range{};
    float guard_chase_range{};
    // After this long on a target it chose itself, the entity looks again and switches when its
    // targeting priorities prefer another (XML Targeting_Stickiness_Time_Threshold, 5 s in most
    // stock uses); 0 keeps the target while it stays valid.
    float stickiness_seconds{};
    // The blast its death sets off where it dies (the area blast of its XML Death_Projectiles
    // projectile: stations' detonations), with that projectile's damage type and flags.
    AreaBlast death_blast;
    std::uint16_t death_blast_damage_type{};
    bool death_blast_damages_shield{true};
    bool death_blast_damages_health{true};
    // Spinning away (XML Spin_Away_On_Death, _Chance, _Time; fighters, corvettes): when its health
    // runs out it spins away with this chance instead of dying at once: out of control (no orders,
    // no target, no fire, no further damage) for `spin_seconds`, then it dies and plays
    // `spin_effect` (Spin_Away_On_Death_Explosion, presentation) instead of its death effect.
    float spin_chance{};
    float spin_seconds{};
    std::uint32_t spin_effect{};
    // Property bits (GameObjectPropertiesType, XML Property_Flags) and a key of its type (the CRC-32
    // of the type name), for others' targeting priorities.
    std::uint32_t properties{};
    std::uint32_t type_key{};
    TargetingPriorities targeting;
    // Every weapon (a land unit's projectile weapon, a ship's weapon hardpoints). Each fires at the
    // entity's target when it is within that weapon's range, with its own recharge and bursts.
    std::vector<WeaponProfile> weapons;
    // Targetable parts (ship hardpoints). Shots aim at the nearest live targetable one; losing every
    // shield generator stops shield recharge and losing every engine slows the entity to a quarter.
    std::vector<HardpointProfile> hardpoints;
    // Presentation only (not simulation state): the effect played when the entity dies (the CRC-32 of
    // the XML Particle object its Death_Explosions names), or 0.
    std::uint32_t death_effect{};

    // The longest weapon range (the entity's targeting range); 0 without weapons.
    [[nodiscard]] float range() const noexcept
    {
        float longest = 0.0F;
        for (const auto& weapon : weapons)
        {
            longest = weapon.range > longest ? weapon.range : longest;
        }
        return longest;
    }
};

// Damage multipliers by (damage type, armor type); pairs that were never set multiply by 1.
class DamageTable final
{
public:
    void set(std::uint16_t damage_type, std::uint16_t armor_type, float modifier);
    [[nodiscard]] float modifier(std::uint16_t damage_type, std::uint16_t armor_type) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::unordered_map<std::uint32_t, float> modifiers_;
};

// Which owners fight each other: a symmetric relation over owner slots 0..63. Owners outside
// that range are never hostile. Nothing is hostile until set.
class Hostility final
{
public:
    static constexpr std::uint32_t max_owners = 64;

    void set(std::uint32_t first, std::uint32_t second, bool hostile) noexcept;
    [[nodiscard]] bool hostile(std::uint32_t first, std::uint32_t second) const noexcept;

private:
    std::uint64_t rows_[max_owners]{};
};

} // namespace eaw
