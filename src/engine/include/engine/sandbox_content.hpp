#pragma once

// What the sandbox shows, built off the render thread: the map or model scene, the model's
// animations, and the files the build read (for hot reload).

#include "vfs/virtual_file_system.hpp"
#include "assets/animation.hpp"
#include "assets/asset_cache.hpp"
#include "engine/sandbox_options.hpp"
#include "engine/visual_scene_builder.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

struct SandboxContent
{
    std::optional<VisualMapAsset> map;
    std::optional<VisualModelAsset> model;
    std::optional<Animation> animation;
    std::string animation_path;
    // What decoding the animation file reported.
    std::vector<std::string> animation_warnings;
    // Every animation beside the model, for runtime selection.
    std::vector<std::string> animations;
    std::string animation_error;
    // When set, nothing was built and the current view stays.
    std::string error;
    double build_seconds{};
    // Files the build consulted (also after a failure, so fixing a file triggers a rebuild).
    DependencySet dependencies;
    // Cache entries dropped because their files changed (rebuilds only).
    std::vector<std::string> invalidated;
    // Cache entries the successful build no longer used.
    std::size_t evicted{};
    bool reload{};
};

// Result of a background hot-reload check: archives whose file changed and scene dependencies
// whose resolved source changed.
struct HotReloadCheck
{
    std::vector<std::size_t> archives;
    std::vector<std::string> files;
    double seconds{};
};

// An animation clip from its file; the decoder's warnings go to `warnings` when given.
[[nodiscard]] Result<Animation> read_animation(const AssetReader& reader, std::string_view path, std::vector<std::string>* warnings = nullptr);

// Builds the map (with the requested spawnable types) or the model and its animation. Reading the
// VFS from a worker is safe because its const reads open their own streams, and the main thread
// does not use the VFS while a build runs. A rebuild (`reload`) first drops cache entries whose
// files changed.
// `extra_spawn_types` are types added at runtime, kept spawnable across rebuilds.
[[nodiscard]] SandboxContent build_sandbox_content(const VirtualFileSystem& vfs, const SandboxOptions& options, AssetCache* cache, bool reload, std::vector<std::string> extra_spawn_types = {});

} // namespace eaw
