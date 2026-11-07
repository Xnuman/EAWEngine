#pragma once

#include "eaw/visual_scene.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace eaw {

struct D3d12RendererOptions
{
    bool vsync{true};
    bool use_warp{};
    bool gpu_validation{};
    std::uint32_t maximum_frame_latency{2};
    // When set, compiled pipeline states are cached in this directory (d3d12_pipelines.bin).
    // Empty disables the cache; nothing is written anywhere else.
    std::filesystem::path pipeline_cache_directory;
};

struct D3d12RendererStats
{
    std::uint64_t frame_number{};
    std::uint64_t direct_fence_completed{};
    std::uint64_t copy_fence_completed{};
    std::uint32_t pipeline_cache_hits{};
    std::uint32_t pipeline_cache_misses{};
    std::string pipeline_cache_status;
    // Resource lifetime: textures resident in the pool, texture-table slots free or awaiting
    // their release fence, and retired resource groups still waiting for the GPU.
    std::size_t live_textures{};
    std::size_t free_texture_slots{};
    std::size_t pending_texture_slots{};
    std::size_t pending_retirements{};
};

class D3d12Renderer final
{
public:
    D3d12Renderer(void* native_window, std::uint32_t width, std::uint32_t height, const D3d12RendererOptions& options = {});
    ~D3d12Renderer();

    D3d12Renderer(const D3d12Renderer&) = delete;
    D3d12Renderer& operator=(const D3d12Renderer&) = delete;
    D3d12Renderer(D3d12Renderer&&) noexcept;
    D3d12Renderer& operator=(D3d12Renderer&&) noexcept;

    // Returns false when rendering is intentionally skipped, for example while
    // the window is occluded or the frame-latency object has not signalled yet.
    bool render();
    void resize(std::uint32_t width, std::uint32_t height);
    void wait_idle();
    // Replaces the current scene; may be called at any time. Resources of the replaced scene stay
    // alive until the GPU has finished every frame that used them; the CPU does not wait.
    void load_scene(const VisualScene& scene);
    void set_bone_transforms(std::span<const std::array<float, 16>> transforms);
    // Throws std::invalid_argument for non-finite or degenerate cameras.
    void set_camera(const VisualCamera& camera);
    // Throws std::invalid_argument for non-finite values or a zero light direction.
    void set_lighting(const VisualLightRig& lighting);
    // Seconds of scene time driving texture-coordinate scrolling.
    void set_scene_time(float seconds);

    [[nodiscard]] const std::string& adapter_name() const;
    [[nodiscard]] D3d12RendererStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eaw
