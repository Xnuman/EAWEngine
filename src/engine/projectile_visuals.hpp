#pragma once

// Presentation of shots in flight. Projectile types with Projectile_Custom_Render 1 or 2 draw as
// glowing bolts. Render 1: a quad Projectile_Width wide and Projectile_Length long along the flight
// direction, textured with a cell (Projectile_Texture_Slot "column,row") of the 4x4 pill atlas
// Data/Art/Textures/W_LASER_PILL.dds. Render 2: a teardrop in Projectile_Laser_Color,
// Projectile_Width wide and Projectile_Length widths long. The atlas name, the teardrop, and its
// length rule are inferences from the stock data, a modder's description, and screenshots of the
// original (docs/compatibility-research.md, "Projectile visuals"). Other projectiles are not drawn
// yet.

#include "eaw/game_object_catalog.hpp"
#include "eaw/simulation.hpp"
#include "eaw/visual_scene.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace eaw {

inline constexpr std::string_view projectile_atlas_texture = "W_LASER_PILL.tga";
inline constexpr std::uint32_t projectile_atlas_cells = 4;

struct ProjectileVisual
{
    // Drawn as a bolt (Projectile_Custom_Render 1 or 2); otherwise only its impacts show.
    bool bolt{};
    // XML Particle objects (CRC-32 of the name, 0 none) played where a shot ends, by ImpactKind:
    // absorbed by shields (Projectile_Absorbed_By_Shields_Particle), on the hull
    // (Projectile_Object_Detonation_Particle), on the hull with armor reducing it
    // (Projectile_Object_Armor_Reduced_Detonation_Particle), and at the end of its flight
    // (Projectile_Lifetime_Detonation_Particle).
    std::array<std::uint32_t, 4> impact_effects{};
    float width{1.0F};
    float length{1.0F};
    std::uint32_t column{};
    std::uint32_t row{};
    // Render 2: a teardrop in `color` (`length` already in world units); render 1: the atlas cell.
    bool tinted{};
    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
};

// Projectile visuals by WeaponProfile::projectile_visual (the CRC-32 of the type name).
using ProjectileVisualTable = std::unordered_map<std::uint32_t, ProjectileVisual>;

// Reads the visual of each projectile type in `keys` (its bolt when it has a custom render, and its
// impact effects); problems become warnings naming the type and field.
void read_projectile_visuals(const GameObjectCatalog& catalog, std::span<const std::uint32_t> keys, ProjectileVisualTable& table, std::vector<std::string>& warnings);

// The projectile visual keys of every weapon in `profile`.
void collect_projectile_keys(const CombatProfile& profile, std::vector<std::uint32_t>& keys);

// The Particle objects the table's impacts name.
void collect_impact_effect_keys(const ProjectileVisualTable& table, std::vector<std::uint32_t>& keys);

// The Particle object played for an impact of `kind` (0 none).
[[nodiscard]] std::uint32_t impact_effect(const ProjectileVisual& visual, ImpactKind kind) noexcept;

// Appends a bolt sprite for every projectile in `current` that has a visual, placed `interpolation`
// (0..1) of a tick after the previous tick (shots are drawn where they were between ticks, like
// entities). `atlas` is the pill atlas's renderer texture.
void append_projectile_sprites(std::span<const ProjectileSnapshot> current, const ProjectileVisualTable& table, std::uint32_t atlas, float interpolation, float tick_seconds,
                               std::vector<VisualParticle>& sprites);

} // namespace eaw
