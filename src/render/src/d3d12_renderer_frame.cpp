// Per-frame state (camera, lighting, instances, debug lines, picks) and render().

#include "d3d12_renderer_impl.hpp"

namespace eaw {
namespace {

D3D12_RESOURCE_STATES d3d12_state(const GraphAccess access) noexcept
{
    switch (access)
    {
    case GraphAccess::render_target:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case GraphAccess::depth_write:
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case GraphAccess::shader_read:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case GraphAccess::copy_source:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case GraphAccess::present:
        return D3D12_RESOURCE_STATE_PRESENT;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

// Temporal anti-aliasing (engine choices): the jitter cycles through this many points of the
// Halton (2, 3) sequence, and each frame adds this share of itself to the history.
constexpr std::uint64_t taa_jitter_samples = 8;
constexpr float taa_new_frame_weight = 0.1F;

// The radical inverse of `index` in `base`: the Halton sequence's point, in [0, 1).
float halton(std::uint64_t index, const std::uint64_t base) noexcept
{
    float result = 0.0F;
    float fraction = 1.0F;
    while (index > 0)
    {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

} // namespace

void D3d12Renderer::Impl::collect_pick(FrameContext& frame)
{
    if (frame.picks.empty())
    {
        return;
    }
    const D3D12_RANGE read_range{0, frame.picks.size() * D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT};
    void* mapped{};
    check_device_hresult(frame.pick_readback->Map(0, &read_range, &mapped), frame.pick_readback, "Map(pick readback)");
    for (std::size_t index = 0; index < frame.picks.size(); ++index)
    {
        std::array<std::uint32_t, 2> texel{};
        std::memcpy(texel.data(), static_cast<const std::byte*>(mapped) + index * D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, sizeof(texel));
        VisualPick result;
        result.request = frame.picks[index].request;
        result.pixel = frame.picks[index].pixel;
        const auto value = texel[0];
        if (value != 0)
        {
            result.depth = std::bit_cast<float>(texel[1]);
        }
        if (value == pick_terrain)
        {
            result.terrain = true;
        }
        else if (value != 0)
        {
            result.instance = value - 1U;
        }
        completed_picks.push_back(result);
    }
    const D3D12_RANGE no_write{0, 0};
    frame.pick_readback->Unmap(0, &no_write);
    frame.picks.clear();
}

void D3d12Renderer::Impl::collect_capture(FrameContext& frame)
{
    if (!frame.capture)
    {
        return;
    }
    const auto copy = *frame.capture;
    frame.capture.reset();
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(copy.bytes)};
    void* mapped{};
    check_device_hresult(frame.capture_readback->Map(0, &read_range, &mapped), frame.capture_readback, "Map(capture readback)");
    RgbaImage image;
    image.width = copy.width;
    image.height = copy.height;
    image.pixels.resize(static_cast<std::size_t>(copy.width) * copy.height * 4U);
    for (std::uint32_t row = 0; row < copy.height; ++row)
    {
        std::memcpy(image.pixels.data() + static_cast<std::size_t>(row) * copy.width * 4U, static_cast<const std::byte*>(mapped) + static_cast<std::size_t>(row) * copy.row_pitch,
                    static_cast<std::size_t>(copy.width) * 4U);
    }
    const D3D12_RANGE no_write{0, 0};
    frame.capture_readback->Unmap(0, &no_write);
    // The back buffer's alpha is whatever blending left; a capture is opaque.
    for (std::size_t pixel = 3; pixel < image.pixels.size(); pixel += 4)
    {
        image.pixels[pixel] = 255;
    }
    completed_captures.push_back(std::move(image));
}

void D3d12Renderer::Impl::record_capture(FrameContext& frame)
{
    if (!capture_requested)
    {
        return;
    }
    capture_requested = false;
    const auto description = frame.back_buffer->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 total_bytes = 0;
    device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &total_bytes);
    // A new buffer per capture: this context's previous one was read before the context was reused.
    D3D12_HEAP_PROPERTIES readback_heap{};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = total_bytes;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    frame.capture_readback.Reset();
    check_device_hresult(device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&frame.capture_readback)), device,
                         "CreateCommittedResource(capture readback)");
    set_name(frame.capture_readback.Get(), L"EAWEngine frame capture readback");
    // The back buffer is in COPY_SOURCE (the frame graph's capture pass).
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = frame.back_buffer.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = frame.capture_readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    frame.capture = FrameContext::CaptureCopy{static_cast<std::uint32_t>(description.Width), description.Height, footprint.Footprint.RowPitch, total_bytes};
}

std::uint32_t D3d12Renderer::Impl::request_pick(const std::uint32_t x, const std::uint32_t y)
{
    const auto id = next_pick_request++;
    pending_picks.push_back({id, x, y});
    return id;
}

void D3d12Renderer::Impl::set_debug_lines(const std::span<const VisualDebugLine> lines)
{
    const auto finite = [](const auto& values)
    {
        return std::ranges::all_of(values,
                                   [](const float value)
                                   {
                                       return std::isfinite(value);
                                   });
    };
    EAW_ASSERT_MSG(std::ranges::all_of(lines,
                                       [&](const VisualDebugLine& line)
                                       {
                                           return is_finite(line.from) && is_finite(line.to) && is_finite(line.color);
                                       }),
                   "debug lines contain non-finite values");
    debug_lines.assign(lines.begin(), lines.end());
}

void D3d12Renderer::Impl::set_particles(const std::span<const VisualParticle> next)
{
    EAW_ASSERT_MSG(next.size() < std::numeric_limits<std::uint32_t>::max(), "too many particles for one frame");
    const auto finite = [](const auto& values)
    {
        return std::ranges::all_of(values,
                                   [](const float value)
                                   {
                                       return std::isfinite(value);
                                   });
    };
    EAW_ASSERT_MSG(std::ranges::all_of(next,
                                       [&](const VisualParticle& particle)
                                       {
                                           return is_finite(particle.position) && is_finite(particle.color) && finite(particle.uv_rect) && is_finite(particle.axis) &&
                                                  std::isfinite(particle.half_size) && std::isfinite(particle.rotation) && std::isfinite(particle.tail);
                                       }),
                   "particles contain non-finite values");
    particles.assign(next.begin(), next.end());
}

void D3d12Renderer::Impl::write_particles(FrameContext& frame)
{
    gpu_particles.clear();
    gpu_particles.reserve(particles.size());
    for (const auto& particle : particles)
    {
        const auto logical = particle.texture < scene_textures.size() ? static_cast<std::uint32_t>(fallback_texture_count + particle.texture) : invalid_visual_texture;
        gpu_particles.push_back({particle.position, particle.half_size, particle.color, particle.uv_rect, particle.axis, particle.rotation, particle.tail, logical,
                                 static_cast<std::uint32_t>(particle.shape), particle.monochrome ? 1U : 0U});
    }
    if (gpu_particles.empty())
    {
        return;
    }
    if (gpu_particles.size() > frame.particle_capacity)
    {
        if (frame.particle_buffer)
        {
            frame.particle_buffer->Unmap(0, nullptr);
            frame.particle_buffer_mapped = nullptr;
            retired_resources.push({std::move(frame.particle_buffer)}, direct_queue.next_value());
        }
        const auto capacity = std::max<std::size_t>({gpu_particles.size(), frame.particle_capacity * 2U, 1024U});
        const auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
        const auto description = buffer_description(static_cast<std::uint64_t>(capacity) * sizeof(GpuParticle));
        check_device_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.particle_buffer)),
                             device, "CreateCommittedResource(frame particles)");
        set_name(frame.particle_buffer.Get(), L"EAWEngine frame particles");
        const D3D12_RANGE no_read{0, 0};
        check_device_hresult(frame.particle_buffer->Map(0, &no_read, reinterpret_cast<void**>(&frame.particle_buffer_mapped)), frame.particle_buffer, "Map(frame particles)");
        frame.particle_capacity = capacity;
    }
    std::memcpy(frame.particle_buffer_mapped, gpu_particles.data(), gpu_particles.size() * sizeof(GpuParticle));
}

