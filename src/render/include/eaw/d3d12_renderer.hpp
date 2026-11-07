#pragma once

#include "eaw/visual_scene.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
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
    // Texture bytes uploaded per frame while a scene streams in; at least one texture is always
    // uploaded. Zero uploads every queued texture in the first frame.
    std::uint64_t texture_upload_budget_bytes{64ULL * 1024ULL * 1024ULL};
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
    // Scene textures still waiting to stream in (drawn with fallbacks meanwhile).
    std::size_t pending_texture_uploads{};
    // Textures the last load_scene kept from the replaced scene (same identity) instead of uploading.
    std::size_t reused_textures{};
    // Debug lines beyond the per-frame capacity in the current set.
    std::size_t dropped_debug_lines{};
    // Last recorded frame: draw calls, triangles, and instances submitted.
    std::uint32_t draw_calls{};
    std::uint64_t triangles{};
    std::uint64_t instances{};
    // GPU time of the most recent frame the GPU finished (timestamp queries; no CPU wait).
    double gpu_frame_milliseconds{};
    // Local video memory in use by this process and the OS budget for it (bytes, refreshed every
    // 30 frames).
    std::uint64_t video_memory_usage{};
    std::uint64_t video_memory_budget{};
    std::uint32_t texture_slot_capacity{};
    std::uint32_t width{};
    std::uint32_t height{};
    bool tearing{};
};

// Result of a pick: the scene instance under the pixel, the terrain, or nothing (both empty).
// Blended effects and backgrounds are not pickable.
struct VisualPick
{
    std::optional<std::uint32_t> instance;
    bool terrain{};
    // Depth-buffer value (0..1) of the picked surface; unset when nothing was hit.
    std::optional<float> depth;
    // The client pixel that was picked (clamped to the target).
    std::array<std::uint32_t, 2> pixel{};
    // The id request_pick returned for this pick.
    std::uint32_t request{};
};

// Mouse state for the debug overlay, in client pixels.
struct VisualOverlayInput
{
    float mouse_x{-1.0F};
    float mouse_y{-1.0F};
    bool left_button{};
    bool right_button{};
    // Wheel steps since the previous call (positive away from the user).
    float wheel{};
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
    // Replaces the current scene; may be called at any time. Geometry and materials upload at once;
    // textures are validated here and stream in under the per-frame budget, drawn with fallbacks
    // until they arrive. Resources of the replaced scene stay alive until the GPU has finished
    // every frame that used them; the CPU does not wait. Pass an rvalue to avoid copying textures.
    void load_scene(VisualScene scene);
    void set_bone_transforms(std::span<const std::array<float, 16>> transforms);
    // Replaces the instances drawn each frame with this list, until the next call or load_scene
    // (which returns to the scene's own instances). The loaded scene's meshes form the mesh library:
    // instances may reference any of them, appear, move, or disappear from frame to frame. Pick ids
    // and set_highlight then refer to positions in this list.
    void set_instances(std::span<const VisualInstance> instances);
    // Throws std::invalid_argument for non-finite or degenerate cameras.
    void set_camera(const VisualCamera& camera);
    // Throws std::invalid_argument for non-finite values or a zero light direction.
    void set_lighting(const VisualLightRig& lighting);
    // Seconds of scene time driving texture-coordinate scrolling.
    void set_scene_time(float seconds);
    // Asks which surface is visible at client pixel (x, y) in the next rendered frame and returns
    // the request id. Answers arrive through take_pick() in request order once their frame has
    // finished on the GPU (usually one or two frames later); the CPU never waits, and no request
    // is dropped (more than eight per frame are copied over several frames).
    std::uint32_t request_pick(std::uint32_t x, std::uint32_t y);
    // The oldest completed pick, if any; call until it returns nothing.
    [[nodiscard]] std::optional<VisualPick> take_pick();
    // Draws one scene instance highlighted (nothing for nullopt); cleared by load_scene.
    void set_highlight(std::optional<std::uint32_t> instance);
    // Debug lines drawn every frame until replaced. At most max_debug_lines() are drawn; the rest
    // are counted in stats().dropped_debug_lines.
    void set_debug_lines(std::span<const VisualDebugLine> lines);
    [[nodiscard]] static std::size_t max_debug_lines() noexcept;
    // Text lines of the on-screen debug panel (top left); an empty line draws a separator and no
    // lines hides the panel.
    void set_overlay_text(std::span<const std::string> lines);
    // Mouse input for the overlay (its "-"/"+" button, moving, and resizing). Call once per frame.
    void set_overlay_input(const VisualOverlayInput& input);
    // Screen-space selection box (x0, y0, x1, y1 in client pixels) drawn over everything; nullopt
    // hides it. Shown even when the statistics panel is hidden.
    void set_selection_rectangle(const std::optional<std::array<float, 4>>& rectangle);
    // True while the mouse is over the overlay or dragging it, as of the last rendered frame; the
    // caller should then not use the mouse for the scene.
    [[nodiscard]] bool overlay_wants_mouse() const noexcept;

    [[nodiscard]] const std::string& adapter_name() const;
    [[nodiscard]] D3d12RendererStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eaw
