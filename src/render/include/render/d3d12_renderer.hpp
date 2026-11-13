#pragma once

#include "core/status.hpp"
#include "formats/rgba_image.hpp"
#include "render/visual_anti_aliasing.hpp"
#include "render/visual_camera.hpp"
#include "render/visual_lod.hpp"
#include "render/visual_scene.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace eaw {

// The rendering features that may change from frame to frame (D3d12Renderer::set_graphics_settings).
struct D3d12GraphicsSettings
{
    bool vsync{true};
    // Bloom: bright parts of the frame (brightest channel above `bloom_threshold`, 0..1) blurred
    // at quarter resolution and added back scaled by `bloom_strength`. An engine choice standing in
    // for the original's glow option; the values are tuned by eye against the original.
    bool bloom{true};
    float bloom_threshold{0.6F};
    float bloom_strength{0.8F};
    // Anti-aliasing: an edge pass in the manner of FXAA over the finished frame, temporal (jittered
    // frames blended with the reprojected history), or both; engine choices.
    VisualAntiAliasing anti_aliasing{VisualAntiAliasing::fxaa};
    // Key-light shadows: cascaded shadow maps (render/shadow_cascades.hpp), every model and the
    // terrain casting, in `shadow_cascades` cascades (1-4; an engine choice).
    bool shadows{true};
    std::uint32_t shadow_cascades{3};
    // How the map environment's fog is drawn (VisualLightRig::fog), when it has one.
    VisualFogMode fog{VisualFogMode::legacy};
    // How the map's water surface is drawn (VisualMaterial::water).
    VisualWaterMode water{VisualWaterMode::legacy};
    // The environment's cloud shadows on the terrain (VisualLightRig::clouds), as the original.
    bool cloud_shadows{true};
    // Enhanced: the ambient takes the captured sky's light (its color and direction, at the map's own
    // ambient brightness; docs/sky-research.md).
    bool sky_ambient{false};
    // Enhanced: the fill light of the world dims by how much sky the terrain under it sees
    // (VisualTerrain::sky_visibility_texture).
    bool sky_visibility{false};
    // Units whose type asks for it show through what hides them (GameConstants'
    // Occlusion_Silhouettes_Enabled is on in the stock data).
    bool occlusion_silhouettes{true};
    // How heat distortion is drawn (VisualParticleBlend::heat).
    VisualHeatMode heat{VisualHeatMode::legacy};
    // Enhanced: depth-tested particles fade out as the surface behind them comes near, instead of
    // cutting a hard line where they meet it (soft particles).
    bool soft_particles{false};
};

struct D3d12RendererOptions
{
    D3d12GraphicsSettings graphics;
    bool use_warp{};
    bool gpu_validation{};
    std::uint32_t maximum_frame_latency{2};
    // When set, compiled pipeline states are cached in this directory (d3d12_pipelines.bin).
    // Empty disables the cache; nothing is written anywhere else.
    std::filesystem::path pipeline_cache_directory;
    // Texture bytes uploaded per frame while a scene streams in; at least one texture is always
    // uploaded. Zero uploads every queued texture in the first frame.
    std::uint64_t texture_upload_budget_bytes{64ULL * 1024ULL * 1024ULL};
    // Each shadow map's texels per side (an engine choice); fixed for the renderer's life.
    std::uint32_t shadow_resolution{2048};
    // Detail levels of meshes that have them (eaw/visual_lod.hpp); set_lod_settings changes them.
    VisualLodSettings lod;
    // Where the overlay's panels keep their positions and sizes between runs (a Dear ImGui settings
    // file); empty keeps nothing.
    std::filesystem::path overlay_settings_file;
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
    // Entries in the current bone palette.
    std::size_t bone_transforms{};
    // Last recorded frame: draw calls, triangles, and instances submitted.
    std::uint32_t draw_calls{};
    std::uint64_t triangles{};
    std::uint64_t instances{};
    // Particles drawn in the last recorded frame.
    std::uint64_t particles{};
    // Instances drawn below their most detailed level, and instances cross-fading between two
    // levels, in the last recorded frame.
    std::uint64_t reduced_detail_instances{};
    std::uint64_t fading_instances{};
    // Cross-fades begun since the scene was loaded.
    std::uint64_t fades_started{};
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
    // Characters typed since the previous call, and the editing keys held down, for text fields.
    std::u32string text;
    bool backspace{};
    bool del{};
    bool enter{};
    bool escape{};
    bool tab{};
    bool left{};
    bool right{};
    bool up{};
    bool down{};
    bool home{};
    bool end{};
    bool control{};
    bool shift{};
};