void D3d12Renderer::Impl::set_instances(const std::span<const VisualInstance> instances)
{
    EAW_ASSERT_MSG(scene_loaded, "set_instances needs a loaded scene (its meshes are the mesh library)");
    EAW_ASSERT_MSG(instances.size() < std::numeric_limits<std::uint32_t>::max(), "too many visual instances for one frame");
    for (std::size_t index = 0; index < instances.size(); ++index)
    {
        const auto& instance = instances[index];
        EAW_ASSERT_MSG(instance.mesh < scene_mesh_draws.size(),
                       "visual instance " + std::to_string(index) + " references mesh " + std::to_string(instance.mesh) + ", but the scene has " + std::to_string(scene_mesh_draws.size()));
        EAW_ASSERT_MSG(static_cast<std::uint64_t>(instance.bone_offset) + scene_mesh_bone_counts[instance.mesh] <= bone_transforms.size(),
                       "visual instance " + std::to_string(index) + " bone range exceeds the current bone palette (set it before the instances)");
        const auto finite = [](const auto& values)
        {
            return std::ranges::all_of(values,
                                       [](const float value)
                                       {
                                           return std::isfinite(value);
                                       });
        };
        EAW_ASSERT_MSG(is_finite(instance.transform) && is_finite(instance.tint) && is_finite(instance.team_color) && std::isfinite(instance.lod_bias),
                       "visual instance " + std::to_string(index) + " has non-finite values");
    }
    dynamic_instances.emplace(instances.begin(), instances.end());
}

void D3d12Renderer::Impl::batch_instances(const std::vector<VisualInstance>& instances, std::vector<GpuInstance>& gpu_instances, std::vector<GpuDraw>& draws)
{
    // Each instance draws its damage state at the detail level the LOD policy picks; a keyed
    // instance whose level changed draws both levels while it cross-fades, the old one fading out.
    struct Entry
    {
        std::uint32_t instance{};
        std::uint32_t state{};
        std::uint32_t level{};
        float fade{};
        // Squared distance from the camera to the bounding sphere's center (transparent draws are
        // ordered by it).
        float depth{};
    };
    std::vector<Entry> entries;
    entries.reserve(instances.size());
    lod_tracker.begin_pass();
    reduced_detail_instances = 0;
    fading_instances = 0;
    const float fov = camera.vertical_fov_degrees * 0.017453292F;
    const float viewport = static_cast<float>(height);
    for (std::uint32_t index = 0; index < instances.size(); ++index)
    {
        const auto& instance = instances[index];
        const auto& levels = scene_mesh_levels[instance.mesh];
        const auto found = std::ranges::find(levels.alternates, instance.state);
        const auto state = found != levels.alternates.end() ? static_cast<std::uint32_t>(found - levels.alternates.begin()) : 0U;
        const auto level_count = static_cast<std::uint32_t>(levels.draws[state].size());
        // The bounding sphere in the world: its center through the transform, its radius by the
        // largest axis scale.
        const auto center = transform_point(levels.center, instance.transform);
        const auto offset = center - camera.eye;
        // A non-finite transform sorts as nearest rather than breaking the order.
        const float squared = dot(offset, offset);
        const float depth = std::isfinite(squared) ? squared : 0.0F;
        if (instance.background || level_count <= 1)
        {
            entries.push_back({index, state, 0, 0.0F, depth});
            continue;
        }
        const auto& m = instance.transform.values;
        const float scale = std::max({std::hypot(m[0], m[1], m[2]), std::hypot(m[4], m[5], m[6]), std::hypot(m[8], m[9], m[10])});
        const float position = visual_lod_position(lod_settings, instance.lod_bias, length(center - camera.eye), levels.radius * scale, fov, viewport);
        if (instance.key == 0)
        {
            const auto level = visual_lod_level(position, level_count, std::nullopt, lod_settings.hysteresis);
            reduced_detail_instances += level != 0 ? 1U : 0U;
            entries.push_back({index, state, level, 0.0F, depth});
            continue;
        }
        const auto choice = lod_tracker.update(instance.key, instance.mesh, state, position, level_count, scene_time, lod_settings);
        fades_started += choice.changed && choice.from ? 1U : 0U;
        reduced_detail_instances += choice.level != 0 ? 1U : 0U;
        if (choice.from)
        {
            entries.push_back({index, state, choice.level, choice.fade, depth});
            entries.push_back({index, state, *choice.from, -choice.fade, depth});
            ++fading_instances;
            continue;
        }
        entries.push_back({index, state, choice.level, 0.0F, depth});
    }
    lod_tracker.end_pass();

    // Batches of one mesh, state, and level (backgrounds apart), in a stable order.
    const auto range_key = [&](const Entry& entry)
    {
        const auto& instance = instances[entry.instance];
        return std::tuple{instance.mesh, entry.state, entry.level, instance.background};
    };
    std::ranges::stable_sort(entries, {}, range_key);
    gpu_instances.clear();
    draws.clear();
    // Blended surfaces of the world (alpha and additive, which write no depth) are drawn after the
    // rest, instance by instance from the farthest, so they blend over what lies behind them; a
    // model's own parts keep their order. Backgrounds keep scene order.
    // shortcut: one draw per blended part and instance (hundreds on a busy map); merge neighbours of
    // one mesh in the sorted order if draw counts start to matter.
    const auto blended = [](const GpuDraw& draw)
    {
        return draw.blend == VisualBlendMode::alpha_blend || draw.blend == VisualBlendMode::additive;
    };
    struct Blended
    {
        float depth{};
        std::uint32_t gpu_instance{};
        const Entry* entry{};
    };
    std::vector<Blended> blended_instances;
    // Last frame's transform and palette offset of keyed instances (motion vectors); last frame's
    // palette follows the current one in the bone buffer.
    const auto previous_palette = static_cast<std::uint32_t>(bone_transforms.size());
    current_instances.clear();
    std::size_t range_start = 0;
    for (std::size_t position = 0; position <= entries.size(); ++position)
    {
        if (position < entries.size())
        {
            const auto& entry = entries[position];
            const auto& instance = instances[entry.instance];
            auto previous_transform = instance.transform;
            auto previous_bone_offset = instance.bone_offset;
            if (instance.key != 0)
            {
                if (const auto found = previous_instances.find(instance.key); found != previous_instances.end())
                {
                    previous_transform = found->second.transform;
                    previous_bone_offset = found->second.bone_offset + previous_palette;
                }
                current_instances.try_emplace(instance.key, PreviousInstance{instance.transform, instance.bone_offset});
            }
            gpu_instances.push_back(
                {instance.transform, instance.tint, instance.team_color, instance.bone_offset,
                 (instance.background ? background_instance_flag : 0U) | (instance.highlighted ? highlighted_instance_flag : 0U) | (instance.receives_shadows ? 0U : no_shadows_instance_flag),
                 entry.instance + 1U, entry.fade, previous_bone_offset, previous_transform});
        }
        if (position == entries.size() || (position > range_start && range_key(entries[position]) != range_key(entries[range_start])))
        {
            if (position > range_start)
            {
                const auto& first = entries[range_start];
                const auto& instance = instances[first.instance];
                bool has_blended = false;
                for (auto draw : scene_mesh_levels[instance.mesh].draws[first.state][first.level])
                {
                    if (!instance.background && blended(draw))
                    {
                        has_blended = true;
                        continue;
                    }
                    draw.first_instance = static_cast<std::uint32_t>(range_start);
                    draw.instance_count = static_cast<std::uint32_t>(position - range_start);
                    draw.background = instance.background;
                    draws.push_back(draw);
                }
                for (std::size_t member = range_start; has_blended && member < position; ++member)
                {
                    blended_instances.push_back({entries[member].depth, static_cast<std::uint32_t>(member), &entries[member]});
                }
            }
            range_start = position;
        }
    }
    std::ranges::stable_sort(draws, {},
                             [](const GpuDraw& draw)
                             {
                                 return draw.background ? 0U : 1U + static_cast<unsigned>(draw.blend);
                             });
    // Farthest first; equal depths in instance order.
    std::ranges::sort(blended_instances,
                      [](const Blended& left, const Blended& right)
                      {
                          return left.depth != right.depth ? left.depth > right.depth : left.gpu_instance < right.gpu_instance;
                      });
    for (const auto& item : blended_instances)
    {
        for (auto draw : scene_mesh_levels[instances[item.entry->instance].mesh].draws[item.entry->state][item.entry->level])
        {
            if (blended(draw))
            {
                draw.first_instance = item.gpu_instance;
                draw.instance_count = 1;
                draws.push_back(draw);
            }
        }
    }
    if (gpu_instances.empty())
    {
        gpu_instances.push_back({Matrix4::identity(), {1.0F, 1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F, 1.0F}, 0, 0, 0, 0.0F});
    }
    std::swap(previous_instances, current_instances);
}

