// Per-frame state (camera, lighting, instances, debug lines, picks) and render().

#include "d3d12_renderer_impl.hpp"

namespace eaw {

void D3d12Renderer::Impl::collect_pick(FrameContext& frame)
{
    if (frame.picks.empty())
    {
        return;
    }
    const D3D12_RANGE read_range{0, frame.picks.size() * D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT};
    void* mapped{};
    check_hresult(frame.pick_readback->Map(0, &read_range, &mapped), "Map(pick readback)");
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
                                           return finite(line.from) && finite(line.to) && finite(line.color);
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
                                           return finite(particle.position) && finite(particle.color) && finite(particle.uv_rect) && finite(particle.axis) && std::isfinite(particle.half_size) &&
                                                  std::isfinite(particle.rotation) && std::isfinite(particle.tail);
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
        check_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.particle_buffer)),
                      "CreateCommittedResource(frame particles)");
        set_name(frame.particle_buffer.Get(), L"EAWEngine frame particles");
        const D3D12_RANGE no_read{0, 0};
        check_hresult(frame.particle_buffer->Map(0, &no_read, reinterpret_cast<void**>(&frame.particle_buffer_mapped)), "Map(frame particles)");
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
        EAW_ASSERT_MSG(finite(instance.transform) && finite(instance.tint) && finite(instance.team_color), "visual instance " + std::to_string(index) + " has non-finite values");
    }
    dynamic_instances.emplace(instances.begin(), instances.end());
}

void D3d12Renderer::Impl::batch_instances(const std::vector<VisualInstance>& instances, std::vector<GpuInstance>& gpu_instances, std::vector<GpuDraw>& draws) const
{
    std::vector<std::uint32_t> order(instances.size());
    for (std::uint32_t index = 0; index < order.size(); ++index)
    {
        order[index] = index;
    }
    const auto range_key = [&](const std::uint32_t index)
    {
        return static_cast<std::size_t>(instances[index].mesh) * 2U + (instances[index].background ? 1U : 0U);
    };
    std::ranges::stable_sort(order, {}, range_key);
    gpu_instances.clear();
    draws.clear();
    std::size_t range_start = 0;
    for (std::size_t position = 0; position <= order.size(); ++position)
    {
        if (position < order.size())
        {
            const auto& instance = instances[order[position]];
            gpu_instances.push_back({instance.transform, instance.tint, instance.team_color, instance.bone_offset,
                                     (instance.background ? background_instance_flag : 0U) | (instance.highlighted ? highlighted_instance_flag : 0U), order[position] + 1U, 0U});
        }
        if (position == order.size() || (position > range_start && range_key(order[position]) != range_key(order[range_start])))
        {
            if (position > range_start)
            {
                const auto& first = instances[order[range_start]];
                for (auto draw : scene_mesh_draws[first.mesh])
                {
                    draw.first_instance = static_cast<std::uint32_t>(range_start);
                    draw.instance_count = static_cast<std::uint32_t>(position - range_start);
                    draw.background = first.background;
                    draws.push_back(draw);
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
    if (gpu_instances.empty())
    {
        gpu_instances.push_back({visual_identity_transform, {1.0F, 1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F, 1.0F}, 0, 0, 0, 0});
    }
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
        check_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.instance_buffer)),
                      "CreateCommittedResource(frame instances)");
        set_name(frame.instance_buffer.Get(), L"EAWEngine frame instances");
        const D3D12_RANGE no_read{0, 0};
        check_hresult(frame.instance_buffer->Map(0, &no_read, reinterpret_cast<void**>(&frame.instance_buffer_mapped)), "Map(frame instances)");
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
    const auto finite = [](const std::array<float, 3>& value)
    {
        return std::ranges::all_of(value,
                                   [](const float component)
                                   {
                                       return std::isfinite(component);
                                   });
    };
    const auto valid_light = [&](const VisualDirectionalLight& light)
    {
        const auto& direction = light.direction;
        return finite(direction) && finite(light.color) && direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2] > 1.0e-12F;
    };
    EAW_ASSERT_MSG(valid_light(next.key) && valid_light(next.fills[0]) && valid_light(next.fills[1]) && finite(next.key_specular) && finite(next.ambient),
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
    const auto finite = [](const std::array<float, 3>& value)
    {
        return std::ranges::all_of(value,
                                   [](const float component)
                                   {
                                       return std::isfinite(component);
                                   });
    };
    EAW_ASSERT_MSG(finite(next.eye) && finite(next.target) && finite(next.up) && std::isfinite(next.vertical_fov_degrees) && std::isfinite(next.near_plane) && std::isfinite(next.far_plane),
                   "visual camera contains non-finite values");
    EAW_ASSERT_MSG(next.near_plane > 0.0F && next.far_plane > next.near_plane * 1.0001F && next.vertical_fov_degrees > 1.0F && next.vertical_fov_degrees < 179.0F,
                   "visual camera has an invalid projection (near " + std::to_string(next.near_plane) + ", far " + std::to_string(next.far_plane) + ", fov " +
                       std::to_string(next.vertical_fov_degrees) + ")");
    const std::array<float, 3> forward{next.target[0] - next.eye[0], next.target[1] - next.eye[1], next.target[2] - next.eye[2]};
    const std::array<float, 3> side{next.up[1] * forward[2] - next.up[2] * forward[1], next.up[2] * forward[0] - next.up[0] * forward[2], next.up[0] * forward[1] - next.up[1] * forward[0]};
    EAW_ASSERT_MSG(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2] > 1.0e-12F && side[0] * side[0] + side[1] * side[1] + side[2] * side[2] > 1.0e-12F,
                   "visual camera eye, target, and up do not define a view");
    camera = next;
}

