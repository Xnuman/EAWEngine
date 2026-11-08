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