void D3d12Renderer::Impl::write_dynamic_instances(FrameContext& frame)
{
    batch_instances(*dynamic_instances, dynamic_gpu_instances, dynamic_draws);
    if (dynamic_gpu_instances.size() > frame.instance_capacity)
    {
        if (frame.instance_buffer)
        {
            frame.instance_buffer->Unmap(0, nullptr);
            frame.instance_buffer_mapped = nullptr;
            retired_resources.push({std::move(frame.instance_buffer)}, direct_queue.next_value());
        }
        const auto capacity = std::max<std::size_t>({dynamic_gpu_instances.size(), frame.instance_capacity * 2U, 256U});
        const auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
        const auto description = buffer_description(static_cast<std::uint64_t>(capacity) * sizeof(GpuInstance));
        check_device_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.instance_buffer)),
                             device, "CreateCommittedResource(frame instances)");
        set_name(frame.instance_buffer.Get(), L"EAWEngine frame instances");
        const D3D12_RANGE no_read{0, 0};
        check_device_hresult(frame.instance_buffer->Map(0, &no_read, reinterpret_cast<void**>(&frame.instance_buffer_mapped)), frame.instance_buffer, "Map(frame instances)");
        frame.instance_capacity = capacity;
    }
    std::memcpy(frame.instance_buffer_mapped, dynamic_gpu_instances.data(), dynamic_gpu_instances.size() * sizeof(GpuInstance));
}

void D3d12Renderer::Impl::set_selection_rectangle(const std::optional<std::array<float, 4>>& rectangle)
{
    selection_rectangle = rectangle;
}

void D3d12Renderer::Impl::set_highlight(const std::optional<std::uint32_t> instance)
{
    highlighted_pick_id = instance && *instance < std::numeric_limits<std::uint32_t>::max() ? *instance + 1U : 0U;
}

void D3d12Renderer::Impl::set_lighting(const VisualLightRig& next)
{
    const auto valid_light = [&](const VisualDirectionalLight& light)
    {
        const auto& direction = light.direction;
        return is_finite(direction) && is_finite(light.color) && dot(direction, direction) > 1.0e-12F;
    };
    EAW_ASSERT_MSG(valid_light(next.key) && valid_light(next.fills[0]) && valid_light(next.fills[1]) && is_finite(next.key_specular) && is_finite(next.ambient),
                   "visual light rig contains non-finite values or a zero light direction");
    lighting = next;
}

void D3d12Renderer::Impl::set_scene_time(const float seconds)
{
    EAW_ASSERT_MSG(std::isfinite(seconds), "scene time is not finite");
    scene_time = seconds;
}

void D3d12Renderer::Impl::set_camera(const VisualCamera& next)
{
    EAW_ASSERT_MSG(is_finite(next.eye) && is_finite(next.target) && is_finite(next.up) && std::isfinite(next.vertical_fov_degrees) && std::isfinite(next.near_plane) && std::isfinite(next.far_plane),
                   "visual camera contains non-finite values");
    EAW_ASSERT_MSG(next.near_plane > 0.0F && next.far_plane > next.near_plane * 1.0001F && next.vertical_fov_degrees > 1.0F && next.vertical_fov_degrees < 179.0F,
                   "visual camera has an invalid projection (near " + std::to_string(next.near_plane) + ", far " + std::to_string(next.far_plane) + ", fov " +
                       std::to_string(next.vertical_fov_degrees) + ")");
    const auto forward = next.target - next.eye;
    const auto side = cross(next.up, forward);
    EAW_ASSERT_MSG(dot(forward, forward) > 1.0e-12F && dot(side, side) > 1.0e-12F, "visual camera eye, target, and up do not define a view");
    camera = next;
}

