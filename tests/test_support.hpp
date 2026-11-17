#pragma once

// Shared helpers for the EAWEngine test modules: requirement checks, binary writers, and synthetic
// format fixtures. Each module file registers its tests with a ModuleRegistrar at its end.

#include "core/checksum.hpp"
#include "core/fatal_error.hpp"
#include "core/file_io.hpp"
#include "core/log.hpp"
#include "core/status.hpp"
#include "core/string_utils.hpp"
#include "vfs/asset_environment.hpp"
#include "vfs/asset_path.hpp"
#include "vfs/game_installation.hpp"
#include "vfs/meg_archive.hpp"
#include "vfs/virtual_file_system.hpp"
#include "formats/ala_animation.hpp"
#include "formats/alo_model.hpp"
#include "formats/alo_particle_system.hpp"
#include "formats/dat_string_table.hpp"
#include "formats/dds_texture.hpp"
#include "formats/lua_bytecode.hpp"
#include "formats/mtd_atlas.hpp"
#include "formats/ted_map.hpp"
#include "formats/xml_document.hpp"
#include "render/gpu_resource_lifetime.hpp"
#include "assets/alamo_loaders.hpp"
#include "assets/asset_cache.hpp"
#include "assets/game_object_catalog.hpp"
#include "assets/legacy_material.hpp"
#include "assets/map_object_layout.hpp"
#include "assets/resource_loaders.hpp"
#include "assets/resource_manager.hpp"
#include "assets/xml_asset_database.hpp"
#include "assets/xml_schema.hpp"
#include "localization/localization_scan.hpp"
#include "localization/localization_workspace.hpp"
#include "simulation/simulation.hpp"
#include "battle/battle_map.hpp"
#include "battle/combat_content.hpp"
#include "presentation/debug_draw.hpp"
#include "presentation/scene_presentation.hpp"
#include "scene/visual_scene_builder.hpp"
#include "control/camera_controller.hpp"
#include "control/camera_math.hpp"
#include "control/rts_control.hpp"
#include "control/terrain_height.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <ranges>
#include <set>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eaw_test {

struct TestCase
{
    const char* module;
    const char* name;
    void (*run)();
};

// Every registered test; registration order within a module is preserved.
std::vector<TestCase>& registry();

struct ModuleRegistrar
{
    ModuleRegistrar(const char* module, std::initializer_list<std::pair<const char*, void (*)()>> tests);
};

// A failed requirement ends the run: the remaining tests may depend on what failed.
void require(const bool condition, const std::string& message);

// The value of a successful result; a failure fails the test with the error message.
template <class T> T expect_ok(eaw::Result<T> result, const std::string& what)
{
    require(result.ok(), what + ": " + (result.ok() ? std::string{} : result.error().message));
    return std::move(result).value();
}

void expect_ok(const eaw::Status& status, const std::string& what);

template <typename Integer> void write_little_endian(std::ostream& stream, Integer value)
{
    for (std::size_t index = 0; index < sizeof(Integer); ++index)
    {
        stream.put(static_cast<char>((value >> (index * 8U)) & 0xffU));
    }
}

void write_test_meg(const std::filesystem::path& path, const std::vector<std::pair<std::string, std::string>>& entries);
// Writes `text` to `path`, creating its folders (fixture files in a temporary folder).
void write_file(const std::filesystem::path& path, std::string_view text);

std::filesystem::path write_test_meg();

std::string read_vfs_text(const eaw::VirtualFileSystem& vfs, const std::string_view path);

void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value);

void append_float(std::vector<std::byte>& bytes, const float value);

void append_u16(std::vector<std::byte>& bytes, const std::uint16_t value);

void append_string(std::vector<std::byte>& bytes, const std::string_view value);

void append_wide_string(std::vector<std::byte>& bytes, const std::u16string_view value);

void append_chunk(std::vector<std::byte>& target, const std::uint32_t type, const std::vector<std::byte>& payload, const bool container = false);

void append_mini(std::vector<std::byte>& target, const std::uint8_t type, const std::vector<std::byte>& payload);

std::vector<std::byte> make_test_alo(const bool root_visible = true);

// A minimal 4x4 BC1 DDS with one mip level.
std::vector<std::byte> make_test_dds();

std::vector<std::byte> make_test_ala();

std::vector<std::byte> make_test_ted();

std::vector<std::byte> make_test_dat();

// A classic particle ALO (root chunk 0x0900) named p_test with two emitters, "sparks" and "smoke";
// `death_child` links sparks' death emitter, and the flags add an unknown property or truncate a
// shape group.
std::vector<std::byte> make_test_particle_system(const std::int32_t death_child, const bool unknown_property = false, const bool short_group = false);

// An entity as tests describe it: its recipe's fields and its instance's in one flat list (the
// layout EntitySpawn had before recipes), so a test names only what it needs. test_spawn registers
// the recipe and spawns the instance; map_entity makes a battle map's entity of it.
struct TestSpawn
{
    std::string type_name;
    std::uint32_t owner{};
    eaw::Vector3 position;
    float heading{};
    float max_speed{};
    std::uint32_t visual{};
    bool selectable{true};
    bool follows_ground{};
    std::uint32_t passable_types{};
    eaw::CombatProfile combat;
    float radius{};
    eaw::FootprintCapsule capsule;
    float turn_rate{};
    eaw::MovementProfile movement;
    std::uint32_t formation_order{2};
    float formation_spacing{0.5F};
    bool formation_solo{};
    eaw::FormationTeam formation_team;
    eaw::CrushProfile crush;
    std::optional<eaw::GroundFootprint> footprint;
    float min_speed{};
    eaw::Stance stance{eaw::Stance::aggressive};
    bool aims_with_body{};
    // A team's ring arrangement (the entity is a team), and the team a squad member joins.
    std::optional<eaw::TeamArrangement> team;
    eaw::EntityId squad;
};

inline eaw::EntityRecipe recipe_of(const TestSpawn& spawn)
{
    return {.type_name = spawn.type_name,
            .max_speed = spawn.max_speed,
            .follows_ground = spawn.follows_ground,
            .passable_types = spawn.passable_types,
            .combat = spawn.combat,
            .radius = spawn.radius,
            .capsule = spawn.capsule,
            .turn_rate = spawn.turn_rate,
            .movement = spawn.movement,
            .formation_order = spawn.formation_order,
            .formation_spacing = spawn.formation_spacing,
            .formation_solo = spawn.formation_solo,
            .crush = spawn.crush,
            .min_speed = spawn.min_speed,
            .aims_with_body = spawn.aims_with_body,
            .team = spawn.team};
}

inline eaw::EntitySpawn instance_of(const TestSpawn& spawn, const eaw::RecipeId recipe = 0)
{
    return {.recipe = recipe,
            .owner = spawn.owner,
            .position = spawn.position,
            .heading = spawn.heading,
            .visual = spawn.visual,
            .selectable = spawn.selectable,
            .formation_team = spawn.formation_team,
            .footprint = spawn.footprint,
            .stance = spawn.stance,
            .squad = spawn.squad.valid() ? std::optional<std::uint32_t>(0) : std::nullopt};
}

inline eaw::Result<eaw::EntityId> test_spawn(eaw::SimulationWorld& world, const TestSpawn& spawn)
{
    EAW_TRY_ASSIGN(const auto recipe, world.add_recipe(recipe_of(spawn)));
    return world.spawn(instance_of(spawn, recipe), spawn.squad);
}

inline eaw::MapEntity map_entity(const TestSpawn& spawn)
{
    return {recipe_of(spawn), instance_of(spawn)};
}
} // namespace eaw_test
