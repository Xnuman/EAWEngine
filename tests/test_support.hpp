#pragma once

// Shared helpers for the EAWEngine test modules: requirement checks, binary writers, and synthetic
// format fixtures. Each module file registers its tests with a ModuleRegistrar at its end.

#include "eaw/ala_animation.hpp"
#include "eaw/alo_model.hpp"
#include "eaw/asset_environment.hpp"
#include "eaw/asset_path.hpp"
#include "camera_controller.hpp"
#include "combat_content.hpp"
#include "visual_scene_builder.hpp"
#include "eaw/dat_file.hpp"
#include "eaw/dds_texture.hpp"
#include "eaw/game_installation.hpp"
#include "asset_cache.hpp"
#include "camera_math.hpp"
#include "debug_draw.hpp"
#include "rts_control.hpp"
#include "terrain_height.hpp"
#include "scene_presentation.hpp"
#include "core/fatal_error.hpp"
#include "core/log.hpp"
#include "core/status.hpp"
#include "eaw/game_object_catalog.hpp"
#include "eaw/simulation.hpp"
#include "eaw/gpu_resource_lifetime.hpp"
#include "eaw/legacy_material.hpp"
#include "eaw/localization_scan.hpp"
#include "eaw/localization_workspace.hpp"
#include "eaw/map_object_layout.hpp"
#include "eaw/lua_script.hpp"
#include "eaw/meg_archive.hpp"
#include "eaw/mtd_directory.hpp"
#include "eaw/particle_system.hpp"
#include "core/string_utils.hpp"
#include "eaw/ted_map.hpp"
#include "eaw/virtual_file_system.hpp"
#include "eaw/xml_asset_database.hpp"
#include "eaw/xml_document.hpp"
#include "eaw/xml_schema.hpp"
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

} // namespace eaw_test