void D3d12Renderer::Impl::record_shadow_cascades(FrameContext& frame, const bool dynamic)
{
    ID3D12DescriptorHeap* descriptor_heaps[]{shader_heap.Get()};
    command_list->SetDescriptorHeaps(1, descriptor_heaps);
    command_list->SetGraphicsRootSignature(root_signature.Get());
    command_list->SetGraphicsRootConstantBufferView(0, frame.constant_buffer->GetGPUVirtualAddress());
    command_list->SetGraphicsRootDescriptorTable(1, shader_heap->GetGPUDescriptorHandleForHeapStart());
    command_list->SetGraphicsRootShaderResourceView(2, frame.bone_buffer->GetGPUVirtualAddress());
    command_list->SetGraphicsRootShaderResourceView(3, (dynamic ? frame.instance_buffer : scene_instance_buffer)->GetGPUVirtualAddress());
    command_list->SetGraphicsRootShaderResourceView(4, scene_material_buffer->GetGPUVirtualAddress());
    command_list->SetGraphicsRootShaderResourceView(6, frame.residency_buffer->GetGPUVirtualAddress());
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(shadow_resolution);
    viewport.Height = static_cast<float>(shadow_resolution);
    viewport.MaxDepth = 1.0F;
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(shadow_resolution), static_cast<LONG>(shadow_resolution)};
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor);
    // shortcut: every caster is drawn into every cascade; cull per cascade when shadow draws cost too much.
    const auto& draws = dynamic ? dynamic_draws : scene_draws;
    for (std::uint32_t cascade = 0; cascade < frame_cascades.count; ++cascade)
    {
        auto target = shadow_dsv_heap->GetCPUDescriptorHandleForHeapStart();
        target.ptr += static_cast<SIZE_T>(cascade) * shadow_dsv_increment;
        command_list->OMSetRenderTargets(0, nullptr, FALSE, &target);
        command_list->ClearDepthStencilView(target, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);
        command_list->SetGraphicsRoot32BitConstant(5, cascade, 2);
        if (terrain_vertex_count != 0)
        {
            command_list->SetPipelineState(terrain_shadow_pipeline.Get());
            const std::array<std::uint32_t, 2> terrain_constants{0U, terrain_first_material};
            command_list->SetGraphicsRoot32BitConstants(5, 2, terrain_constants.data(), 0);
            command_list->IASetVertexBuffers(0, 1, &terrain_vertex_view);
            command_list->DrawInstanced(terrain_vertex_count, 1, 0, 0);
        }
        std::uint32_t bound_block = std::numeric_limits<std::uint32_t>::max();
        ID3D12PipelineState* bound_pipeline = nullptr;
        for (const auto& draw : draws)
        {
            // Every opaque and alpha-tested model casts; backgrounds and blended parts do not.
            if (draw.background || (draw.blend != VisualBlendMode::opaque && draw.blend != VisualBlendMode::alpha_test))
            {
                continue;
            }
            auto* pipeline = draw.blend == VisualBlendMode::alpha_test ? shadow_alpha_test_pipeline.Get() : shadow_pipeline.Get();
            if (pipeline != bound_pipeline)
            {
                command_list->SetPipelineState(pipeline);
                bound_pipeline = pipeline;
            }
            if (draw.block != bound_block)
            {
                const auto& vertex_view = draw.block == 0 ? scene_vertex_view : added_blocks[draw.block - 1U].vertex_view;
                const auto& index_view = draw.block == 0 ? scene_index_view : added_blocks[draw.block - 1U].index_view;
                command_list->IASetVertexBuffers(0, 1, &vertex_view);
                command_list->IASetIndexBuffer(&index_view);
                bound_block = draw.block;
            }
            const std::array<std::uint32_t, 2> draw_constants{draw.first_instance, draw.material};
            command_list->SetGraphicsRoot32BitConstants(5, 2, draw_constants.data(), 0);
            command_list->DrawIndexedInstanced(draw.geometry.index_count, draw.instance_count, draw.geometry.first_index, draw.geometry.base_vertex, 0);
        }
    }
}

DirectX::XMMATRIX D3d12Renderer::Impl::camera_view_projection() const
{
    using namespace DirectX;
    const auto eye = XMVectorSet(camera.eye[0], camera.eye[1], camera.eye[2], 1.0F);
    const auto target = XMVectorSet(camera.target[0], camera.target[1], camera.target[2], 1.0F);
    const auto up = XMVectorSet(camera.up[0], camera.up[1], camera.up[2], 0.0F);
    // The world is right-handed (Z up, X east, Y north, as the game's data and 3ds Max are): a
    // left-handed view mirrored every scene left to right against the original.
    const auto view = XMMatrixLookAtRH(eye, target, up);
    const auto aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1U));
    const auto projection = XMMatrixPerspectiveFovRH(XMConvertToRadians(camera.vertical_fov_degrees), aspect, camera.near_plane, camera.far_plane);
    return view * projection;
}

