#pragma once

// What gameplay reads of XML object types and their models: the cached XML catalog, movement
// classes, and combat content, the simulation-relevant fields of a type, and the footprints and
// hardpoint models measured from its model. Scene building reads the look fields beside these
// (scene_builder_internal.hpp, type_looks.cpp).

#include "math/matrix.hpp"
#include "math/scalar.hpp"
#include "assets/game_object_catalog.hpp"
#include "assets/model.hpp"
#include "assets/model_geometry.hpp"
#include "assets/resource_loaders.hpp"
#include "assets/resource_manager.hpp"
#include "assets/xml_asset_database.hpp"
#include "simulation/simulation.hpp"
#include "battle/battle_map.hpp"
#include "battle/combat_content.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eaw {

class AssetReader;

// Alamo models face -Y: every stock muzzle and fire bone sits at negative Y and points along -Y
// (T2B, AT-ST, X-wing, Star Destroyer; see docs/compatibility-research.md). An entity's heading is
// the direction of its model's forward axis about +Z, so an unrotated model has this heading.
inline constexpr float identity_model_heading = -half_pi;

// The heading of a row-vector model transform: the direction of its forward (-Y) axis about +Z.
[[nodiscard]] float model_forward_heading(const Matrix4& transform) noexcept;

// A turret's turn rate without Turret_Rotate_Speed (degrees per frame, legacy_units; the stock
// values run 2 to 10; an engine choice).
inline constexpr float default_turret_rotate_speed = 5.0F;

// The XML object catalog borrows its database, so both live behind one stable allocation.
struct CachedCatalog
{
    std::unique_ptr<XmlAssetDatabase> database;
    GameObjectCatalog catalog;
};

// Terrain scale established from the stock corpus: vertices are 20 world units apart (column ->
// +X, row -> +Y) and stored elevation is 20x the world height, which places map objects within a
// few world units of the interpolated ground.
inline constexpr float terrain_sample_spacing = 20.0F;

inline constexpr float terrain_elevation_scale = 20.0F;

inline constexpr std::string_view movement_class_path = "Data/XML/MovementClassTypeDefs.xml";

// Movement classes from Data/XML/MovementClassTypeDefs.xml (the winning layer, so mods apply): class
// name (folded) -> passability types it may enter, plus problems to report as scene warnings.
struct MovementClasses
{
    std::unordered_map<std::string, std::uint32_t> classes;
    std::vector<std::string> warnings;
};

// How the cached content types load: each has one fixed source.
template <> struct ResourceLoader<CachedCatalog>
{
    using Value = CachedCatalog;
    static constexpr std::string_view kind = "xml-catalog";
    [[nodiscard]] static std::string key(std::string_view path);
    [[nodiscard]] static Value load(const AssetReader& reader, std::string_view path);
};

template <> struct ResourceLoader<MovementClasses>
{
    using Value = MovementClasses;
    static constexpr std::string_view kind = "movement-classes";
    [[nodiscard]] static std::string key(std::string_view path);
    [[nodiscard]] static Value load(const AssetReader& reader, std::string_view path);
};

template <> struct ResourceLoader<CombatContent>
{
    using Value = CombatContent;
    static constexpr std::string_view kind = "combat-content";
    [[nodiscard]] static std::string key(std::string_view path);
    [[nodiscard]] static Value load(const AssetReader& reader, std::string_view path);
};

// The XML catalog's source (the whole XML folder) and the combat content's.
inline constexpr std::string_view xml_catalog_path = "Data/XML";
inline constexpr std::string_view combat_content_path = "Data/XML/GameConstants.xml";

// The models a type's hardpoints attach to `host` (XML HardPoints entries with Model_To_Attach and
// an Attachment_Bone the host has), in list order; problems become `warnings`.
std::vector<ModelAttachment> type_hardpoint_models(const ResourceManager& resources, std::vector<std::string>& warnings, const GameObjectCatalog& catalog, const XmlDefinition& type,
                                                   const Model* host);

// Key of a mesh built from `model_path` with `attachments` (types sharing a model but not their
// hardpoints get meshes of their own).
std::string attached_mesh_key(const std::string& model_path, std::span<const ModelAttachment> attachments);

// Separation footprint of a model placed with `transform` (row-vector): a quarter of the summed X
// and Y extents of its drawn bounds (assets/model_geometry), scaled by the transform's horizontal
// scale. An engine choice until collision meshes and UnitCollisionClass are interpreted.
float model_footprint_radius(const ModelBounds& bounds, const Matrix4& transform);

