#pragma once

#include "eaw/visual_scene.hpp"

#include <string_view>

namespace eaw {

class VirtualFileSystem;

[[nodiscard]] VisualScene build_visual_scene(const VirtualFileSystem& vfs, std::string_view model_path);

} // namespace eaw
