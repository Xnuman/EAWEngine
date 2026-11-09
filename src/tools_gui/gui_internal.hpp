#pragma once

// Shared pieces of the EAWTools workbench (internal): colors, preview types, the helpers several
// panels use, and GuiApp::State. Each panel lives in its own gui_*.cpp.

#include "gui_app.hpp"

#include "eaw/file_dialogs.hpp"
#include "preview_renderer.hpp"
#include "eaw/ala_animation.hpp"
#include "eaw/alo_model.hpp"
#include "eaw/asset_environment.hpp"
#include "eaw/dat_file.hpp"
#include "eaw/dds_texture.hpp"
#include "eaw/game_installation.hpp"
#include "eaw/game_object_catalog.hpp"
#include "eaw/legacy_material.hpp"
#include "eaw/localization_scan.hpp"
#include "eaw/localization_workspace.hpp"
#include "eaw/map_object_layout.hpp"
#include "eaw/lua_script.hpp"
#include "eaw/mtd_directory.hpp"
#include "eaw/text_encoding.hpp"
#include "eaw/ted_map.hpp"
#include "eaw/virtual_file_system.hpp"
#include "eaw/xml_asset_database.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eaw::tools {

inline constexpr ImVec4 accent{0.16F, 0.72F, 0.72F, 1.0F};

inline constexpr ImVec4 warning{0.96F, 0.69F, 0.24F, 1.0F};

inline constexpr ImVec4 danger{0.96F, 0.35F, 0.39F, 1.0F};

inline constexpr ImVec4 muted{0.58F, 0.64F, 0.70F, 1.0F};

struct KnownAssetExtension
{
    const char* label;
    const char* extension;
};

inline constexpr std::array known_asset_extensions{KnownAssetExtension{"All extensions", ""},        KnownAssetExtension{"ALO models / particles", ".alo"},
                                                   KnownAssetExtension{"ALA animations", ".ala"},    KnownAssetExtension{"TED maps", ".ted"},
                                                   KnownAssetExtension{"DDS textures", ".dds"},      KnownAssetExtension{"TGA textures", ".tga"},
                                                   KnownAssetExtension{"MTD mega-textures", ".mtd"}, KnownAssetExtension{"DAT localization", ".dat"},
                                                   KnownAssetExtension{"XML definitions", ".xml"},   KnownAssetExtension{"Lua scripts", ".lua"},
                                                   KnownAssetExtension{"WAV audio", ".wav"},         KnownAssetExtension{"BIK videos", ".bik"}};

struct PreviewTexture
{
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    std::uint32_t width{};
    std::uint32_t height{};
};

// Terrain triangle for land-map previews, in preview space.
struct PreviewTriangle
{
    AloVector3 points[3];
    ImU32 color{};
};

// GPU geometry for one placed-object model, shared by every instance on the current map.
struct MapModelGpu
{
    PreviewMesh mesh;
    float radius{1.0F};
    bool valid{};
};

// CPU geometry for a GPU preview: one part per visible submesh, positions posed by `pose` (CPU
// skinning) or, without a pose, placed in bind pose.
struct PreviewGeometry
{
    std::vector<PreviewVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<PreviewPart> parts;
    AloVector3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    AloVector3 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    // Distance of the farthest vertex from the model origin.
    float radius{};
};

std::string lower(std::string value);

std::string human_size(std::uint64_t bytes);

Result<PreviewTexture> create_dds_texture(ID3D11Device* device, std::span<const std::byte> bytes);

std::string clean_one_line(std::string value, std::size_t maximum = 90);

Result<AloModel> read_model(const AssetReader& vfs, const std::string& path);

Result<std::string> bytes_preview(const ResolvedAsset& asset, std::span<const std::byte> bytes);

void label_value(const char* label, std::string_view value);

PreviewGeometry build_preview_geometry(const AloModel& model, const std::vector<AlaPoseBone>* pose, const std::vector<AloMatrix4>& inverse_bind, bool show_collision,
                                       const std::function<ID3D11ShaderResourceView*(const std::string&)>& texture);

// Renders `meshes` into `target` and shows it over the canvas. Returns false (after drawing a
// message) when the GPU preview pipeline is unavailable.
bool draw_gpu_preview(GuiApp::State& state, PreviewTarget& target, const ImVec2 canvas_min, const ImVec2 canvas_max, const AloVector3& center, float yaw, float pitch, float scale,
                      const float depth_radius, const std::span<const PreviewMesh* const> meshes);

void render_map_viewport(GuiApp::State& state, bool reserve_footer);

void render_large_map_preview(GuiApp::State& state);

void render_model_viewport(GuiApp::State& state);

