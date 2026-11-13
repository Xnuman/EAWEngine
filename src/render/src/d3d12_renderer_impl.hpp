#pragma once

// D3d12Renderer's implementation: every Direct3D 12 object and the state the public renderer methods
// forward to. Member functions are defined by subject in d3d12_renderer_device.cpp,
// d3d12_renderer_pipelines.cpp, d3d12_renderer_resources.cpp, d3d12_renderer_frame.cpp, and
// d3d12_renderer_overlay.cpp. Internal to src/render.

#include "d3d12_support.hpp"

#include "render/d3d12_renderer.hpp"
#include "render/frame_graph.hpp"
#include "render/lod_selection.hpp"
#include "render/shadow_cascades.hpp"
#include "render/wind_bend.hpp"

#include <optional>
#include <vector>

namespace eaw {

// The renderer's helpers and constants are used throughout the implementation.
using namespace d3d12;

struct D3d12Renderer::Impl
{
    struct FrameContext
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> back_buffer;
        ComPtr<ID3D12Resource> bone_buffer;
        std::byte* bone_buffer_mapped{};
        std::uint32_t bone_capacity{};
        ComPtr<ID3D12Resource> constant_buffer;
        std::byte* constant_buffer_mapped{};
        // Logical texture id -> descriptor slot for this frame (sandbox shaders' texture_residency).
        ComPtr<ID3D12Resource> residency_buffer;
        std::uint32_t* residency_mapped{};
        // One pick-target texel copied by this frame, read once its fence completes.
        ComPtr<ID3D12Resource> pick_readback;
        struct PickCopy
        {
            std::uint32_t request{};
            std::array<std::uint32_t, 2> pixel{};
        };
        std::vector<PickCopy> picks;
        // A frame copy requested by request_capture, read once this context's fence completes. The
        // buffer is created on request at the frame's size (captures are rare).
        ComPtr<ID3D12Resource> capture_readback;
        struct CaptureCopy
        {
            std::uint32_t width{};
            std::uint32_t height{};
            std::uint32_t row_pitch{};
            // Bytes the copy wrote: rows are pitch-aligned except the last.
            std::uint64_t bytes{};
        };
        std::optional<CaptureCopy> capture;
        // Debug line vertices written once this context is reusable.
        ComPtr<ID3D12Resource> debug_vertex_buffer;
        std::byte* debug_vertices_mapped{};
        // Begin/end GPU timestamps of this context's last frame, resolved for readback.
        ComPtr<ID3D12Resource> timestamp_readback;
        bool timestamps_pending{};
        // Instances submitted through set_instances, written once this context is reusable.
        ComPtr<ID3D12Resource> instance_buffer;
        std::byte* instance_buffer_mapped{};
        std::size_t instance_capacity{};
        // Particles submitted through set_particles, likewise.
        ComPtr<ID3D12Resource> particle_buffer;
        std::byte* particle_buffer_mapped{};
        std::size_t particle_capacity{};
        // Terrain decals and their grid (decal_grid_cells per side over the terrain: each cell's
        // first index and count, then the indices), always present so the root views are valid.
        ComPtr<ID3D12Resource> decal_buffer;
        std::byte* decal_buffer_mapped{};
        std::size_t decal_capacity{};
        ComPtr<ID3D12Resource> decal_grid_buffer;
        std::byte* decal_grid_buffer_mapped{};
        std::size_t decal_grid_capacity{};
        std::uint64_t fence_value{};
    };

    // Vertex layout of debug.hlsl.
    struct DebugVertex
    {
        Vector3 position;
        Color color;
    };

