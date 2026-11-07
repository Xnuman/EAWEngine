#pragma once

#include "eaw/visual_scene.hpp"

#include <array>
#include <cstdint>
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
};

struct D3d12RendererStats
{
    std::uint64_t frame_number{};
    std::uint64_t direct_fence_completed{};
    std::uint64_t copy_fence_completed{};
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
    void load_scene(const VisualScene& scene);
    void set_bone_transforms(std::span<const std::array<float, 16>> transforms);
    void orbit_camera(float yaw_delta, float pitch_delta);
    void zoom_camera(float wheel_delta);
    void reset_camera();

    [[nodiscard]] const std::string& adapter_name() const;
    [[nodiscard]] D3d12RendererStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eaw
