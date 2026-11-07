#pragma once

#include "eaw/ala_animation.hpp"
#include "eaw/alo_model.hpp"
#include "eaw/visual_scene.hpp"

#include <array>
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

[[nodiscard]] VisualModelAsset build_visual_model(const VirtualFileSystem& vfs, std::string_view model_path);
[[nodiscard]] std::optional<std::string> default_model_animation(const VirtualFileSystem& vfs, std::string_view model_path);
[[nodiscard]] std::vector<std::array<float, 16>> sample_skin_palette(const VisualModelAsset& asset, const AlaAnimation* animation, float seconds);

} // namespace eaw