    // Layout of FrameConstants in sandbox.hlsl.
    struct FrameConstants
    {
        DirectX::XMFLOAT4X4 view_projection;
        std::array<float, 4> camera_position_time;
        std::array<float, 4> key_direction;
        std::array<float, 4> key_diffuse;
        std::array<float, 4> key_specular;
        std::array<float, 4> ambient;
        std::array<std::array<float, 4>, 2> fill_direction;
        std::array<std::array<float, 4>, 2> fill_diffuse;
        std::array<std::uint32_t, 4> selection{};
        // World-space camera basis for camera-facing particles.
        std::array<float, 4> camera_right{};
        std::array<float, 4> camera_up{};
        std::array<float, 4> post_parameters{};
        DirectX::XMFLOAT4X4 unjittered_view_projection;
        DirectX::XMFLOAT4X4 inverse_view_projection;
        DirectX::XMFLOAT4X4 previous_view_projection;
        std::array<float, 4> taa_parameters{};
        std::array<DirectX::XMFLOAT4X4, maximum_shadow_cascades> shadow_view_projection;
        std::array<float, 4> shadow_splits{};
        std::array<float, 4> shadow_texels{};
        std::array<float, 4> camera_forward{};
        std::array<std::uint32_t, 4> shadow_slots{};
        std::array<float, 4> shadow_parameters{};
        std::array<float, 4> fog_color{};
        std::array<float, 4> fog_range{};
        // Decal grid: x, y its corner, z, w cells per world unit; and cells per side, decal count.
        std::array<float, 4> decal_grid{};
        std::array<std::uint32_t, 4> decal_grid_size{};
        // Water: x enhanced (1) or legacy (0), y the depth buffer's texture slot, z the refraction
        // copy's, w refraction on; the plane's height and planar reflections on (1).
        std::array<std::uint32_t, 4> water_parameters{};
        std::array<float, 4> water_plane{};
        // Wind bend (render/wind_bend.hpp) now and last frame: xyz the offset, w 1 / height^2.
        std::array<float, 4> wind_bend{};
        std::array<float, 4> previous_wind_bend{};
        // Grass wind: xyz the direction, w the speed (VisualWind).
        std::array<float, 4> grass_wind{};
        // Cloud shadows: x 1 / scale, yz the drift so far in texture units; the texture's logical
        // id and whether they are on.
        std::array<float, 4> cloud_shadow{};
        std::array<std::uint32_t, 4> cloud_parameters{};
        // The sky capture: each cube face's matrix for the background draws (pass index
        // sky_face_pass + face), the atlas's texture slot, whether the frame has a sky, whether the
        // ambient takes the sky's light, and the face size.
        std::array<DirectX::XMFLOAT4X4, 6> sky_face_view_projection;
        std::array<std::uint32_t, 4> sky_parameters{};
        // Sky visibility: world X, Y to texture coordinates (scale x, y, offset x, y); the texture's
        // logical id and whether it is on.
        std::array<float, 4> sky_visibility_map{};
        std::array<std::uint32_t, 4> sky_visibility_parameters{};
        // Enhanced heat: the projection's vertical scale, the screen's width over height; the
        // signed buffer's slot and whether TAA weighs the new frame up where heat is.
        std::array<float, 4> heat_parameters{};
        std::array<std::uint32_t, 4> heat_slots{};
        // Particles: soft particles on.
        std::array<std::uint32_t, 4> particle_parameters{};
    };
    // Layout of TerrainDecal in terrain.hlsl (64 bytes).
    struct GpuDecal
    {
        Vector2 center{};
        Vector2 half_size{};
        Vector2 axis{};
        float opacity{};
        std::uint32_t mode{};
        std::array<float, 4> uv_rect{};
        std::uint32_t texture{};
        std::array<std::uint32_t, 3> padding{};
    };
    static_assert(sizeof(GpuDecal) == 64, "GpuDecal must match TerrainDecal in terrain.hlsl");
    static_assert(sizeof(FrameConstants) <= frame_constant_bytes);

    // Layout of MaterialData in sandbox.hlsl (96 bytes) and its flag bits.
    struct GpuMaterial
    {
        Color tint{white};
        Color diffuse{white};
        std::array<float, 4> specular{};
        std::array<float, 4> emissive{};
        std::array<float, 2> uv_scroll{};
        float alpha_cutoff{0.5F};
        std::uint32_t flags{};
        std::uint32_t base_texture{white_texture_slot};
        std::uint32_t normal_texture{flat_normal_texture_slot};
        std::uint32_t gloss_texture{white_texture_slot};
        std::uint32_t lighting{};
        // Extra layers, read by the lighting model. Planets (VisualPlanet): a the atmosphere rgb
        // and strength, b the city light rgb and atmosphere power, texture a the clouds, b their
        // normal map. Shields (VisualShield): a the base, distortion, and wave UV scales and the edge
        // brightness, b their scroll rates, texture a the wave, b the distortion.
        std::array<float, 4> layer_a{};
        std::array<float, 4> layer_b{};
        std::uint32_t layer_texture_a{white_texture_slot};
        std::uint32_t layer_texture_b{flat_normal_texture_slot};
        float cloud_scroll{};
        float bend_scale{};
        // Grass: its second color (VisualMaterial::grass_diffuse_alt).
        Color grass_diffuse_alt{white};
    };
    static_assert(sizeof(GpuMaterial) == 160);
    static constexpr std::uint32_t material_clouds = 1U << 5U;
    static constexpr std::uint32_t material_grass = 1U << 6U;
    static constexpr std::uint32_t material_cloud_shadow = 1U << 7U;
    static constexpr std::uint32_t material_alpha_test = 1U << 0U;
    static constexpr std::uint32_t material_alpha_blend = 1U << 1U;
    static constexpr std::uint32_t material_additive = 1U << 2U;
    static constexpr std::uint32_t material_team_color = 1U << 3U;
    static constexpr std::uint32_t material_emissive_mask = 1U << 4U;
    static constexpr std::uint32_t material_gloss_shift = 8U;

