#pragma once

// Presentation of shots in flight. Projectile types with Projectile_Custom_Render 1 or 2 draw as
// glowing bolts. Render 1: a quad Projectile_Width wide and Projectile_Length long along the flight
// direction, textured with a cell (Projectile_Texture_Slot "column,row") of the 4x4 pill atlas
// Data/Art/Textures/W_LASER_PILL.dds. Render 2: a teardrop in Projectile_Laser_Color,
// Projectile_Width wide and Projectile_Length widths long. The atlas name, the teardrop, and its
// length rule are inferences from the stock data, a modder's description, and screenshots of the
// original (docs/compatibility-research.md, "Projectile visuals"). Other projectiles draw their
// model (Space_Model_Name, else Land_Model_Name, and the reverse on land maps) at the projectile's
// Scale_Factor, nose along the flight: a mesh, whose proxies (missile trails) play as attached
// effects, or a particle system that moves with the shot.

#include "render/visual_frame.hpp"
#include "assets/game_object_catalog.hpp"
#include "assets/particle_rules.hpp"
#include "assets/surface_effects.hpp"
#include "simulation/simulation.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
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
    // (Projectile_Object_Armor_Reduced_Detonation_Particle), at the end of its flight
    // (Projectile_Lifetime_Detonation_Particle), on the ground (Projectile_Ground_Detonation_Particle),
    // and detonated where it was set off (a death projectile: its lifetime detonation, else its object
    // one, else its ground one, as death explosions naming a projectile; an inference from the data).
    std::array<std::uint32_t, 6> impact_effects{};
    // On the ground, the particle of its Projectile_Ground_Detonation_SurfaceFX for each surface kind
    // the SurfaceFX names (lower case: default, water, lava, ice); see ground_impact_effects.
    struct SurfaceParticle
    {
        std::string surface;
        std::uint32_t particle{};
    };
    std::vector<SurfaceParticle> ground_surfaces;
    float width{1.0F};
    float length{1.0F};
    std::uint32_t column{};
    std::uint32_t row{};
    // Render 2: a teardrop in `color` (`length` already in world units); render 1: the atlas cell.
    bool tinted{};
    Color color{white};
    // Model projectiles (no bolt): the scene mesh drawn along the flight, or the particle system
    // that follows the shot (some projectiles name a particle file as their model), and the XML
    // Scale_Factor both are drawn at.
    std::optional<std::uint32_t> mesh;
    std::shared_ptr<const ParticleEffect> effect;
    float scale{1.0F};
    // Drawn flight shape over the simulation's straight path (presentation only): a lob's gravity in
    // world units per s^2 (XML Projectile_Grenade_Gravity x _Lob_Mod, per frame^2 scaled like
    // accelerations: a guess; 0 flies straight), and a rocket's sway: straight for
    // `sway_after` (Projectile_Rocket_Straight_Distance), then up to `sway` sideways
    // (Projectile_Rocket_Curve_Offset) in a half sine, the side alternating by shot.
    float lob_gravity{};
    float sway_after{};
    float sway{};

    // Whether the shot has a look of its own (otherwise only its impacts show).
    [[nodiscard]] bool drawn() const noexcept
    {
        return bolt || mesh || effect;
    }
};

// Projectile visuals by WeaponProfile::projectile_visual (the CRC-32 of the type name).
using ProjectileVisualTable = std::unordered_map<std::uint32_t, ProjectileVisual>;

// Reads the visual of each projectile type in `keys` (its bolt when it has a custom render, and its
// impact effects); problems become warnings naming the type and field.
// `surfaces` resolves the ground SurfaceFX (null: none, as in space).
void read_projectile_visuals(const GameObjectCatalog& catalog, std::span<const std::uint32_t> keys, ProjectileVisualTable& table, std::vector<std::string>& warnings,
                             const SurfaceEffectCatalog* surfaces = nullptr);

// The projectile visual keys of every weapon in `profile`.
void collect_projectile_keys(const CombatProfile& profile, std::vector<std::uint32_t>& keys);

// The Particle objects the table's impacts name.
void collect_impact_effect_keys(const ProjectileVisualTable& table, std::vector<std::uint32_t>& keys);

// The Particle object played for an impact of `kind` (0 none).
[[nodiscard]] std::uint32_t impact_effect(const ProjectileVisual& visual, ImpactKind kind) noexcept;

// The Particle objects a ground hit plays on `surface` (default, or the liquid's kind): over a
// liquid its SurfaceFX particle alone (a splash, not an explosion on the water), else the ground
// detonation and the SurfaceFX's default particle when it is another one. An engine choice: how the
// two tags combine is not known; every stock user names both. 0 for none.
[[nodiscard]] std::array<std::uint32_t, 2> ground_impact_effects(const ProjectileVisual& visual, std::string_view surface) noexcept;

// Where a shot is drawn between the previous tick and this one (`interpolation` 0..1), with its
// flight shape, and the direction it faces there.
struct ProjectilePose
{
    Vector3 position;
    Vector3 direction;
};
[[nodiscard]] ProjectilePose projectile_pose(const ProjectileSnapshot& projectile, const ProjectileVisual& visual, float interpolation, float tick_seconds) noexcept;

// The world transform of a model projectile `visual` for `projectile`, placed between ticks like a
// bolt (`interpolation` of a tick after the previous one), its model's -Y axis along the flight.
[[nodiscard]] Matrix4 projectile_model_transform(const ProjectileSnapshot& projectile, const ProjectileVisual& visual, float interpolation, float tick_seconds);

// Appends an instance (in its mesh's rest pose, bone offsets set by the caller's animation) for every
// projectile in `current` whose visual is a mesh, and its serial to `serials`.
void append_projectile_instances(std::span<const ProjectileSnapshot> current, const ProjectileVisualTable& table, float interpolation, float tick_seconds, std::vector<VisualInstance>& instances,
                                 std::vector<std::uint32_t>& serials);

// Appends a bolt sprite for every projectile in `current` that has a visual, placed `interpolation`
// (0..1) of a tick after the previous tick (shots are drawn where they were between ticks, like
// entities). `atlas` is the pill atlas's renderer texture.
void append_projectile_sprites(std::span<const ProjectileSnapshot> current, const ProjectileVisualTable& table, std::uint32_t atlas, float interpolation, float tick_seconds,
                               std::vector<VisualParticle>& sprites);

} // namespace eaw