DirectX::XMFLOAT4X4 D3d12Renderer::Impl::camera_matrix() const
{
    using namespace DirectX;
    const auto eye = XMVectorSet(camera.eye[0], camera.eye[1], camera.eye[2], 1.0F);
    const auto target = XMVectorSet(camera.target[0], camera.target[1], camera.target[2], 1.0F);
    const auto up = XMVectorSet(camera.up[0], camera.up[1], camera.up[2], 0.0F);
    const auto view = XMMatrixLookAtLH(eye, target, up);
    const auto aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1U));
    const auto projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(camera.vertical_fov_degrees), aspect, camera.near_plane, camera.far_plane);
    DirectX::XMFLOAT4X4 result;
    XMStoreFloat4x4(&result, XMMatrixTranspose(view * projection));
    return result;
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
        }
    }
    if (occluded)
    {
        const auto test_result = swap_chain->Present(0, DXGI_PRESENT_TEST);
        if (test_result == DXGI_STATUS_OCCLUDED)
        {
            return false;
        }
        check_device_result(test_result, "IDXGISwapChain::Present(test)");
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
    ensure_bone_capacity(frame, bone_transforms.size());
    std::memcpy(frame.bone_buffer_mapped, bone_transforms.data(), bone_transforms.size() * sizeof(bone_transforms.front()));
    stream_scene_textures();
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
    constants.view_projection = camera_matrix();
    constants.camera_position_time = {camera.eye[0], camera.eye[1], camera.eye[2], scene_time};
    const auto unit = [](const std::array<float, 3>& value)
    {
        const auto length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
        return std::array<float, 4>{value[0] / length, value[1] / length, value[2] / length, 0.0F};
    };
    const auto rgb = [](const std::array<float, 3>& value)
    {
        return std::array<float, 4>{value[0], value[1], value[2], 0.0F};
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
        // The view basis of XMMatrixLookAtLH: right = up x forward, up' = forward x right.
        const std::array<float, 3> forward{camera.target[0] - camera.eye[0], camera.target[1] - camera.eye[1], camera.target[2] - camera.eye[2]};
        const auto cross = [](const std::array<float, 3>& a, const std::array<float, 3>& b)
        {
            return std::array<float, 3>{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
        };
        const auto right = unit(cross(camera.up, forward));
        const auto view_up = unit(cross(forward, {right[0], right[1], right[2]}));
        constants.camera_right = right;
        constants.camera_up = view_up;
    }
    constants.post_parameters = {options.bloom_threshold, options.bloom_strength, 0.15F, 0.0F};
    std::memcpy(frame.constant_buffer_mapped, &constants, sizeof(constants));

    check_hresult(frame.allocator->Reset(), "ID3D12CommandAllocator::Reset");
    check_hresult(command_list->Reset(frame.allocator.Get(), opaque_pipeline.Get()), "ID3D12GraphicsCommandList::Reset");
    command_list->EndQuery(timestamp_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_index * 2U);

    if (!pending_transitions.empty())
    {
        command_list->ResourceBarrier(static_cast<UINT>(pending_transitions.size()), pending_transitions.data());
        pending_transitions.clear();
    }

    auto to_render_target = transition_barrier(frame.back_buffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    command_list->ResourceBarrier(1, &to_render_target);

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
    // With bloom the scene renders into its own target, and the composite fills the back buffer.
    const bool bloom = options.bloom;
    const auto scene_target = bloom ? post_rtv(0) : render_target;
    const std::array targets{scene_target, pick_rtv_heap->GetCPUDescriptorHandleForHeapStart()};
    command_list->OMSetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), FALSE, &depth_target);
    constexpr std::array clear_color{0.018F, 0.031F, 0.043F, 1.0F};
    constexpr std::array clear_pick{0.0F, 0.0F, 0.0F, 0.0F};
    command_list->ClearRenderTargetView(scene_target, clear_color.data(), 0, nullptr);
    command_list->ClearRenderTargetView(targets[1], clear_pick.data(), 0, nullptr);
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

    if (bloom)
    {
        // Bright pass at reduced size, two horizontal-then-vertical blur rounds ping-ponging between
        // the bloom targets, then scene plus bloom into the back buffer. Every target returns to
        // RENDER_TARGET by the end of the frame.
        const auto barrier = [&](ID3D12Resource* resource, const D3D12_RESOURCE_STATES before, const D3D12_RESOURCE_STATES after)
        {
            auto transition = transition_barrier(resource, before, after);
            command_list->ResourceBarrier(1, &transition);
        };
        const auto pass = [&](ID3D12PipelineState* pipeline, const D3D12_CPU_DESCRIPTOR_HANDLE target, const std::uint32_t source_slot, const std::uint32_t parameter)
        {
            command_list->OMSetRenderTargets(1, &target, FALSE, nullptr);
            command_list->SetPipelineState(pipeline);
            const std::array<std::uint32_t, 2> post_constants{source_slot, parameter};
            command_list->SetGraphicsRoot32BitConstants(5, 2, post_constants.data(), 0);
            command_list->DrawInstanced(3, 1, 0, 0);
            ++draw_calls;
        };
        constexpr auto shader_read = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        constexpr auto target_write = D3D12_RESOURCE_STATE_RENDER_TARGET;
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_VIEWPORT bloom_viewport{};
        bloom_viewport.Width = static_cast<float>(bloom_width);
        bloom_viewport.Height = static_cast<float>(bloom_height);
        bloom_viewport.MaxDepth = 1.0F;
        const D3D12_RECT bloom_scissor{0, 0, static_cast<LONG>(bloom_width), static_cast<LONG>(bloom_height)};
        command_list->RSSetViewports(1, &bloom_viewport);
        command_list->RSSetScissorRects(1, &bloom_scissor);
        barrier(scene_color.Get(), target_write, shader_read);
        pass(bloom_extract_pipeline.Get(), post_rtv(1), post_slots[0], 0U);
        for (int round = 0; round < 2; ++round)
        {
            barrier(bloom_targets[0].Get(), target_write, shader_read);
            pass(bloom_blur_pipeline.Get(), post_rtv(2), post_slots[1], 0U);
            barrier(bloom_targets[1].Get(), target_write, shader_read);
            barrier(bloom_targets[0].Get(), shader_read, target_write);
            pass(bloom_blur_pipeline.Get(), post_rtv(1), post_slots[2], 1U);
            barrier(bloom_targets[1].Get(), shader_read, target_write);
        }
        barrier(bloom_targets[0].Get(), target_write, shader_read);
        command_list->RSSetViewports(1, &viewport);
        command_list->RSSetScissorRects(1, &scissor);
        pass(bloom_composite_pipeline.Get(), render_target, post_slots[0], post_slots[1]);
        barrier(bloom_targets[0].Get(), shader_read, target_write);
        barrier(scene_color.Get(), shader_read, target_write);
        // Debug lines and the overlay draw over the composite, with the scene's depth and pick target.
        const std::array final_targets{render_target, pick_rtv_heap->GetCPUDescriptorHandleForHeapStart()};
        command_list->OMSetRenderTargets(static_cast<UINT>(final_targets.size()), final_targets.data(), FALSE, &depth_target);
    }

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
    last_draw_calls = draw_calls;
    last_triangles = triangles;
    last_instances = instances_drawn;
    if ((!overlay_lines.empty() || selection_rectangle || overlay_ui) && overlay_backend_initialized)
    {
        render_overlay(render_target);
    }

    // Requested picks copy one texel each (up to a per-frame limit; the rest wait for the next
    // frame); they are read when this frame's fence completes.
    if (!pending_picks.empty())
    {
        auto to_copy = transition_barrier(pick_target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        command_list->ResourceBarrier(1, &to_copy);
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
        auto to_target = transition_barrier(pick_target.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        command_list->ResourceBarrier(1, &to_target);
    }

    auto to_present = transition_barrier(frame.back_buffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    command_list->ResourceBarrier(1, &to_present);
    command_list->EndQuery(timestamp_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_index * 2U + 1U);
    command_list->ResolveQueryData(timestamp_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_index * 2U, 2, frame.timestamp_readback.Get(), 0);
    frame.timestamps_pending = true;
    check_hresult(command_list->Close(), "ID3D12GraphicsCommandList::Close");
    ID3D12CommandList* lists[]{command_list.Get()};
    direct_queue.execute(lists, 1);

    const UINT sync_interval = options.vsync ? 1U : 0U;
    const UINT present_flags = !options.vsync && tearing_supported ? DXGI_PRESENT_ALLOW_TEARING : 0U;
    const auto present_result = swap_chain->Present(sync_interval, present_flags);
    check_device_result(present_result, "IDXGISwapChain::Present");
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