void render_asset_workspace(GuiApp::State& state, HWND owner);

void render_localization_workspace(GuiApp::State& state, HWND owner);

struct GuiApp::State
{
    enum class Workspace
    {
        assets,
        localization
    } workspace{Workspace::assets};

    ID3D11Device* graphics_device{};
    std::string game_root;
    GameVariant variant{GameVariant::forces_of_corruption};
    std::vector<std::filesystem::path> mods;
    VirtualFileSystem vfs;
    std::vector<ResolvedAsset> assets;
    std::string asset_query;
    std::string asset_extension;
    int selected_asset{-1};
    std::string asset_preview;
    std::vector<std::string> mount_warnings;
    std::optional<AloModel> preview_model;
    std::optional<TedMap> preview_map;
    std::vector<AloMatrix4> preview_inverse_bind;
    std::vector<ResolvedAsset> preview_animation_assets;
    std::optional<AlaAnimation> preview_animation;
    int preview_animation_index{-1};
    float preview_time{};
    float preview_yaw{-0.65F};
    float preview_pitch{0.35F};
    float preview_zoom{1.0F};
    bool preview_playing{true};
    bool preview_show_skeleton{};
    bool preview_show_collision{};
    // GPU preview: the model mesh is rebuilt when the model or collision toggle changes, and every
    // frame while an animation plays (skinning runs on the CPU).
    PreviewRenderer renderer;
    PreviewMesh model_mesh;
    PreviewTarget model_target;
    bool model_mesh_cached{};
    bool model_mesh_collision{};
    AloVector3 model_minimum{};
    AloVector3 model_maximum{};
    std::unordered_map<std::string, PreviewTexture> preview_textures;
    std::vector<std::string> preview_texture_warnings;
    float map_preview_yaw{-0.65F};
    float map_preview_pitch{0.72F};
    float map_preview_zoom{1.0F};
    float map_preview_vertical_scale{1.0F};
    int map_preview_triangle_budget{18000};
    bool map_preview_show_passability{};
    bool map_preview_show_wireframe{};
    bool map_preview_show_grid{true};
    bool map_preview_show_objects{true};
    bool map_preview_show_altitude{true};
    float map_preview_marker_scale{1.0F};
    bool map_preview_large_window{};
    bool map_preview_cached{};
    std::vector<PreviewTriangle> map_preview_triangles;
    PreviewTarget map_target;
    PreviewMesh terrain_mesh;
    bool terrain_mesh_cached{};

    // Object-type index for map previews, built lazily per mounted VFS. The catalog borrows the
    // database, so both live in one heap allocation with a stable address.
    struct ObjectIndex
    {
        XmlAssetDatabase database;
        GameObjectCatalog catalog;
    };
    std::unique_ptr<ObjectIndex> object_index;
    // Textures for map-object models persist across map selections, like the model proxies.
    std::unordered_map<std::string, PreviewTexture> object_textures;
    std::unordered_map<std::string, MapModelGpu> map_models;
    std::optional<MapObjectLayout> map_object_layout;
    std::vector<int> map_object_placement;
    std::vector<std::string> map_object_warnings;
    bool map_object_cached{};
    std::vector<char> map_object_drawn;

    void reset_map_objects();

    void load_map_objects();

    std::optional<std::filesystem::path> base_dat_path;
    std::optional<std::filesystem::path> workspace_path;
    std::vector<LocalizationRow> rows;
    std::string row_query;
    std::string status_filter{"all"};
    int selected_row{-1};
    bool dirty{};
    bool skip_todo{};
    std::vector<std::string> validation_messages;

    std::string new_key;
    std::string unit_id;
    std::string unit_name;
    std::string unit_description;

    std::string message{"Ready"};
    bool message_error{};

    explicit State(ID3D11Device* device);

    void succeed(std::string text);

    void fail(const Error& error);

    void load_assets();

    void select_preview_animation(int index);

    void inspect_asset(int index);

    // Loads the DDS for an ALO texture reference into `cache` (keyed by folded reference) and
    // returns its view, or null when it is missing or undecodable. Failures are cached too.
    ID3D11ShaderResourceView* load_texture(std::unordered_map<std::string, PreviewTexture>& cache, std::vector<std::string>& warnings, const std::string& reference);

    void open_dat(const std::filesystem::path& path);

    void open_workspace(const std::filesystem::path& path);

    void save_workspace(const std::filesystem::path& path);

    void validate_workspace();

    void build_dat(const std::filesystem::path& output);

    void scan_mod(const std::filesystem::path& mod_root);

    Status add_localization_row(LocalizationRow row);
};

} // namespace eaw::tools