    struct UploadBatch
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        std::vector<ComPtr<ID3D12Resource>> resources;
        std::vector<D3D12_RESOURCE_BARRIER> transitions;
        std::uint64_t fence_value{};
    };

    // One draw of one mesh, issued once for every instance of that mesh. Instances are sorted by
    // mesh at load, so each batch reads a contiguous instance range.
    struct GpuDraw
    {
        VisualDraw geometry;
        std::uint32_t material{};
        VisualBlendMode blend{VisualBlendMode::opaque};
        std::uint32_t first_instance{};
        std::uint32_t instance_count{1};
        bool background{};
        // Geometry block holding the draw's vertices and indices: 0 is the loaded scene's, others
        // were added with add_meshes (added_blocks[block - 1]).
        std::uint32_t block{};
        // The map's water surface (VisualMaterial::water), drawn in the water pass.
        bool water{};
    };

    // Structured-buffer layout shared with the vertex shader (180 bytes, packed).
    struct GpuInstance
    {
        Matrix4 transform{Matrix4::zero()};
        Color tint{};
        Color team_color{};
        std::uint32_t bone_offset{};
        std::uint32_t flags{};
        // Scene instance index + 1; 0 for instances that are not pickable.
        std::uint32_t pick_id{};
        // Detail-level cross-fade: 0 drawn whole; t in (0, 1) the incoming level drawn on a
        // dither fraction t of the pixels; -t the outgoing level on the other pixels.
        float fade{};
        // Last frame's palette offset and transform (motion vectors); the current ones when the
        // instance is new or has no key.
        std::uint32_t previous_bone_offset{};
        Matrix4 previous_transform{Matrix4::identity()};
    };
    static_assert(sizeof(GpuInstance) == 180 && offsetof(GpuInstance, previous_transform) == 116, "GpuInstance must match InstanceData in sandbox.hlsl");

    // A mesh's draws by damage state and detail level ([state][level], most detailed first; one
    // state of one level for a mesh without either), the states' alternate numbers, and its
    // bounding sphere in mesh space for detail-level selection.
    struct MeshLevels
    {
        std::vector<std::uint32_t> alternates;
        std::vector<std::vector<std::vector<GpuDraw>>> draws;
        Vector3 center;
        float radius{};
    };

    static constexpr std::uint32_t background_instance_flag = 1U;
    static constexpr std::uint32_t highlighted_instance_flag = 2U;
    // No key-light shadows fall on it (VisualInstance::receives_shadows; INSTANCE_NO_SHADOWS).
    static constexpr std::uint32_t no_shadows_instance_flag = 4U;
    static constexpr std::uint32_t silhouette_instance_flag = 8U;

    HWND window{};
    D3d12RendererOptions options;
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t swap_chain_flags{};
    bool tearing_supported{};
    bool occluded{};
    std::uint64_t frame_number{};
    std::string adapter_name;
    VisualCamera camera;

    ComPtr<IDXGIFactory7> factory;
    ComPtr<IDXGIAdapter4> adapter;
    ComPtr<ID3D12Device> device;
    // Optional pipeline library. The serialized blob backs the library, so it is declared first
    // and therefore destroyed after it.
    std::vector<std::byte> pipeline_library_blob;
    ComPtr<ID3D12PipelineLibrary> pipeline_library;
    std::filesystem::path pipeline_cache_file;
    std::uint64_t pipeline_cache_key{};
    bool pipeline_library_loaded{};
    bool pipeline_library_dirty{};
    bool pipeline_library_stale{};
    std::uint32_t pipeline_cache_hits{};
    std::uint32_t pipeline_cache_misses{};
    std::string pipeline_cache_status{"disabled"};
    std::vector<std::pair<std::wstring, ComPtr<ID3D12PipelineState>>> created_pipelines;
#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue1> debug_info_queue;
    DWORD debug_callback_cookie{};
    bool debug_callback_registered{};