// Programming errors in calls (invalid camera, lighting, instances, debug lines, bone palettes, or
// scene time produced by engine code) are assertions. A D3D12 or DXGI failure after startup, such
// as device removal, ends the process through eaw::fatal_error with the device-removed diagnostics.
class D3d12Renderer final
{
public:
    // Creates the device, queues, swap chain, and pipelines for a window (an HWND). Returns an
    // error when no suitable adapter exists, the device lacks a required feature, or the device,
    // queues, swap chain, or debug overlay cannot be created.
    [[nodiscard]] static Result<std::unique_ptr<D3d12Renderer>> create(void* native_window, std::uint32_t width, std::uint32_t height, const D3d12RendererOptions& options = {});
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
    // Returns an error for an invalid scene (no geometry, capacities exceeded, references outside
    // the scene, non-finite data, textures D3D12 cannot lay out); the current scene then stays.
    [[nodiscard]] Status load_scene(VisualScene scene);
    // Replaces the bone palette that instances index through bone_offset (at most
    // max_bone_transforms() entries; at least the loaded scene's own palette size, padded with
    // identity). Set it before the instances that use it: set_instances checks their bone ranges
    // against the current palette. Frame buffers grow as needed; load_scene resets the palette to
    // identity matrices of the scene's size.
    void set_bone_transforms(std::span<const Matrix4> transforms);
    // Adds meshes to the loaded scene's mesh library and returns the index of the first: `meshes`
    // carries vertices, indices, textures, materials, draws, and meshes with indices local to it
    // (no instances or terrain). Geometry and materials upload at once into a geometry block of
    // their own; textures stream in like the scene's, and a texture whose identity is already in
    // the scene is shared. Existing mesh indices stay valid; load_scene drops added meshes.
    // Returns an error (adding nothing) without a loaded scene or for an invalid fragment.
    [[nodiscard]] Result<std::uint32_t> add_meshes(VisualScene meshes);
    // Adds textures to the loaded scene (particle textures) and returns each one's index in the
    // renderer's texture list (the scene's textures, then those added since), as VisualParticle
    // names them. They stream in like the scene's; an identity already present is shared.
    // load_scene drops them. Returns an error (adding nothing) for an invalid texture.
    [[nodiscard]] Result<std::vector<std::uint32_t>> add_textures(std::vector<VisualTexture> textures);
    // Particles drawn every frame until replaced (any count; frame buffers grow as needed), after
    // the scene and before debug lines, in list order, without depth or pick writes. Textures are
    // indices into the renderer's texture list (see add_textures); out-of-range indices draw white.
    void set_particles(std::span<const VisualParticle> particles);
    // The terrain's decals for the following frames (copied).
    void set_decals(std::span<const VisualDecal> decals);
    [[nodiscard]] static std::size_t max_bone_transforms() noexcept;
    // Replaces the instances drawn each frame with this list, until the next call or load_scene
    // (which returns to the scene's own instances). The loaded scene's meshes form the mesh library:
    // instances may reference any of them, appear, move, or disappear from frame to frame. Pick ids
    // and set_highlight then refer to positions in this list.
    void set_instances(std::span<const VisualInstance> instances);
    // Non-finite or degenerate cameras are a programming error (assertion).
    void set_camera(const VisualCamera& camera);
    // Non-finite values or a zero light direction are a programming error (assertion).
    void set_lighting(const VisualLightRig& lighting);
    // Seconds of scene time driving texture-coordinate scrolling.
    void set_scene_time(float seconds);
    // Detail-level selection from the next frame on (invalid values fall back to the defaults).
    void set_lod_settings(const VisualLodSettings& settings);
    [[nodiscard]] const VisualLodSettings& lod_settings() const noexcept;
    // The switchable features from the next frame on; the shadow maps are created the first time
    // shadows are on.
    void set_graphics_settings(const D3d12GraphicsSettings& settings);
    [[nodiscard]] const D3d12GraphicsSettings& graphics_settings() const noexcept;
    // Asks which surface is visible at client pixel (x, y) in the next rendered frame and returns
    // the request id. Answers arrive through take_pick() in request order once their frame has
    // finished on the GPU (usually one or two frames later); the CPU never waits, and no request
    // is dropped (more than eight per frame are copied over several frames).
    std::uint32_t request_pick(std::uint32_t x, std::uint32_t y);
    // The oldest completed pick, if any; call until it returns nothing.
    [[nodiscard]] std::optional<VisualPick> take_pick();
    // Copies the next rendered frame as it is after the scene and post-processing, before debug lines
    // and the overlay (for screenshots and image regression). The image arrives through
    // take_capture() once that frame has finished on the GPU; the CPU never waits for it.
    void request_capture();
    [[nodiscard]] std::optional<RgbaImage> take_capture();
    // Draws one scene instance highlighted (nothing for nullopt); cleared by load_scene.
    void set_highlight(std::optional<std::uint32_t> instance);
    // Debug lines drawn every frame until replaced. At most max_debug_lines() are drawn; the rest
    // are counted in stats().dropped_debug_lines.
    void set_debug_lines(std::span<const VisualDebugLine> lines);
    [[nodiscard]] static std::size_t max_debug_lines() noexcept;
    // Mouse and keyboard input for the overlay's panels (set_overlay_ui). Call once per frame.
    void set_overlay_input(const VisualOverlayInput& input);
    // Screen-space selection box (x0, y0, x1, y1 in client pixels) drawn over everything; nullopt
    // hides it. Shown even when the statistics panel is hidden.
    void set_selection_rectangle(const std::optional<std::array<float, 4>>& rectangle);
    // True while the mouse is over the overlay or dragging it, as of the last rendered frame; the
    // caller should then not use the mouse for the scene.
    [[nodiscard]] bool overlay_wants_mouse() const noexcept;
    // True while a text field of the overlay has the keyboard; the caller should then not use keys
    // as hotkeys.
    [[nodiscard]] bool overlay_wants_keyboard() const noexcept;
    // Engine-built Dear ImGui windows, drawn every rendered frame after the statistics panel inside
    // the overlay's ImGui frame (the callback may use the ImGui API; it runs on the render thread
    // during render()). An empty function draws none.
    void set_overlay_ui(std::function<void()> build);

    [[nodiscard]] const std::string& adapter_name() const;
    [[nodiscard]] D3d12RendererStats stats() const;

private:
    struct Impl;
    explicit D3d12Renderer(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace eaw