bool D3d12Renderer::Impl::render()
{
    retire_completed_resources();
    // Picks are read as soon as their frame's fence has completed, from any frame context, rather
    // than when that context is next reused. Every context's picks and fence value were recorded
    // together in an earlier render(), so the check is safe here.
    {
        const auto completed = direct_queue.completed_value();
        for (auto& context : frames)
        {
            if (!context.picks.empty() && context.fence_value != 0 && completed >= context.fence_value)
            {
                collect_pick(context);
            }
            if (context.capture && context.fence_value != 0 && completed >= context.fence_value)
            {
                collect_capture(context);
            }
        }
    }
    if (occluded)
    {
        const auto test_result = swap_chain->Present(0, DXGI_PRESENT_TEST);
        if (test_result == DXGI_STATUS_OCCLUDED)
        {
            return false;
        }
        check_device_hresult(test_result, device, "IDXGISwapChain::Present(test)");
        occluded = false;
    }
    if (!wait_for_frame_latency())
    {
        return false;
    }

    const auto frame_index = swap_chain->GetCurrentBackBufferIndex();
    auto& frame = frames[frame_index];
    if (frame.fence_value != 0 && direct_queue.completed_value() < frame.fence_value)
    {
        direct_queue.wait_cpu(frame.fence_value);
    }
    // This context's capture finished with its fence; read it before the context is reused.
    collect_capture(frame);
    // The palette, then last frame's (motion vectors address it past the current one; the first
    // frame repeats the current one).
    if (previous_bone_transforms.empty())
    {
        previous_bone_transforms = bone_transforms;
    }
    ensure_bone_capacity(frame, bone_transforms.size() + previous_bone_transforms.size());
    std::memcpy(frame.bone_buffer_mapped, bone_transforms.data(), bone_transforms.size() * sizeof(bone_transforms.front()));
    std::memcpy(frame.bone_buffer_mapped + bone_transforms.size() * sizeof(Matrix4), previous_bone_transforms.data(), previous_bone_transforms.size() * sizeof(Matrix4));
    // The next frame's previous palette is this one (batching below needs only its size).
    previous_bone_transforms = bone_transforms;
    stream_scene_textures();
    // The overlay's frame is built now: its texture uploads join this frame's pending transitions.
    const bool draw_overlay = (selection_rectangle || overlay_ui) && overlay_backend_initialized;
    if (draw_overlay)
    {
        prepare_overlay();
    }
    write_texture_residency(frame);
    const bool dynamic = scene_loaded && dynamic_instances.has_value();
    if (dynamic)
    {
        write_dynamic_instances(frame);
    }
    // This context's fence has completed (waited above), so any picks it still holds are ready;
    // collect them before recording new picks into the same readback slots.
    write_particles(frame);
    collect_pick(frame);
    collect_timestamps(frame);
    if (frame_number % 30U == 0)
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
        if (SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)))
        {
            video_memory_usage = memory.CurrentUsage;
            video_memory_budget = memory.Budget;
        }
    }
    std::uint32_t draw_calls = 0;
    std::uint64_t triangles = 0;
    std::uint64_t instances_drawn = 0;
    const auto debug_line_count = std::min(debug_lines.size(), maximum_debug_lines);
    auto* debug_vertices = reinterpret_cast<DebugVertex*>(frame.debug_vertices_mapped);
    for (std::size_t index = 0; index < debug_line_count; ++index)
    {
        const auto& line = debug_lines[index];
        debug_vertices[index * 2U] = {line.from, line.color};
        debug_vertices[index * 2U + 1U] = {line.to, line.color};
    }
    // This frame context's fence has completed above, so its constants can be rewritten.
    FrameConstants constants{};
    {
        // Under temporal anti-aliasing the scene is drawn a sub-pixel off each frame (a translation
        // in clip space); the resolve reprojects with the unjittered matrices.
        using namespace DirectX;
        const bool taa = options.anti_aliasing == VisualAntiAliasing::taa || options.anti_aliasing == VisualAntiAliasing::hybrid;
        const auto unjittered = camera_view_projection();
        std::array<float, 2> jitter{};
        if (taa)
        {
            const auto sample = taa_frame++ % taa_jitter_samples + 1U;
            jitter = {(halton(sample, 2) - 0.5F) * 2.0F / static_cast<float>(std::max(width, 1U)), (halton(sample, 3) - 0.5F) * 2.0F / static_cast<float>(std::max(height, 1U))};
        }
        XMStoreFloat4x4(&constants.view_projection, XMMatrixTranspose(unjittered * XMMatrixTranslation(jitter[0], jitter[1], 0.0F)));
        XMStoreFloat4x4(&constants.unjittered_view_projection, XMMatrixTranspose(unjittered));
        XMStoreFloat4x4(&constants.inverse_view_projection, XMMatrixTranspose(XMMatrixInverse(nullptr, unjittered)));
        const bool history = taa && taa_history_valid;
        constants.previous_view_projection = previous_camera_known ? previous_camera : constants.unjittered_view_projection;
        previous_camera_known = true;
        constants.taa_parameters = {jitter[0], jitter[1], history ? 1.0F : 0.0F, taa_new_frame_weight};
        previous_camera = constants.unjittered_view_projection;
    }
    constants.camera_position_time = {camera.eye[0], camera.eye[1], camera.eye[2], scene_time};
    const auto unit = [](const Vector3& value)
    {
        const auto l = length(value);
        return std::array<float, 4>{value.x / l, value.y / l, value.z / l, 0.0F};
    };
    const auto rgb = [](const ColorRgb& value)
    {
        return std::array<float, 4>{value.r, value.g, value.b, 0.0F};
    };
    constants.key_direction = unit(lighting.key.direction);
    constants.key_diffuse = rgb(lighting.key.color);
    constants.key_specular = rgb(lighting.key_specular);
    constants.ambient = rgb(lighting.ambient);
    for (std::size_t index = 0; index < lighting.fills.size(); ++index)
    {
        constants.fill_direction[index] = unit(lighting.fills[index].direction);
        constants.fill_diffuse[index] = rgb(lighting.fills[index].color);
    }
    constants.selection = {highlighted_pick_id, 0, 0, 0};
    {
        // The view basis of XMMatrixLookAtRH: right = forward x up, up' = right x forward.
        const auto forward = camera.target - camera.eye;
        const auto right = unit(cross(forward, camera.up));
        const auto view_up = unit(cross(Vector3{right[0], right[1], right[2]}, forward));
        constants.camera_right = right;
        constants.camera_up = view_up;
    }
    constants.post_parameters = {options.bloom_threshold, options.bloom_strength, 0.15F, 0.0F};
    {
        // Key-light shadows for this view (none before a scene is loaded).
        frame_cascades = {};
        if (shadow_resolution != 0 && scene_loaded)
        {
            ShadowCascadeSettings settings;
            settings.cascade_count = options.shadow_cascades;
            settings.resolution = shadow_resolution;
            frame_cascades =
                compute_shadow_cascades(camera, static_cast<float>(width) / static_cast<float>(std::max(height, 1U)), lighting.key.direction, scene_bounds_minimum, scene_bounds_maximum, settings);
        }
        for (std::uint32_t index = 0; index < frame_cascades.count; ++index)
        {
            const auto& cascade = frame_cascades.cascades[index];
            // Transposed, as every matrix in the constants.
            for (std::size_t row = 0; row < 4; ++row)
            {
                for (std::size_t column = 0; column < 4; ++column)
                {
                    constants.shadow_view_projection[index].m[row][column] = cascade.view_projection(column, row);
                }
            }
            constants.shadow_splits[index] = cascade.split_distance;
            constants.shadow_texels[index] = cascade.texel_size;
            constants.shadow_slots[index] = shadow_slots[index];
        }
        constants.camera_forward = unit(camera.target - camera.eye);
        constants.shadow_parameters = {static_cast<float>(frame_cascades.count), shadow_normal_offset_texels, shadow_blend_band,
                                       shadow_resolution != 0 ? shadow_filter_texels / static_cast<float>(shadow_resolution) : 0.0F};
    }
    std::memcpy(frame.constant_buffer_mapped, &constants, sizeof(constants));

    check_device_hresult(frame.allocator->Reset(), frame.allocator, "ID3D12CommandAllocator::Reset");
    check_device_hresult(command_list->Reset(frame.allocator.Get(), opaque_pipeline.Get()), command_list, "ID3D12GraphicsCommandList::Reset");
    command_list->EndQuery(timestamp_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_index * 2U);

    if (!pending_transitions.empty())
    {
        command_list->ResourceBarrier(static_cast<UINT>(pending_transitions.size()), pending_transitions.data());
        pending_transitions.clear();
    }

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0F;
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor);

    auto render_target = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    render_target.ptr += static_cast<SIZE_T>(frame_index) * rtv_increment;
    const auto depth_target = dsv_heap->GetCPUDescriptorHandleForHeapStart();
    // With bloom or anti-aliasing the scene renders into its own target; the composite goes to
    // post_color when anti-aliasing follows, else to the back buffer, and anti-aliasing fills the
    // back buffer from the composite (or from the scene without bloom).
    const bool bloom = options.bloom;
    const bool anti_aliasing = options.anti_aliasing != VisualAntiAliasing::off;
    const bool temporal = options.anti_aliasing == VisualAntiAliasing::taa || options.anti_aliasing == VisualAntiAliasing::hybrid;
    const bool hybrid = options.anti_aliasing == VisualAntiAliasing::hybrid;
    const auto scene_target = bloom || anti_aliasing ? post_rtv(0) : render_target;
    const auto composite_target = anti_aliasing ? post_rtv(3) : render_target;

    // The frame's passes and how each uses its resources (render/frame_graph.hpp); the graph places
    // the barriers.
    frame_graph.clear();
    graph_resources.clear();
    const auto import_resource = [&](ID3D12Resource* resource, const char* name, const GraphAccess initial, const GraphAccess final)
    {
        graph_resources.push_back(resource);
        return frame_graph.import_resource(name, initial, final);
    };
    const auto back_buffer = import_resource(frame.back_buffer.Get(), "back buffer", GraphAccess::present, GraphAccess::present);
    const auto pick = import_resource(pick_target.Get(), "pick target", GraphAccess::render_target, GraphAccess::render_target);
    const auto depth = import_resource(depth_buffer.Get(), "depth", GraphAccess::depth_write, GraphAccess::depth_write);
    const auto motion = import_resource(motion_target.Get(), "motion", GraphAccess::render_target, GraphAccess::render_target);
    const auto scene = bloom || anti_aliasing ? import_resource(scene_color.Get(), "scene color", GraphAccess::render_target, GraphAccess::render_target) : back_buffer;
    const auto composite = bloom && anti_aliasing ? import_resource(post_color.Get(), "post color", GraphAccess::render_target, GraphAccess::render_target) : back_buffer;

    // Key-light shadows: every cascade's map, drawn before the scene samples them.
    std::vector<FrameGraph::Use> scene_uses{{scene, GraphAccess::render_target}, {pick, GraphAccess::render_target}, {motion, GraphAccess::render_target}, {depth, GraphAccess::depth_write}};
    if (frame_cascades.count > 0)
    {
        std::vector<FrameGraph::Use> shadow_writes;
        for (std::uint32_t index = 0; index < frame_cascades.count; ++index)
        {
            const auto map = import_resource(shadow_maps[index].Get(), "shadow cascade", GraphAccess::depth_write, GraphAccess::depth_write);
            shadow_writes.push_back({map, GraphAccess::depth_write});
            scene_uses.push_back({map, GraphAccess::shader_read});
        }
        frame_graph.add_pass("shadow cascades", std::move(shadow_writes),
                             [&]
                             {
                                 record_shadow_cascades(frame, dynamic);
                             });
    }

    frame_graph.add_pass(
        "scene", std::move(scene_uses),
        [&]
        {
            const std::array targets{scene_target, pick_rtv_heap->GetCPUDescriptorHandleForHeapStart(), post_rtv(6)};
            command_list->RSSetViewports(1, &viewport);
            command_list->RSSetScissorRects(1, &scissor);
            command_list->OMSetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), FALSE, &depth_target);
            constexpr std::array clear_color{0.018F, 0.031F, 0.043F, 1.0F};
            constexpr std::array clear_pick{0.0F, 0.0F, 0.0F, 0.0F};
            command_list->ClearRenderTargetView(scene_target, clear_color.data(), 0, nullptr);
            command_list->ClearRenderTargetView(targets[1], clear_pick.data(), 0, nullptr);
            constexpr std::array clear_motion{motion_unknown, motion_unknown, 0.0F, 0.0F};
            command_list->ClearRenderTargetView(targets[2], clear_motion.data(), 0, nullptr);
            command_list->ClearDepthStencilView(depth_target, D3D12_CLEAR_FLAG_DEPTH, 1.0F, 0, 0, nullptr);

            ID3D12DescriptorHeap* descriptor_heaps[]{shader_heap.Get()};
            command_list->SetDescriptorHeaps(1, descriptor_heaps);
            command_list->SetGraphicsRootSignature(root_signature.Get());
            // Root parameters (legacy_common.hlsli): 0 frame CBV, 1 texture table, 2 bones, 3 instances,
            // 4 materials, 5 draw constants (first instance, material), 6 texture residency.
            command_list->SetGraphicsRootConstantBufferView(0, frame.constant_buffer->GetGPUVirtualAddress());
            command_list->SetGraphicsRootDescriptorTable(1, shader_heap->GetGPUDescriptorHandleForHeapStart());
            command_list->SetGraphicsRootShaderResourceView(2, frame.bone_buffer->GetGPUVirtualAddress());
            command_list->SetGraphicsRootShaderResourceView(3, (dynamic ? frame.instance_buffer : scene_loaded ? scene_instance_buffer : identity_instance_buffer)->GetGPUVirtualAddress());
            command_list->SetGraphicsRootShaderResourceView(4, (scene_loaded ? scene_material_buffer : identity_material_buffer)->GetGPUVirtualAddress());
            command_list->SetGraphicsRootShaderResourceView(6, frame.residency_buffer->GetGPUVirtualAddress());
            command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            if (!scene_loaded)
            {
                command_list->SetPipelineState(opaque_pipeline.Get());
                const std::array<std::uint32_t, 2> draw_constants{0U, 0U};
                command_list->SetGraphicsRoot32BitConstants(5, 2, draw_constants.data(), 0);
                command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view);
                command_list->DrawInstanced(3, 1, 0, 0);
                ++draw_calls;
                ++triangles;
                ++instances_drawn;
            }
            else
            {
                bool terrain_drawn = terrain_vertex_count == 0;
                const auto draw_terrain = [&]
                {
                    command_list->SetPipelineState(terrain_pipeline.Get());
                    const std::array<std::uint32_t, 2> terrain_constants{0U, terrain_first_material};
                    command_list->SetGraphicsRoot32BitConstants(5, 2, terrain_constants.data(), 0);
                    command_list->IASetVertexBuffers(0, 1, &terrain_vertex_view);
                    command_list->DrawInstanced(terrain_vertex_count, 1, 0, 0);
                    ++draw_calls;
                    triangles += terrain_vertex_count / 3U;
                    terrain_drawn = true;
                };
                // Geometry blocks: 0 is the scene's, others came from add_meshes.
                std::uint32_t bound_block = 0;
                const auto bind_block = [&](const std::uint32_t block)
                {
                    const auto& vertex_view = block == 0 ? scene_vertex_view : added_blocks[block - 1U].vertex_view;
                    const auto& index_view = block == 0 ? scene_index_view : added_blocks[block - 1U].index_view;
                    command_list->IASetVertexBuffers(0, 1, &vertex_view);
                    command_list->IASetIndexBuffer(&index_view);
                    bound_block = block;
                };
                bind_block(0);
                for (const auto& draw : dynamic ? dynamic_draws : scene_draws)
                {
                    if (!terrain_drawn && !draw.background)
                    {
                        draw_terrain();
                        bind_block(bound_block);
                    }
                    if (draw.block != bound_block)
                    {
                        bind_block(draw.block);
                    }
                    ID3D12PipelineState* pipeline = draw.background ? background_opaque_pipeline.Get() : opaque_pipeline.Get();
                    if (draw.blend == VisualBlendMode::alpha_blend)
                    {
                        pipeline = draw.background ? background_alpha_pipeline.Get() : alpha_pipeline.Get();
                    }
                    else if (draw.blend == VisualBlendMode::additive)
                    {
                        pipeline = draw.background ? background_additive_pipeline.Get() : additive_pipeline.Get();
                    }
                    command_list->SetPipelineState(pipeline);
                    const std::array<std::uint32_t, 2> draw_constants{draw.first_instance, draw.material};
                    command_list->SetGraphicsRoot32BitConstants(5, 2, draw_constants.data(), 0);
                    command_list->DrawIndexedInstanced(draw.geometry.index_count, draw.instance_count, draw.geometry.first_index, draw.geometry.base_vertex, 0);
                    ++draw_calls;
                    triangles += static_cast<std::uint64_t>(draw.geometry.index_count / 3U) * draw.instance_count;
                    instances_drawn += draw.instance_count;
                }
                if (!terrain_drawn)
                {
                    draw_terrain();
                }
            }

            // Particles: runs of one blend and depth mode, in the given (back-to-front) order.
            if (!gpu_particles.empty())
            {
                command_list->SetGraphicsRootShaderResourceView(3, frame.particle_buffer->GetGPUVirtualAddress());
                std::size_t run_start = 0;
                const auto pipeline_of = [&](const VisualParticle& particle)
                {
                    return particle_pipelines[static_cast<std::size_t>(particle.blend)][particle.depth_test ? 0U : 1U].Get();
                };
                for (std::size_t index = 1; index <= particles.size(); ++index)
                {
                    if (index < particles.size() && pipeline_of(particles[index]) == pipeline_of(particles[run_start]))
                    {
                        continue;
                    }
                    command_list->SetPipelineState(pipeline_of(particles[run_start]));
                    const std::array<std::uint32_t, 2> particle_constants{static_cast<std::uint32_t>(run_start), 0U};
                    command_list->SetGraphicsRoot32BitConstants(5, 2, particle_constants.data(), 0);
                    command_list->DrawInstanced(6, static_cast<UINT>(index - run_start), 0, 0);
                    ++draw_calls;
                    triangles += static_cast<std::uint64_t>(index - run_start) * 2U;
                    run_start = index;
                }
            }
            last_particles = gpu_particles.size();
        });

    if (bloom)
    {
        // Bright pass at reduced size, two horizontal-then-vertical blur rounds ping-ponging between
        // the bloom targets, then scene plus bloom into the back buffer.
        const auto bloom_a = import_resource(bloom_targets[0].Get(), "bloom A", GraphAccess::render_target, GraphAccess::render_target);
        const auto bloom_b = import_resource(bloom_targets[1].Get(), "bloom B", GraphAccess::render_target, GraphAccess::render_target);
        const auto pass = [&](ID3D12PipelineState* pipeline, const D3D12_CPU_DESCRIPTOR_HANDLE target, const std::uint32_t source_slot, const std::uint32_t parameter)
        {
            command_list->OMSetRenderTargets(1, &target, FALSE, nullptr);
            command_list->SetPipelineState(pipeline);
            const std::array<std::uint32_t, 2> post_constants{source_slot, parameter};
            command_list->SetGraphicsRoot32BitConstants(5, 2, post_constants.data(), 0);
            command_list->DrawInstanced(3, 1, 0, 0);
            ++draw_calls;
        };
        frame_graph.add_pass("bloom extract", {{scene, GraphAccess::shader_read}, {bloom_a, GraphAccess::render_target}},
                             [&, pass]
                             {
                                 command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                                 D3D12_VIEWPORT bloom_viewport{};
                                 bloom_viewport.Width = static_cast<float>(bloom_width);
                                 bloom_viewport.Height = static_cast<float>(bloom_height);
                                 bloom_viewport.MaxDepth = 1.0F;
                                 const D3D12_RECT bloom_scissor{0, 0, static_cast<LONG>(bloom_width), static_cast<LONG>(bloom_height)};
                                 command_list->RSSetViewports(1, &bloom_viewport);
                                 command_list->RSSetScissorRects(1, &bloom_scissor);
                                 pass(bloom_extract_pipeline.Get(), post_rtv(1), post_slots[0], 0U);
                             });
        for (int round = 0; round < 2; ++round)
        {
            frame_graph.add_pass("bloom blur across", {{bloom_a, GraphAccess::shader_read}, {bloom_b, GraphAccess::render_target}},
                                 [&, pass]
                                 {
                                     pass(bloom_blur_pipeline.Get(), post_rtv(2), post_slots[1], 0U);
                                 });
            frame_graph.add_pass("bloom blur down", {{bloom_b, GraphAccess::shader_read}, {bloom_a, GraphAccess::render_target}},
                                 [&, pass]
                                 {
                                     pass(bloom_blur_pipeline.Get(), post_rtv(1), post_slots[2], 1U);
                                 });
        }
        frame_graph.add_pass("bloom composite", {{scene, GraphAccess::shader_read}, {bloom_a, GraphAccess::shader_read}, {composite, GraphAccess::render_target}},
                             [&, pass]
                             {
                                 command_list->RSSetViewports(1, &viewport);
                                 command_list->RSSetScissorRects(1, &scissor);
                                 pass(bloom_composite_pipeline.Get(), composite_target, post_slots[0], post_slots[1]);
                             });
    }

    if (temporal)
    {
        const auto source = bloom ? composite : scene;
        const auto source_slot = bloom ? post_slots[3] : post_slots[0];
        const auto last = taa_history_index;
        const auto next = 1U - last;
        const auto history_last = import_resource(taa_history[last].Get(), "TAA history (last)", GraphAccess::render_target, GraphAccess::render_target);
        const auto history_next = import_resource(taa_history[next].Get(), "TAA history (next)", GraphAccess::render_target, GraphAccess::render_target);
        frame_graph.add_pass("temporal anti-aliasing",
                             {{source, GraphAccess::shader_read},
                              {depth, GraphAccess::shader_read},
                              {motion, GraphAccess::shader_read},
                              {history_last, GraphAccess::shader_read},
                              {back_buffer, GraphAccess::render_target},
                              {history_next, GraphAccess::render_target}},
                             [&, source_slot, last, next]
                             {
                                 const std::array taa_targets{render_target, post_rtv(4U + next)};
                                 command_list->OMSetRenderTargets(static_cast<UINT>(taa_targets.size()), taa_targets.data(), FALSE, nullptr);
                                 command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                                 command_list->RSSetViewports(1, &viewport);
                                 command_list->RSSetScissorRects(1, &scissor);
                                 command_list->SetPipelineState(taa_pipeline.Get());
                                 const std::array<std::uint32_t, 2> taa_constants{source_slot | (post_slots[6] << 16U), post_slots[4U + last] | (depth_slot << 16U)};
                                 command_list->SetGraphicsRoot32BitConstants(5, 2, taa_constants.data(), 0);
                                 command_list->DrawInstanced(3, 1, 0, 0);
                                 ++draw_calls;
                             });
        taa_history_index = next;
        taa_history_valid = true;
        if (hybrid)
        {
            // The edge pass over TAA's result (the history it just wrote) into the back buffer.
            const auto history_slot = post_slots[4U + next];
            frame_graph.add_pass("edge anti-aliasing after TAA", {{history_next, GraphAccess::shader_read}, {back_buffer, GraphAccess::render_target}},
                                 [&, history_slot]
                                 {
                                     command_list->OMSetRenderTargets(1, &render_target, FALSE, nullptr);
                                     command_list->SetPipelineState(edge_aa_pipeline.Get());
                                     const std::array<std::uint32_t, 2> post_constants{history_slot, 0U};
                                     command_list->SetGraphicsRoot32BitConstants(5, 2, post_constants.data(), 0);
                                     command_list->DrawInstanced(3, 1, 0, 0);
                                     ++draw_calls;
                                 });
        }
    }
    else if (anti_aliasing)
    {
        const auto source = bloom ? composite : scene;
        const auto source_slot = bloom ? post_slots[3] : post_slots[0];
        frame_graph.add_pass("edge anti-aliasing", {{source, GraphAccess::shader_read}, {back_buffer, GraphAccess::render_target}},
                             [&, source_slot]
                             {
                                 command_list->OMSetRenderTargets(1, &render_target, FALSE, nullptr);
                                 command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                                 command_list->RSSetViewports(1, &viewport);
                                 command_list->RSSetScissorRects(1, &scissor);
                                 command_list->SetPipelineState(edge_aa_pipeline.Get());
                                 const std::array<std::uint32_t, 2> post_constants{source_slot, 0U};
                                 command_list->SetGraphicsRoot32BitConstants(5, 2, post_constants.data(), 0);
                                 command_list->DrawInstanced(3, 1, 0, 0);
                                 ++draw_calls;
                             });
    }

    if (!temporal)
    {
        taa_history_valid = false;
    }

    // A requested capture copies the frame as the scene and post-processing left it.
    if (capture_requested)
    {
        frame_graph.add_pass("capture", {{back_buffer, GraphAccess::copy_source}},
                             [&]
                             {
                                 record_capture(frame);
                             });
    }

    // Debug lines and the overlay draw over the frame, with the scene's depth and pick target.
    frame_graph.add_pass("debug and overlay", {{back_buffer, GraphAccess::render_target}, {pick, GraphAccess::render_target}, {depth, GraphAccess::depth_write}},
                         [&]
                         {
                             const std::array final_targets{render_target, pick_rtv_heap->GetCPUDescriptorHandleForHeapStart()};
                             command_list->OMSetRenderTargets(static_cast<UINT>(final_targets.size()), final_targets.data(), FALSE, &depth_target);
                             if (debug_line_count != 0)
                             {
                                 command_list->SetPipelineState(debug_pipeline.Get());
                                 command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
                                 D3D12_VERTEX_BUFFER_VIEW debug_view{};
                                 debug_view.BufferLocation = frame.debug_vertex_buffer->GetGPUVirtualAddress();
                                 debug_view.SizeInBytes = static_cast<UINT>(debug_line_count * 2U * sizeof(DebugVertex));
                                 debug_view.StrideInBytes = sizeof(DebugVertex);
                                 command_list->IASetVertexBuffers(0, 1, &debug_view);
                                 command_list->DrawInstanced(static_cast<UINT>(debug_line_count * 2U), 1, 0, 0);
                                 ++draw_calls;
                             }

                             if (draw_overlay)
                             {
                                 render_overlay(render_target);
                             }
                         });

    // Requested picks copy one texel each (up to a per-frame limit; the rest wait for the next
    // frame); they are read when this frame's fence completes.
    if (!pending_picks.empty())
    {
        frame_graph.add_pass("picks", {{pick, GraphAccess::copy_source}},
                             [&]
                             {
                                 D3D12_TEXTURE_COPY_LOCATION source{};
                                 source.pResource = pick_target.Get();
                                 source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                                 while (!pending_picks.empty() && frame.picks.size() < maximum_picks_per_frame)
                                 {
                                     const auto request = pending_picks.front();
                                     pending_picks.pop_front();
                                     const auto x = std::min(request.x, width - 1);
                                     const auto y = std::min(request.y, height - 1);
                                     D3D12_TEXTURE_COPY_LOCATION destination{};
                                     destination.pResource = frame.pick_readback.Get();
                                     destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                                     destination.PlacedFootprint.Offset = frame.picks.size() * D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
                                     destination.PlacedFootprint.Footprint = {pick_format, 1, 1, 1, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT};
                                     const D3D12_BOX texel{x, y, 0, x + 1, y + 1, 1};
                                     command_list->CopyTextureRegion(&destination, 0, 0, 0, &source, &texel);
                                     frame.picks.push_back({request.id, {x, y}});
                                 }
                             });
    }

    frame_graph.execute(
        [&](const std::span<const GraphTransition> transitions)
        {
            graph_barriers.clear();
            for (const auto& transition : transitions)
            {
                graph_barriers.push_back(transition_barrier(graph_resources[transition.resource], d3d12_state(transition.before), d3d12_state(transition.after)));
            }
            command_list->ResourceBarrier(static_cast<UINT>(graph_barriers.size()), graph_barriers.data());
        });
    last_draw_calls = draw_calls;
    last_triangles = triangles;
    last_instances = instances_drawn;
    command_list->EndQuery(timestamp_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_index * 2U + 1U);
    command_list->ResolveQueryData(timestamp_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_index * 2U, 2, frame.timestamp_readback.Get(), 0);
    frame.timestamps_pending = true;
    check_device_hresult(command_list->Close(), command_list, "ID3D12GraphicsCommandList::Close");
    ID3D12CommandList* lists[]{command_list.Get()};
    direct_queue.execute(lists, 1);

    const UINT sync_interval = options.vsync ? 1U : 0U;
    const UINT present_flags = !options.vsync && tearing_supported ? DXGI_PRESENT_ALLOW_TEARING : 0U;
    const auto present_result = swap_chain->Present(sync_interval, present_flags);
    check_device_hresult(present_result, device, "IDXGISwapChain::Present");
    frame.fence_value = direct_queue.signal();
    ++frame_number;
    if (present_result == DXGI_STATUS_OCCLUDED)
    {
        occluded = true;
        return false;
    }
    return true;
}

} // namespace eaw