#endif
    CommandQueue direct_queue;
    CommandQueue copy_queue;
    ComPtr<IDXGISwapChain3> swap_chain;
    UniqueHandle frame_latency_event;

    std::array<FrameContext, frame_count> frames;
    ComPtr<ID3D12GraphicsCommandList> command_list;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    ComPtr<ID3D12DescriptorHeap> dsv_heap;
    ComPtr<ID3D12Resource> depth_buffer;
    ComPtr<ID3D12DescriptorHeap> pick_rtv_heap;
    ComPtr<ID3D12Resource> pick_target;
    // Requested pixel and selection state (see request_pick / set_highlight).
    struct PickRequest
    {
        std::uint32_t id{};
        std::uint32_t x{};
        std::uint32_t y{};
    };
    std::deque<PickRequest> pending_picks;
    std::uint32_t next_pick_request{1};
    std::deque<VisualPick> completed_picks;
    bool capture_requested{};
    std::deque<RgbaImage> completed_captures;
    std::uint32_t highlighted_pick_id{};
    std::vector<VisualDebugLine> debug_lines;
    ComPtr<ID3D12PipelineState> debug_pipeline;

    // Post-processing (bloom): the scene renders into scene_color, a bright pass fills
    // bloom_targets[0] at reduced size, blurs ping-pong between the two, and a composite writes the
    // back buffer. Their shader views sit in texture-table slots reserved at startup.
    ComPtr<ID3D12Resource> scene_color;
    // The opaque scene copied before the water pass (post slot 7), which enhanced water refracts.
    ComPtr<ID3D12Resource> refraction_color;
    // The sky capture (post slot 8): six cube faces of the background draws, three by two.
    ComPtr<ID3D12Resource> sky_atlas;
    // Occlusion silhouettes (post slot 9): the hidden parts of flagged units in their team color.
    ComPtr<ID3D12Resource> silhouette_mask;
    // Heat distortion (post slot 10): the bump colors heat particles blended in, 0.5 shifting nothing.
    ComPtr<ID3D12Resource> heat_buffer;
    // Enhanced heat distortion (post slot 11): signed shifts in texture units, added up (RG16F).
    ComPtr<ID3D12Resource> heat_signed_buffer;
    // The sky's light as nine spherical-harmonic colors (sky_light.hlsl).
    ComPtr<ID3D12Resource> sky_light;
    std::array<ComPtr<ID3D12Resource>, 2> bloom_targets;
    ComPtr<ID3D12DescriptorHeap> post_rtv_heap;
    // Texture slots (and RTVs, same index) of scene_color, the bloom targets, post_color, the two
    // TAA history targets, and the motion target.
    std::array<std::uint32_t, 12> post_slots{};
    // The depth buffer's shader view (temporal anti-aliasing reprojects by depth).
    std::uint32_t depth_slot{};
    // The pick target's integer view (occlusion silhouettes read which instance each pixel shows).
    std::uint32_t pick_slot{};
    std::uint32_t bloom_width{1};
    std::uint32_t bloom_height{1};
    ComPtr<ID3D12PipelineState> bloom_extract_pipeline;
    ComPtr<ID3D12PipelineState> bloom_blur_pipeline;
    ComPtr<ID3D12PipelineState> bloom_composite_pipeline;
    // Edge anti-aliasing: the composite goes to post_color, which the pass reads into the back
    // buffer (without bloom it reads scene_color).
    ComPtr<ID3D12Resource> post_color;
    ComPtr<ID3D12PipelineState> edge_aa_pipeline;
    // Temporal anti-aliasing: the history ping-pongs between two targets (index: the one written
    // last); it is invalid after a resize or a frame without TAA. previous_camera is the last
    // frame's unjittered view-projection (transposed, as in the constants).
    std::array<ComPtr<ID3D12Resource>, 2> taa_history;
    // Screen motion of the models drawn this frame (motion_format), written in the scene pass.
    ComPtr<ID3D12Resource> motion_target;
    // Motion vectors: last frame's palette (uploaded after the current one, so instances can
    // address it), each keyed instance's last transform and palette offset, and whether a last
    // camera exists.
    std::vector<Matrix4> previous_bone_transforms;
    struct PreviousInstance
    {
        Matrix4 transform{Matrix4::identity()};
        std::uint32_t bone_offset{};
    };
    std::unordered_map<std::uint64_t, PreviousInstance> previous_instances;
    std::unordered_map<std::uint64_t, PreviousInstance> current_instances;
    bool previous_camera_known{};
    ComPtr<ID3D12PipelineState> taa_pipeline;
    std::uint32_t taa_history_index{};
    bool taa_history_valid{};
    std::uint64_t taa_frame{};
    DirectX::XMFLOAT4X4 previous_camera{};
    // Creates (or, after a resize, recreates) the post-processing targets and their views.
    void create_post_targets();
    // The shadow cascades' maps (created once, the first time shadows are on).
    void create_shadow_maps();
    // Records the shadow pass: every cascade's map from the casters of this frame's draws.
    void record_shadow_cascades(FrameContext& frame, bool dynamic);
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE post_rtv(std::size_t index) const;

    // Structured-buffer layout of ParticleData in particle.hlsl (80 bytes).
    struct GpuParticle
    {
        Vector3 position{};
        float half_size{};
        Color color{};
        std::array<float, 4> uv_rect{};
        Vector3 axis{};
        float rotation{};
        float tail{};
        // Logical texture id, or invalid_visual_texture for white.
        std::uint32_t texture{};
        std::uint32_t shape{};
        // Bit 0: monochrome; bit 1: added to the frame (fog fades it rather than tinting it).
        std::uint32_t flags{};
    };
    static_assert(sizeof(GpuParticle) == 80);
    std::vector<VisualParticle> particles;
    std::vector<GpuParticle> gpu_particles;
    // Pipelines by blend (VisualParticleBlend) and depth test (0 tested, 1 not).
    std::array<std::array<ComPtr<ID3D12PipelineState>, 2>, 4> particle_pipelines;
    std::uint64_t last_particles{};

    // Debug overlay: the caller's Dear ImGui panels (overlay_ui) and the selection rectangle, drawn
    // last into the back buffer. Panel layouts persist in overlay_settings_file (UTF-8, kept alive
    // for Dear ImGui).
    ImGuiContext* overlay_context{};
    bool overlay_backend_initialized{};
    std::string overlay_settings_file;
    std::chrono::steady_clock::time_point overlay_tick{std::chrono::steady_clock::now()};
    VisualOverlayInput overlay_input;
    // Dear ImGui's textures (its font atlas), by ImTextureData: uploaded through the renderer's own
    // upload path (copy queue, a GPU wait on the direct queue, slots and resources retired by fence
    // value) instead of the backend's, which signals a fence of its own on the direct queue and
    // waits for it on the CPU. Their BackendUserData stays null, so the backend never frees them.
    struct OverlayTexture
    {
        ComPtr<ID3D12Resource> resource;
        std::uint32_t descriptor{};
    };
    std::unordered_map<const ImTextureData*, OverlayTexture> overlay_textures;
    bool overlay_wants_mouse{};
    bool overlay_wants_keyboard{};
    std::function<void()> overlay_ui;
    std::optional<std::array<float, 4>> selection_rectangle;

    // Frame statistics of the last recorded frame and of the last frame the GPU finished.
    ComPtr<ID3D12QueryHeap> timestamp_heap;
    std::uint64_t timestamp_frequency{};
    double last_gpu_milliseconds{};
    std::uint32_t last_draw_calls{};
    std::uint64_t last_triangles{};
    std::uint64_t last_instances{};
    std::uint64_t video_memory_usage{};
    std::uint64_t video_memory_budget{};
    std::uint32_t rtv_increment{};

    ComPtr<ID3D12RootSignature> root_signature;
    ComPtr<ID3D12PipelineState> opaque_pipeline;
    ComPtr<ID3D12PipelineState> alpha_pipeline;
    // Enhanced water: the alpha pipeline with the water pixel shader.
    ComPtr<ID3D12PipelineState> water_pipeline;
    // Planar reflections of the water (water_reflections.hlsl): a source per screen pixel, cleared
    // and filled by two compute shaders with their own root signature.
    ComPtr<ID3D12RootSignature> water_reflection_root_signature;
    ComPtr<ID3D12PipelineState> water_reflection_clear_pipeline;
    ComPtr<ID3D12PipelineState> water_reflection_project_pipeline;
    // The background pipelines drawing into the sky atlas (one target, no depth, both faces), and
    // the sky light's projection (the water reflections' root signature).
    ComPtr<ID3D12PipelineState> sky_capture_opaque_pipeline;
    ComPtr<ID3D12PipelineState> sky_capture_alpha_pipeline;
    ComPtr<ID3D12PipelineState> sky_capture_additive_pipeline;
    ComPtr<ID3D12PipelineState> sky_light_pipeline;
    // Occlusion silhouettes: the hidden parts into the mask (depth greater, max blend), then the mask
    // over the scene.
    ComPtr<ID3D12PipelineState> silhouette_pipeline;
    ComPtr<ID3D12PipelineState> silhouette_composite_pipeline;
    // Heat distortion: heat particles into the heat buffer, then the scene shifted by it.
    ComPtr<ID3D12PipelineState> heat_particle_pipeline;
    ComPtr<ID3D12PipelineState> heat_apply_pipeline;
    ComPtr<ID3D12PipelineState> heat_particle_enhanced_pipeline;
    ComPtr<ID3D12PipelineState> heat_apply_enhanced_pipeline;
    ComPtr<ID3D12Resource> water_reflection_sources;
    // The loaded scene's water plane height (from its water mesh), if it has one.
    std::optional<float> water_level;
    // The loaded terrain's sky visibility texture and its world-to-texture mapping, if any.
    std::optional<std::uint32_t> sky_visibility_texture;
    std::array<float, 4> sky_visibility_map{};
    // Last frame's wind bend (motion vectors of swaying trees); none before the first frame.
    std::optional<std::array<float, 4>> previous_wind_bend;
    ComPtr<ID3D12PipelineState> additive_pipeline;
    ComPtr<ID3D12PipelineState> background_opaque_pipeline;
    ComPtr<ID3D12PipelineState> terrain_pipeline;
    // Shadow casting (depth only): models, alpha-tested models, and the terrain.
    ComPtr<ID3D12PipelineState> shadow_pipeline;
    ComPtr<ID3D12PipelineState> shadow_alpha_test_pipeline;
    ComPtr<ID3D12PipelineState> terrain_shadow_pipeline;
    // The cascades' depth maps (typeless, read through float views at shadow_slots), their DSVs,
    // the loaded scene's bounds (the cascades' depth range), and this frame's cascades.
    std::array<ComPtr<ID3D12Resource>, maximum_shadow_cascades> shadow_maps;
    std::array<std::uint32_t, maximum_shadow_cascades> shadow_slots{};
    ComPtr<ID3D12DescriptorHeap> shadow_dsv_heap;
    std::uint32_t shadow_dsv_increment{};
    std::uint32_t shadow_resolution{};
    Vector3 scene_bounds_minimum{};
    Vector3 scene_bounds_maximum{};
    // The terrain's extent in X/Y (decals lie on it; empty without terrain).
    Vector2 terrain_minimum{};
    Vector2 terrain_maximum{};
    std::vector<VisualDecal> decals;
    std::vector<GpuDecal> gpu_decals;
    std::vector<std::uint32_t> decal_grid;
    std::uint32_t decal_count{};
    float fog_floor{};
    ShadowCascades frame_cascades;
    ComPtr<ID3D12Resource> terrain_vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW terrain_vertex_view{};
    std::uint32_t terrain_vertex_count{};
    std::uint32_t terrain_first_material{};
    // True once load_scene succeeded; before that the validation triangle is drawn.
    bool scene_loaded{};
    ComPtr<ID3D12PipelineState> background_alpha_pipeline;
    ComPtr<ID3D12PipelineState> background_additive_pipeline;
    ComPtr<ID3D12DescriptorHeap> shader_heap;
    ComPtr<ID3D12Resource> vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view{};
    ComPtr<ID3D12Resource> texture;
    // The validation fallback keeps its own identity instance; its pending COMMON-to-SRV transition
    // holds a raw resource pointer, so it must outlive any scene that replaces the scene buffer.
    ComPtr<ID3D12Resource> identity_instance_buffer;
    ComPtr<ID3D12Resource> scene_instance_buffer;
    ComPtr<ID3D12Resource> scene_vertex_buffer;
    ComPtr<ID3D12Resource> scene_index_buffer;
    D3D12_VERTEX_BUFFER_VIEW scene_vertex_view{};
    D3D12_INDEX_BUFFER_VIEW scene_index_view{};
    // Geometry of meshes added after load_scene, one block per add_meshes call.
    struct GeometryBlock
    {
        ComPtr<ID3D12Resource> vertex_buffer;
        ComPtr<ID3D12Resource> index_buffer;
        D3D12_VERTEX_BUFFER_VIEW vertex_view{};
        D3D12_INDEX_BUFFER_VIEW index_view{};
    };
    std::vector<GeometryBlock> added_blocks;
    // Scene textures are pool entries owning their resource and shader-table slot; the scene keeps
    // generation-checked handles. Replaced resources retire on the direct queue's timeline.
    struct GpuTexture
    {
        ComPtr<ID3D12Resource> resource;
        std::uint32_t descriptor{};
    };
    struct TextureTag
    {};
    using TextureHandle = ResourceHandle<TextureTag>;
    HandlePool<TextureTag, GpuTexture> textures;
    DescriptorSlotAllocator texture_slots{fallback_texture_count, maximum_scene_textures - fallback_texture_count};
    FenceRetirementQueue<std::vector<ComPtr<ID3D12Resource>>> retired_resources;
    // Materials name logical textures: 0..fallback_texture_count-1 are the fallbacks, then one per
    // scene texture. Scene textures stream in under the per-frame upload budget; until a texture is
    // resident its logical id maps to a role-appropriate fallback in the frame's residency table.
    struct StreamingTexture
    {
        VisualTexture source;
        TextureHandle handle;
        std::uint32_t fallback_slot{white_texture_slot};
        std::uint64_t bytes{};
        std::string identity;
    };
    std::vector<StreamingTexture> scene_textures;
    // Scan position of the streaming queue; entries before it are resident or reused.
    std::size_t next_streaming_texture{};
    std::size_t pending_texture_count{};
    std::size_t last_reused_textures{};
    std::vector<GpuDraw> scene_draws;
    // Mesh library of the loaded scene: each mesh's draws (instance range unset) and bone count.
    std::vector<std::vector<GpuDraw>> scene_mesh_draws;
    std::vector<std::uint32_t> scene_mesh_bone_counts;
    // Parallel to scene_mesh_draws (whose entries are each mesh's default range).
    std::vector<MeshLevels> scene_mesh_levels;
    VisualLodSettings lod_settings;
    VisualLodTracker lod_tracker;
    std::uint64_t reduced_detail_instances{};
    std::uint64_t fading_instances{};
    std::uint64_t fades_started{};
    std::uint32_t scene_bone_capacity{1};
    // Runtime instance list (set_instances); when present it replaces the scene's instances.
    std::optional<std::vector<VisualInstance>> dynamic_instances;
    std::vector<GpuInstance> dynamic_gpu_instances;
    std::vector<GpuDraw> dynamic_draws;
    std::vector<Matrix4> bone_transforms;
    ComPtr<ID3D12Resource> fallback_white_texture;
    ComPtr<ID3D12Resource> fallback_normal_texture;
    // The validation fallback keeps its own material; like the identity instance it must outlive
    // any scene because its pending state transition holds a raw resource pointer.
    ComPtr<ID3D12Resource> identity_material_buffer;
    ComPtr<ID3D12Resource> scene_material_buffer;
    // CPU copy of the material buffer's contents, so add_meshes can append to it.
    std::vector<GpuMaterial> scene_gpu_materials;
    VisualLightRig lighting;
    float scene_time{};
    std::vector<UploadBatch> pending_uploads;
    // Transitions of uploaded resources out of COMMON, recorded by the next render(). They hold raw
    // resource pointers: retire_uploaded drops a retired resource's transition, so a signal between
    // the upload and that render (a resize flush, wait_idle) cannot free a resource a barrier names.
    std::vector<D3D12_RESOURCE_BARRIER> pending_transitions;
    // The frame's passes, rebuilt each frame (storage kept): the graph, its resources by graph id,
    // and the barriers of one batch.
    FrameGraph frame_graph;
    std::vector<ID3D12Resource*> graph_resources;
    std::vector<D3D12_RESOURCE_BARRIER> graph_barriers;

    // Construction only records the parameters; initialize() creates the device and resources.
    Impl(void* native_window, const std::uint32_t initial_width, const std::uint32_t initial_height, const D3d12RendererOptions& renderer_options);

    ~Impl();

    // Startup. Failures that depend on the machine (debug layer, adapter, device, queues, swap
    // chain, required features, overlay backend) are returned; a failed resource creation once the
    // device and its required features exist is treated like any later D3D12 failure (fatal).
    [[nodiscard]] Status initialize();

    void create_statistics_queries();

    // The Dear ImGui DX12 backend draws the overlay; its textures are ours (overlay_textures). What
    // remains of the backend's own practice, a documented exception for this debug overlay: it
    // compiles its two shaders at startup (D3DCompile) and reuses its vertex and index buffers by
    // frame index, which holds because the overlay is drawn once per frame with frame_count frames
    // in flight.
    [[nodiscard]] Status create_overlay();

    void allocate_overlay_descriptor(D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu);

    void free_overlay_descriptor(const D3D12_CPU_DESCRIPTOR_HANDLE cpu);

    void shutdown_overlay() noexcept;

    // Builds the overlay's frame (input, panels, draw data) and uploads its texture changes, before
    // the frame records its pending transitions, so new textures are in their shader state by the
    // time the overlay draws.
    void prepare_overlay();

    // Uploads, replaces, or retires the overlay textures Dear ImGui asks for, then takes the
    // requests away from the backend (ImDrawData::Textures).
    void update_overlay_textures();

    // Draws the prepared overlay into the current back buffer (call after every scene pass).
    void render_overlay(const D3D12_CPU_DESCRIPTOR_HANDLE render_target);

    // Retires an overlay texture's resource and slot once the direct queue's next signal completes.
    void retire_overlay_texture(OverlayTexture texture);

    // The shader-visible GPU handle of a texture-table slot.
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE texture_slot_gpu_handle(std::uint32_t slot) const;

    // Reads the GPU time of the frame this context recorded last; its fence has completed.
    void collect_timestamps(FrameContext& frame);

    [[nodiscard]] Status create_swap_chain();

    void create_frame_resources();

    void acquire_back_buffers();

    void create_depth_buffer();

    void create_constant_buffers();

    // Resets the palette to `requested_count` identity matrices (frame buffers grow at render).
    void create_bone_buffers(const std::uint32_t requested_count);
    // Grows a reusable frame context's palette buffer to hold `count` matrices.
    void ensure_bone_capacity(FrameContext& frame, std::size_t count);

    // --- Pipeline library cache -----------------------------------------------------------------
    // File layout: "EAWPSO01", u64 key (shader bytecode + layout version), u64 payload size,
    // payload (ID3D12PipelineLibrary::Serialize output). Any mismatch or driver rejection discards
    // the cache; failures never stop rendering.
    static constexpr std::array<char, 8> pipeline_cache_magic{'E', 'A', 'W', 'P', 'S', 'O', '0', '1'};
    static constexpr std::uint64_t pipeline_layout_version = 10;
    static constexpr std::uint64_t maximum_pipeline_cache_bytes = 256ULL * 1024ULL * 1024ULL;

    static std::uint64_t fnv1a(std::uint64_t hash, const std::span<const std::byte> bytes) noexcept;

    void open_pipeline_cache();

    void create_pipeline_state(const wchar_t* name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& description, ComPtr<ID3D12PipelineState>& pipeline);

    void save_pipeline_cache();

    // Serializes the pipeline library to the cache file (through a temporary and a rename).
    // Failures leave rendering unaffected; save_pipeline_cache records them in the status.
    [[nodiscard]] Status write_pipeline_cache() const;

    // Features the offline-compiled shaders and the texture table require (checked at startup).
    [[nodiscard]] Status check_required_features() const;

    void create_pipeline();

    void create_initial_assets();

    UploadBatch begin_upload(const std::wstring_view name);

    void submit_upload(UploadBatch upload);
    // Retires resources that may have been uploaded since the last render() at `fence_value`,
    // dropping their pending transitions first (they are never drawn again).
    void retire_uploaded(std::vector<ComPtr<ID3D12Resource>> resources, std::uint64_t fence_value);

    ComPtr<ID3D12Resource> upload_buffer(UploadBatch& upload, const std::span<const std::byte> bytes, const D3D12_RESOURCE_STATES final_state, const std::wstring_view name);

    // Checks a texture's format, mip chain, and data sizes against its D3D12 copy footprint so a
    // streamed upload cannot fail on malformed data after the scene was accepted.
    [[nodiscard]] Result<D3D12_RESOURCE_DESC> texture_description(const VisualTexture& source) const;

    ComPtr<ID3D12Resource> upload_texture(UploadBatch& upload, const VisualTexture& source, const std::uint32_t descriptor_index);

    // Validates the scene before anything of the current scene changes, so a failed load leaves
    // the current scene, its palette, and its textures untouched. Resources created for a rejected
    // scene are released with the unsubmitted upload batch when this returns.
    [[nodiscard]] Status load_scene(VisualScene scene);

    // Second half of load_scene: validates terrain and draws, submits the upload, and swaps the new
    // scene in. The replaced scene's resources retire once the next direct-queue signal completes,
    // which covers in-flight frames and the pending transitions that still name them.
    [[nodiscard]] Status commit_scene(VisualScene& scene, UploadBatch upload, std::vector<GpuMaterial> gpu_materials, std::vector<StreamingTexture> new_textures,
                                      std::vector<std::pair<std::uint32_t, std::uint32_t>> mesh_ranges, ComPtr<ID3D12Resource> new_vertex_buffer, ComPtr<ID3D12Resource> new_index_buffer,
                                      ComPtr<ID3D12Resource> new_instance_buffer, const std::size_t vertex_byte_count, const std::size_t index_byte_count);

    // Removes textures from the pool and retires their resources and descriptor slots on the
    // direct queue's next signal. Entries that never became resident are dropped; the vector is cleared.
    void retire_textures(std::vector<StreamingTexture>& entries);

    // Uploads queued scene textures in order until the per-frame byte budget is spent (always at
    // least one, so a texture larger than the budget still arrives). A texture counts as resident
    // from the frame that submits it: the direct queue waits for the copy and applies its
    // transition before that frame's draws. When the shader table is full the rest wait on their
    // fallbacks and retry next frame.
    void stream_scene_textures();

    // Reads the picks this frame context copied earlier; its fence has completed when this runs.
    void collect_pick(FrameContext& frame);
    void collect_capture(FrameContext& frame);
    // Records the copy of the back buffer (in the render-target state) for a requested capture.
    void record_capture(FrameContext& frame);

    std::uint32_t request_pick(const std::uint32_t x, const std::uint32_t y);

    void set_debug_lines(const std::span<const VisualDebugLine> lines);

    void set_particles(const std::span<const VisualParticle> next);
    void set_decals(const std::span<const VisualDecal> next)
    {
        decals.assign(next.begin(), next.end());
    }
    // Writes this frame's decals and their grid over the terrain (write_decals).
    void write_decals(FrameContext& frame);
    // Grows a persistently mapped upload buffer of `frame`'s to at least `bytes` (retiring the old
    // one on the direct timeline).
    void ensure_upload_buffer(ComPtr<ID3D12Resource>& buffer, std::byte*& mapped, std::size_t& capacity, std::size_t bytes, const wchar_t* name);

    // Writes the particles into this frame context's buffer, growing it when needed (the outgrown
    // buffer retires on the direct timeline).
    void write_particles(FrameContext& frame);

    // Texture registration shared by add_meshes and add_textures, in two phases so a failed
    // validation commits nothing: plan_textures maps each texture to a logical id (an identity
    // already present shares its id) and lists the new ones; commit_textures queues those for
    // streaming.
    struct TexturePlan
    {
        std::vector<std::uint32_t> logical_ids;
        std::vector<std::size_t> new_sources;
    };
    [[nodiscard]] Result<TexturePlan> plan_textures(std::span<const VisualTexture> textures) const;
    void commit_textures(std::vector<VisualTexture>& textures, const TexturePlan& plan);
    [[nodiscard]] Result<std::vector<std::uint32_t>> add_textures(std::vector<VisualTexture> added);

    void set_instances(const std::span<const VisualInstance> instances);

    // Groups instances by mesh and layer into contiguous ranges and builds the draws, ordered like a
    // loaded scene: backgrounds first, then the world by blend mode. Pick ids are list index + 1.
    void batch_instances(const std::vector<VisualInstance>& instances, std::vector<GpuInstance>& gpu_instances, std::vector<GpuDraw>& draws);
    // A mesh's draws by state and level (its default range alone when it lists no states), with
    // `gpu_draw(index)` turning a scene draw into a GPU draw, and its bounding sphere.
    template <class GpuDrawOf> static MeshLevels mesh_levels(const VisualMesh& mesh, const GpuDrawOf& gpu_draw);

    // Writes the dynamic instances into this frame context's buffer, growing it when needed (the
    // outgrown buffer retires on the direct timeline).
    void write_dynamic_instances(FrameContext& frame);

    void set_selection_rectangle(const std::optional<std::array<float, 4>>& rectangle);

    void set_overlay_input(const VisualOverlayInput& input);

    void set_highlight(const std::optional<std::uint32_t> instance);

    // Writes this frame's logical-texture-to-slot table. Called once the frame context is reusable.
    void write_texture_residency(FrameContext& frame) const;

    // Returns completed texture slots to the allocator and points them at a null view so the shader
    // table never names a released resource.
    void reclaim_texture_slots();

    void set_bone_transforms(const std::span<const Matrix4> transforms);

    // Validates one scene material and converts it to the shader layout.
    // `texture_ids` maps the scene's texture indices to logical texture ids (empty: fallback count +
    // index, as load_scene assigns them).
    static Result<GpuMaterial> gpu_material(const VisualScene& scene, const std::size_t index, std::span<const std::uint32_t> texture_ids = {});
    [[nodiscard]] Result<std::uint32_t> add_meshes(VisualScene fragment);

    void set_lighting(const VisualLightRig& next);

    void set_scene_time(const float seconds);

    void set_camera(const VisualCamera& next);

    // The camera's view-projection, unjittered and not transposed.
    [[nodiscard]] DirectX::XMMATRIX camera_view_projection() const;

    bool render();

    bool wait_for_frame_latency();

    void resize(const std::uint32_t new_width, const std::uint32_t new_height);

    // Waits for both queues; a failed wait is fatal (the device is lost).
    void wait_idle();

    // Destructor variant of wait_idle: never fatal. When a wait fails, resources are not retired
    // through the normal path; they are released with the renderer after the device is lost.
    void wait_idle_for_shutdown() noexcept;

    void retire_completed_after_idle();

    void retire_completed_resources();

    void retire_completed_uploads();
};

} // namespace eaw
