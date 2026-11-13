#include "d3d12_renderer_impl.hpp"

namespace eaw {

D3d12Renderer::D3d12Renderer(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl))
{}

Result<std::unique_ptr<D3d12Renderer>> D3d12Renderer::create(void* native_window, const std::uint32_t width, const std::uint32_t height, const D3d12RendererOptions& options)
{
    auto impl = std::make_unique<Impl>(native_window, width, height, options);
    if (auto status = impl->initialize(); !status)
    {
        // ~Impl waits for whatever was submitted before the failure (best effort).
        return std::move(status).error();
    }
    return std::unique_ptr<D3d12Renderer>(new D3d12Renderer(std::move(impl)));
}

D3d12Renderer::~D3d12Renderer() = default;
D3d12Renderer::D3d12Renderer(D3d12Renderer&&) noexcept = default;
D3d12Renderer& D3d12Renderer::operator=(D3d12Renderer&&) noexcept = default;

bool D3d12Renderer::render()
{
    return impl_->render();
}

void D3d12Renderer::resize(const std::uint32_t width, const std::uint32_t height)
{
    impl_->resize(width, height);
}

void D3d12Renderer::wait_idle()
{
    impl_->wait_idle();
}

std::uint32_t D3d12Renderer::request_pick(const std::uint32_t x, const std::uint32_t y)
{
    return impl_->request_pick(x, y);
}

std::optional<VisualPick> D3d12Renderer::take_pick()
{
    if (impl_->completed_picks.empty())
    {
        return std::nullopt;
    }
    auto result = impl_->completed_picks.front();
    impl_->completed_picks.pop_front();
    return result;
}

void D3d12Renderer::request_capture()
{
    impl_->capture_requested = true;
}

std::optional<RgbaImage> D3d12Renderer::take_capture()
{
    if (impl_->completed_captures.empty())
    {
        return std::nullopt;
    }
    auto result = std::move(impl_->completed_captures.front());
    impl_->completed_captures.pop_front();
    return result;
}

void D3d12Renderer::set_highlight(const std::optional<std::uint32_t> instance)
{
    impl_->set_highlight(instance);
}

void D3d12Renderer::set_debug_lines(const std::span<const VisualDebugLine> lines)
{
    impl_->set_debug_lines(lines);
}

void D3d12Renderer::set_instances(const std::span<const VisualInstance> instances)
{
    impl_->set_instances(instances);
}

void D3d12Renderer::set_selection_rectangle(const std::optional<std::array<float, 4>>& rectangle)
{
    impl_->set_selection_rectangle(rectangle);
}

void D3d12Renderer::set_overlay_input(const VisualOverlayInput& input)
{
    impl_->set_overlay_input(input);
}

bool D3d12Renderer::overlay_wants_mouse() const noexcept
{
    return impl_->overlay_wants_mouse;
}

bool D3d12Renderer::overlay_wants_keyboard() const noexcept
{
    return impl_->overlay_wants_keyboard;
}

void D3d12Renderer::set_overlay_ui(std::function<void()> build)
{
    impl_->overlay_ui = std::move(build);
}

std::size_t D3d12Renderer::max_debug_lines() noexcept
{
    return maximum_debug_lines;
}

std::size_t D3d12Renderer::max_bone_transforms() noexcept
{
    return maximum_bone_transforms;
}

Status D3d12Renderer::load_scene(VisualScene scene)
{
    return impl_->load_scene(std::move(scene));
}

Result<std::vector<std::uint32_t>> D3d12Renderer::add_textures(std::vector<VisualTexture> textures)
{
    if (!impl_->scene_loaded)
    {
        return make_error("add_textures needs a loaded scene");
    }
    return impl_->add_textures(std::move(textures));
}

void D3d12Renderer::set_particles(const std::span<const VisualParticle> particles)
{
    impl_->set_particles(particles);
}

void D3d12Renderer::set_decals(const std::span<const VisualDecal> decals)
{
    impl_->set_decals(decals);
}

Result<std::uint32_t> D3d12Renderer::add_meshes(VisualScene meshes)
{
    return impl_->add_meshes(std::move(meshes));
}

void D3d12Renderer::set_bone_transforms(const std::span<const Matrix4> transforms)
{
    impl_->set_bone_transforms(transforms);
}

void D3d12Renderer::set_camera(const VisualCamera& camera)
{
    impl_->set_camera(camera);
}

void D3d12Renderer::set_lighting(const VisualLightRig& lighting)
{
    impl_->set_lighting(lighting);
}

void D3d12Renderer::set_scene_time(const float seconds)
{
    impl_->set_scene_time(seconds);
}

void D3d12Renderer::set_lod_settings(const VisualLodSettings& settings)
{
    impl_->lod_settings = settings;
}

void D3d12Renderer::set_graphics_settings(const D3d12GraphicsSettings& settings)
{
    impl_->options.graphics = settings;
    // Called between frames, so nothing in flight uses the maps being created.
    if (settings.shadows && impl_->shadow_resolution == 0)
    {
        impl_->create_shadow_maps();
    }
}

const VisualLodSettings& D3d12Renderer::lod_settings() const noexcept
{
    return impl_->lod_settings;
}

const D3d12GraphicsSettings& D3d12Renderer::graphics_settings() const noexcept
{
    return impl_->options.graphics;
}

const std::string& D3d12Renderer::adapter_name() const
{
    return impl_->adapter_name;
}

D3d12RendererStats D3d12Renderer::stats() const
{
    return {
        .frame_number = impl_->frame_number,
        .direct_fence_completed = impl_->direct_queue.completed_value(),
        .copy_fence_completed = impl_->copy_queue.completed_value(),
        .pipeline_cache_hits = impl_->pipeline_cache_hits,
        .pipeline_cache_misses = impl_->pipeline_cache_misses,
        .pipeline_cache_status = impl_->pipeline_cache_status,
        .live_textures = impl_->textures.size(),
        .free_texture_slots = impl_->texture_slots.free_count(),
        .pending_texture_slots = impl_->texture_slots.pending_count(),
        .pending_retirements = impl_->retired_resources.size(),
        .pending_texture_uploads = impl_->pending_texture_count,
        .reused_textures = impl_->last_reused_textures,
        .dropped_debug_lines = impl_->debug_lines.size() - std::min(impl_->debug_lines.size(), maximum_debug_lines),
        .bone_transforms = impl_->bone_transforms.size(),
        .draw_calls = impl_->last_draw_calls,
        .triangles = impl_->last_triangles,
        .instances = impl_->last_instances,
        .particles = impl_->last_particles,
        .reduced_detail_instances = impl_->reduced_detail_instances,
        .fading_instances = impl_->fading_instances,
        .fades_started = impl_->fades_started,
        .gpu_frame_milliseconds = impl_->last_gpu_milliseconds,
        .video_memory_usage = impl_->video_memory_usage,
        .video_memory_budget = impl_->video_memory_budget,
        .texture_slot_capacity = impl_->texture_slots.capacity(),
        .width = impl_->width,
        .height = impl_->height,
        .tearing = !impl_->options.graphics.vsync && impl_->tearing_supported,
    };
}

} // namespace eaw
