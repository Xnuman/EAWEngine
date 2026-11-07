#pragma once

#include "eaw/ala_animation.hpp"
#include "eaw/alo_model.hpp"
#include "eaw/visual_scene.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

class VirtualFileSystem;

struct VisualModelAsset
{
    VisualScene scene;
    AloModel model;
    std::vector<AloMatrix4> inverse_bind_pose;
};

// A stock or mod map's placed objects as one instanced scene (no terrain yet). Counts describe
// placed objects that could not be drawn and why.
struct VisualMapAsset
{
    VisualScene scene;
    bool land{};
    std::size_t untyped{};
    std::size_t without_model{};
    std::size_t missing_model_files{};
    std::size_t expanded_members{};
    std::size_t unexpanded_containers{};
    std::size_t unrenderable_placements{};
    // Resolved backdrop layers ("definition (model path)"), in draw order.
    std::vector<std::string> backdrops;
};

[[nodiscard]] VisualModelAsset build_visual_model(const VirtualFileSystem& vfs, std::string_view model_path);
[[nodiscard]] VisualMapAsset build_visual_map(const VirtualFileSystem& vfs, std::string_view map_path);
[[nodiscard]] std::optional<std::string> default_model_animation(const VirtualFileSystem& vfs, std::string_view model_path);
[[nodiscard]] std::vector<std::array<float, 16>> sample_skin_palette(const VisualModelAsset& asset, const AlaAnimation* animation, float seconds);

} // namespace eaw