// The ground rectangle of a model placed with `transform`: its drawn bounds' X/Y extent, rotated
// and scaled with the placement, with the given passability type. An engine choice until collision
// meshes are read.
GroundFootprint model_ground_footprint(const ModelBounds& bounds, const Matrix4& transform, std::uint32_t passability_type);

// Whether a type walks on the ground of land maps. XML Space_Layer is "Land" for ground vehicles,
// infantry, and heroes, and "None" for walkers such as Ewoks; air units (snowspeeders, air whales)
// use "Frigate", "Capital", or "Corvette", and birds or space-only types have none. This reading is
// a corpus inference (see docs/compatibility-research.md).
bool type_follows_ground(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// Reads movement_class_path from the winning layer; problems become warnings.
MovementClasses load_movement_classes(const AssetReader& reader);

// Passability types a type may enter: its MovementClass's entry, or Clear | Shield (what most
// ground classes allow) when the type names no known class.
std::uint32_t type_passable_types(const GameObjectCatalog& catalog, const XmlDefinition& definition, const std::unordered_map<std::string, std::uint32_t>& classes);

// A numeric field through the variant chain; 0 when it is missing or not a finite number.
float type_float_field(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::string_view field);

// XML FormationOrder (default 2, the most common) and FormationSpacing (default 0.5, likewise).
std::uint32_t type_formation_order(const GameObjectCatalog& catalog, const XmlDefinition& definition);
float type_formation_spacing(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// XML Is_Supercrusher, Is_Squashable, and Is_Squashable_By_Supercrusher; a type that is not a
// supercrusher crushes when its UnitCollisionClass is Vehicle, Large Vehicle, or Giant Vehicle (an
// engine choice: the data does not say who crushes infantry).
CrushProfile type_crush(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// XML Destruction_Survivors ("type, weight" entries; empty entries and weights of 0 skipped) and
// Minimum_Destruction_Survivor_Count.
Survivors type_survivors(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// A company's TeamLayout: XML Max_Squad_Size and its Create_Team_Type container's
// ContainerArrangement (Rows or Dartboard, the default) and FormationSpacing.
TeamLayout type_team_layout(const GameObjectCatalog& catalog, const XmlDefinition& company);
// XML FormationRaggedness, clamped to 0..10.
float type_formation_raggedness(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// XML FormationGrouping Solo (the other stock value is Standard, the default).
bool type_formation_solo(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// The passability type an immobile placed object stamps on the ground, or nothing when it does not
// block: its XML OverridePassability (walls: RebelWall, EmpireWall), otherwise Obstacle for
// structures (GroundStructure, SpecialStructure, SecondaryStructure) and for types whose
// UnitCollisionClass names an obstacle (High, Medium, or Low Obstacle). Build pads and other
// props do not block. A provisional reading of the stock data.
std::optional<std::uint32_t> type_obstacle_passability(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// The ground rectangle a type's XML gives when placed with `placement`: its Obstacle_Width/_Height
// rectangle (from the Obstacle_X/_Y_Offset corner), else its Custom_Hard_X/_YExtent half extents
// (around the _Offset center), read from its Obstacle_Proxy_Type when it names one; nothing when
// it sets neither (the mesh bounds stand in).
std::optional<GroundFootprint> type_ground_footprint(const GameObjectCatalog& catalog, const XmlDefinition& definition, const Matrix4& placement, std::uint32_t passability_type);

// The radius ships keep clear of an immobile space object: its XML Space_Obstacle_Radius when its
// behaviors include SPACE_OBSTACLE (docks, asteroids, wrecks, junk); 0 for asteroid fields,
// nebulae, and ion storms, which ships enter. World units, unscaled (an inference, as for hard
// extents); Space_Obstacle_Offset (0 0 0 in nearly all stock uses) is not read.
float type_space_obstacle_radius(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// XML Custom_Soft_Footprint_Radius (infantry 3.5), else Custom_Footprint_Radius (small space craft:
// 7 or 15; taken as their separation radius, an engine choice), or 0 when both are missing.
float type_soft_footprint_radius(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// A type's XML Scale_Factor (inherited through Variant_Of_Existing_Type), the size its model is drawn
// at; 1 when absent, and 1 with a warning when not a positive number.
float type_scale_factor(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::vector<std::string>& warnings);
// `transform` (row-vector) with its model axes scaled by `scale`, so the model is drawn `scale` times
// its file size about its own origin.
Matrix4 scaled_model_transform(Matrix4 transform, float scale) noexcept;

// XML Max_Rate_Of_Turn (degrees per frame: infantry 16, 480 degrees per second; tanks 12-20;
// capital ships 0.4-1.5) in radians per second (legacy_units); 0 turns instantly.
// Times `mode_multiplier`: in space the game scales turn rates by Object_Max_Speed_Multiplier_Space
// as it does speeds (recorded, see type_movement).
float type_turn_rate(const GameObjectCatalog& catalog, const XmlDefinition& definition, float mode_multiplier = 1.0F);

// How a type gets up to speed and turns: XML OverrideAcceleration and OverrideDeceleration (or
// Override_Acceleration and Override_Deceleration, the team containers' spelling)
// (speed changes per frame, legacy_units: the Calamari cruiser's .02 reaches full speed in 2.5
// seconds, the T2B's .2 in 0.4; absent changes speed at once) and Min_Speed_Fraction_For_Turn. On
// land it turns at Stopped_Rate_Of_Turn (or Max_Rate_Of_Turn) below that fraction and stops to turn
// when its way is more than Close_Enough_Angle_For_Move_Start (degrees; the AT-AT 30) or else
// ground_turn_in_place_angle off; in space it cannot turn below that
// fraction (default_space_turn_speed_fraction without one). The default profile for flyers.
// Acceleration, deceleration, and turn rates are times `mode_multiplier`: in space the game scales them
// by Object_Max_Speed_Multiplier_Space like Max_Speed (recorded in the FoC debug build: a corvette
// reaches 3.72 = 3.1 x 1.2 in 62 frames, 0.05 x 1.2 per frame, and turns 1.506 = 1.25 x 1.2 degrees
// per frame; EaW-Revised's S-10, S-12, S-26; docs/compatibility-research.md). Callers pass the
// space multiplier on space maps; on land it is not observed, so they pass 1.
MovementProfile type_movement(const GameObjectCatalog& catalog, const XmlDefinition& definition, bool land, bool flyer, float mode_multiplier = 1.0F);
// Engine choices, not the original's: how far off its way a ground unit stops to turn (the
// owner's observation that land units do not turn while moving off; GameConstants'
// CloseEnoughAngleForMoveStart says 180, which would never stop them, so the two disagree), and the
// turn speed fraction of space ships that do not name one (capital ships).
inline constexpr float ground_turn_in_place_angle = 0.785398163F;
inline constexpr float default_space_turn_speed_fraction = 0.25F;

// The movement of the team `company` forms (its XML Create_Team_Type container): the container's
// Max_Speed (scaled like type_max_speed, times `speed_multiplier`), Max_Rate_Of_Turn, and
// acceleration where it sets them; empty without a team.
TeamMovement type_team_movement(const GameObjectCatalog& catalog, const XmlDefinition& company, float speed_multiplier);

// Minimum flight speed of fighters and bombers (SpaceBehavior names FIGHTER_LOCOMOTOR): XML
// Min_Speed on the Max_Speed scale, or half of `max_speed` without one; 0 for every other type.
float type_min_flight_speed(const GameObjectCatalog& catalog, const XmlDefinition& definition, float max_speed);

// Whether a type is a death clone, never spawned on its own: another type names it in a
// Death_Clone entry, or it carries Specific_Death_Anim_Index or Death_Persistence_Duration.
bool type_is_death_clone(const GameObjectCatalog& catalog, const XmlDefinition& definition, const std::unordered_set<std::string>& clone_names);
// Folded names of every type some Death_Clone entry names.
std::unordered_set<std::string> death_clone_names(const XmlAssetDatabase& database, const GameObjectCatalog& catalog);

// Whether a type aims with its body: none of its behavior lists (LandBehavior, Behavior) names
// TURRET. Stock infantry list WALK_LOCOMOTOR or LAND_TEAM_INFANTRY_LOCOMOTOR, IDLE, TARGETING, and
// WEAPON without TURRET; vehicles (T2-B, AT-ST) add TURRET. Only meaningful for ground units.
bool type_aims_with_body(const GameObjectCatalog& catalog, const XmlDefinition& definition);
// What scripts read of a type (XML Lua_Script and CategoryMask, through its variant chain).
ScriptTypeInfo type_script_info(const GameObjectCatalog& catalog, const XmlDefinition& definition);

// XML Max_Speed (per frame, legacy_units: infantry 0.8, AT-AT 0.5, TIE fighter 4.5) in world units
// per second;
// unparsable values are 0 with a warning.
float type_max_speed(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::vector<std::string>& warnings, float mode_multiplier = 1.0F);

// The clone type a Death_Clone entry ("Damage_Normal, AT_ST_Walker_Death_Clone_00") names, and its
// damage type, both trimmed.
std::pair<std::string_view, std::string_view> death_clone_entry(std::string_view text);

} // namespace eaw
