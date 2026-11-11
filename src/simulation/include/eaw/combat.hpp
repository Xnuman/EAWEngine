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
};

// One projectile weapon: fires bursts of `pulse_count` shots `pulse_delay_seconds` apart, one
// burst every `recharge_seconds` (measured from the start of the previous burst), at targets
// within `range`.
struct WeaponProfile
{
    float range{};
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
    // Where shots leave the entity, and the horizontal arc the weapon covers: centered
    // `cone_direction` radians from the entity's heading (counter-clockwise), `cone_half_angle` to
    // either side (pi or more covers all around).
    LocalPoint mount;
    float cone_direction{};
    float cone_half_angle{3.14159265F};
    // The hardpoint the weapon is mounted on (its index in CombatProfile::hardpoints), or -1. The
    // weapon is silent once that hardpoint is destroyed.
    std::int32_t hardpoint{-1};
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
