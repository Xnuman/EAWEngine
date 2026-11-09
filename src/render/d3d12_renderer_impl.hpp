#pragma once

// D3d12Renderer's implementation: every Direct3D 12 object and the state the public renderer methods
// forward to. Member functions are defined by subject in d3d12_renderer_device.cpp,
// d3d12_renderer_pipelines.cpp, d3d12_renderer_resources.cpp, d3d12_renderer_frame.cpp, and
// d3d12_renderer_overlay.cpp. Internal to src/render.

#include "d3d12_support.hpp"

#include "eaw/d3d12_renderer.hpp"

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
        std::uint64_t fence_value{};
    };

    // Vertex layout of debug.hlsl.
    struct DebugVertex
    {
        Vector3 position;
        std::array<float, 4> color;
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
    };
    static_assert(sizeof(FrameConstants) <= frame_constant_bytes);

    // Layout of MaterialData in sandbox.hlsl (96 bytes) and its flag bits.
    struct GpuMaterial
    {
        std::array<float, 4> tint{1.0F, 1.0F, 1.0F, 1.0F};
        std::array<float, 4> diffuse{1.0F, 1.0F, 1.0F, 1.0F};
        std::array<float, 4> specular{};
        std::array<float, 4> emissive{};
        std::array<float, 2> uv_scroll{};
        float alpha_cutoff{0.5F};
        std::uint32_t flags{};
        std::uint32_t base_texture{white_texture_slot};
        std::uint32_t normal_texture{flat_normal_texture_slot};
        std::uint32_t gloss_texture{white_texture_slot};
        std::uint32_t lighting{};
    };
    static_assert(sizeof(GpuMaterial) == 96);
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
    };

    // Structured-buffer layout shared with the vertex shader (112 bytes).
    struct GpuInstance
    {
        Matrix4 transform{Matrix4::zero()};
        std::array<float, 4> tint{};
        std::array<float, 4> team_color{};
        std::uint32_t bone_offset{};
        std::uint32_t flags{};
        // Scene instance index + 1; 0 for instances that are not pickable.
        std::uint32_t pick_id{};
        std::uint32_t padding{};
    };

    static constexpr std::uint32_t background_instance_flag = 1U;
    static constexpr std::uint32_t highlighted_instance_flag = 2U;

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
    std::array<ComPtr<ID3D12Resource>, 2> bloom_targets;
    ComPtr<ID3D12DescriptorHeap> post_rtv_heap;
    std::array<std::uint32_t, 3> post_slots{};
    std::uint32_t bloom_width{1};
    std::uint32_t bloom_height{1};
    ComPtr<ID3D12PipelineState> bloom_extract_pipeline;
    ComPtr<ID3D12PipelineState> bloom_blur_pipeline;
    ComPtr<ID3D12PipelineState> bloom_composite_pipeline;
    // Creates (or, after a resize, recreates) the post-processing targets and their views.
    void create_post_targets();
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE post_rtv(std::size_t index) const;

    // Structured-buffer layout of ParticleData in particle.hlsl (80 bytes).
    struct GpuParticle
    {
        Vector3 position{};
        float half_size{};
        std::array<float, 4> color{};
        std::array<float, 4> uv_rect{};
        Vector3 axis{};
        float rotation{};
        float tail{};
        // Logical texture id, or invalid_visual_texture for white.
        std::uint32_t texture{};
        std::uint32_t shape{};
        // Bit 0: monochrome.
        std::uint32_t flags{};
    };
    static_assert(sizeof(GpuParticle) == 80);
    std::vector<VisualParticle> particles;
    std::vector<GpuParticle> gpu_particles;
    // Pipelines by blend (VisualParticleBlend) and depth test (0 tested, 1 not).
    std::array<std::array<ComPtr<ID3D12PipelineState>, 2>, 4> particle_pipelines;
    std::uint64_t last_particles{};

    // Debug overlay: a display-only Dear ImGui panel drawn last into the back buffer. Its font
    // texture takes slots from texture_slots through the backend's allocation callbacks.
    ImGuiContext* overlay_context{};
    bool overlay_backend_initialized{};
    std::vector<std::string> overlay_lines;
    std::chrono::steady_clock::time_point overlay_tick{std::chrono::steady_clock::now()};
    VisualOverlayInput overlay_input;
    ImVec2 overlay_position{10.0F, 10.0F};
    bool overlay_minimized{};
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
    ComPtr<ID3D12PipelineState> additive_pipeline;
    ComPtr<ID3D12PipelineState> background_opaque_pipeline;
    ComPtr<ID3D12PipelineState> terrain_pipeline;
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
    std::vector<D3D12_RESOURCE_BARRIER> pending_transitions;

    // Construction only records the parameters; initialize() creates the device and resources.
    Impl(void* native_window, const std::uint32_t initial_width, const std::uint32_t initial_height, const D3d12RendererOptions& renderer_options);

    ~Impl();

    // Startup. Failures that depend on the machine (debug layer, adapter, device, queues, swap
    // chain, required features, overlay backend) are returned; a failed resource creation once the
    // device and its required features exist is treated like any later D3D12 failure (fatal).
    [[nodiscard]] Status initialize();

    void create_statistics_queries();

    // The ImGui DX12 backend uploads its font texture on the direct queue and waits for that upload
    // on the CPU. This happens only when the font atlas changes (startup, first use of new glyphs),
    // never per frame; it is a documented exception to the no-CPU-wait rule for this debug overlay.
    [[nodiscard]] Status create_overlay();

    void allocate_overlay_descriptor(D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu);

    void free_overlay_descriptor(const D3D12_CPU_DESCRIPTOR_HANDLE cpu);

    void shutdown_overlay() noexcept;

    // Draws the overlay panel into the current back buffer (call after every scene pass).
    void render_overlay(const D3D12_CPU_DESCRIPTOR_HANDLE render_target);

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
    static constexpr std::uint64_t pipeline_layout_version = 2;
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
    void batch_instances(const std::vector<VisualInstance>& instances, std::vector<GpuInstance>& gpu_instances, std::vector<GpuDraw>& draws) const;

    // Writes the dynamic instances into this frame context's buffer, growing it when needed (the
    // outgrown buffer retires on the direct timeline).
    void write_dynamic_instances(FrameContext& frame);

    void set_overlay_text(const std::span<const std::string> lines);

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

    [[nodiscard]] DirectX::XMFLOAT4X4 camera_matrix() const;

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

    void check_device_result(const HRESULT result, const std::string_view operation) const;

    [[nodiscard]] std::string device_removed_diagnostics() const;
};

} // namespace eaw
