// Initial assets, uploads, scene loading, texture streaming and residency, materials.

#include "d3d12_renderer_impl.hpp"

namespace eaw {
namespace {

// Whether a mesh's default range and every state's levels lie within `draw_count` draws.
bool mesh_ranges_inside(const VisualMesh& mesh, const std::size_t draw_count)
{
    const auto inside = [&](const std::uint32_t first, const std::uint32_t count)
    {
        return static_cast<std::uint64_t>(first) + count <= draw_count;
    };
    if (!inside(mesh.first_draw, mesh.draw_count))
    {
        return false;
    }
    return std::ranges::all_of(mesh.states,
                               [&](const VisualMeshState& state)
                               {
                                   return std::ranges::all_of(state.lods,
                                                              [&](const VisualMeshLod& level)
                                                              {
                                                                  return inside(level.first_draw, level.draw_count);
                                                              });
                               });
}

} // namespace

// A mesh's draws by state and level (its default range alone when it lists no states), with
// `gpu_draw(index)` turning a scene draw into a GPU draw, and its bounding sphere.
template <class GpuDrawOf> D3d12Renderer::Impl::MeshLevels D3d12Renderer::Impl::mesh_levels(const VisualMesh& mesh, const GpuDrawOf& gpu_draw)
{
    MeshLevels levels;
    const auto run = [&](const std::uint32_t first, const std::uint32_t count)
    {
        std::vector<GpuDraw> draws;
        draws.reserve(count);
        for (auto index = first; index < first + count; ++index)
        {
            draws.push_back(gpu_draw(index));
        }
        return draws;
    };
    // The default range comes first, so draws[0][0] is what the static path draws.
    levels.alternates.push_back(0);
    levels.draws.push_back({run(mesh.first_draw, mesh.draw_count)});
    for (const auto& state : mesh.states)
    {
        std::vector<std::vector<GpuDraw>> state_levels;
        for (const auto& level : state.lods)
        {
            state_levels.push_back(run(level.first_draw, level.draw_count));
        }
        if (state_levels.empty())
        {
            continue;
        }
        if (state.alternate == 0 && levels.draws.size() == 1 && levels.alternates.size() == 1)
        {
            // State 0's own levels replace the default range (its first level is that range).
            levels.draws.front() = std::move(state_levels);
            continue;
        }
        levels.alternates.push_back(state.alternate);
        levels.draws.push_back(std::move(state_levels));
    }
    const Vector3 minimum{mesh.bounds_minimum[0], mesh.bounds_minimum[1], mesh.bounds_minimum[2]};
    const Vector3 maximum{mesh.bounds_maximum[0], mesh.bounds_maximum[1], mesh.bounds_maximum[2]};
    const bool valid = minimum.x <= maximum.x && minimum.y <= maximum.y && minimum.z <= maximum.z;
    levels.center = valid ? (minimum + maximum) * 0.5F : Vector3{};
    levels.radius = valid ? length(maximum - minimum) * 0.5F : 0.0F;
    return levels;
}

void D3d12Renderer::Impl::create_initial_assets()
{
    auto upload = begin_upload(L"EAWEngine validation upload");
    constexpr std::array<VisualVertex, 3> vertices{{
        {{-0.78F, -0.68F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}},
        {{0.0F, 0.78F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.5F, 0.0F}},
        {{0.78F, -0.68F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F}},
    }};
    const auto vertex_bytes = std::as_bytes(std::span(vertices));
    vertex_buffer = upload_buffer(upload, vertex_bytes, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, L"EAWEngine validation vertex buffer");
    vertex_buffer_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
    vertex_buffer_view.SizeInBytes = static_cast<UINT>(vertex_bytes.size());
    vertex_buffer_view.StrideInBytes = sizeof(VisualVertex);

    constexpr std::uint32_t texture_width = 8;
    constexpr std::uint32_t texture_height = 8;
    std::array<std::uint32_t, texture_width * texture_height> pixels{};
    for (std::uint32_t y = 0; y < texture_height; ++y)
    {
        for (std::uint32_t x = 0; x < texture_width; ++x)
        {
            const bool bright = ((x / 2) + (y / 2)) % 2 == 0;
            pixels[y * texture_width + x] = bright ? 0xFF50D5DBU : 0xFF17242EU;
        }
    }
    VisualTexture fallback;
    fallback.name = "validation checker";
    fallback.format = VisualTextureFormat::rgba8_unorm;
    VisualTextureMip mip;
    mip.width = texture_width;
    mip.height = texture_height;
    mip.row_pitch = texture_width * sizeof(std::uint32_t);
    mip.slice_pitch = sizeof(pixels);
    const auto pixel_bytes = std::as_bytes(std::span(pixels));
    mip.data.assign(pixel_bytes.begin(), pixel_bytes.end());
    fallback.mips.push_back(std::move(mip));
    texture = upload_texture(upload, fallback, checker_texture_slot);
    const auto solid_texture = [](const std::string& name, const std::uint32_t pixel)
    {
        VisualTexture result;
        result.name = name;
        result.format = VisualTextureFormat::rgba8_unorm;
        VisualTextureMip level;
        level.width = level.height = 1;
        level.row_pitch = level.slice_pitch = sizeof(pixel);
        const auto bytes = std::as_bytes(std::span(&pixel, 1));
        level.data.assign(bytes.begin(), bytes.end());
        result.mips.push_back(std::move(level));
        return result;
    };
    fallback_white_texture = upload_texture(upload, solid_texture("fallback white", 0xFFFFFFFFU), white_texture_slot);
    // (0.5, 0.5, 1.0) encodes an unperturbed tangent-space normal; alpha 1 keeps full gloss.
    fallback_normal_texture = upload_texture(upload, solid_texture("fallback flat normal", 0xFFFF8080U), flat_normal_texture_slot);
    GpuMaterial checker_material;
    checker_material.base_texture = checker_texture_slot;
    identity_material_buffer = upload_buffer(upload, std::as_bytes(std::span(&checker_material, 1)), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, L"EAWEngine validation material buffer");
    const GpuInstance identity{Matrix4::identity(), {1.0F, 1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F, 1.0F}, 0, 0, 0, 0};
    identity_instance_buffer = upload_buffer(upload, std::as_bytes(std::span(&identity, 1)), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"EAWEngine identity instance buffer");
    submit_upload(std::move(upload));
}

auto D3d12Renderer::Impl::begin_upload(const std::wstring_view name) -> UploadBatch
{
    UploadBatch result;
    check_device_hresult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&result.allocator)), device, "CreateCommandAllocator(copy)");
    check_device_hresult(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, result.allocator.Get(), nullptr, IID_PPV_ARGS(&result.list)), device, "CreateCommandList(copy)");
    set_name(result.allocator.Get(), std::wstring(name) + L" allocator");
    set_name(result.list.Get(), std::wstring(name) + L" command list");
    return result;
}

void D3d12Renderer::Impl::submit_upload(UploadBatch upload)
{
    pending_uploads.reserve(pending_uploads.size() + 1U);
    pending_transitions.reserve(pending_transitions.size() + upload.transitions.size());
    check_device_hresult(upload.list->Close(), upload.list, "close upload command list");
    ID3D12CommandList* lists[]{upload.list.Get()};
    copy_queue.execute(lists, 1);
    upload.fence_value = copy_queue.signal();

    // Queue-to-queue synchronization keeps upload completion off the CPU's
    // critical path. The direct queue transitions and consumes the data.
    direct_queue.wait_gpu(copy_queue, upload.fence_value);
    pending_transitions.insert(pending_transitions.end(), upload.transitions.begin(), upload.transitions.end());
    pending_uploads.push_back(std::move(upload));
}

void D3d12Renderer::Impl::retire_uploaded(std::vector<ComPtr<ID3D12Resource>> resources, const std::uint64_t fence_value)
{
    std::erase(resources, nullptr);
    if (resources.empty())
    {
        return;
    }
    std::erase_if(pending_transitions,
                  [&](const D3D12_RESOURCE_BARRIER& barrier)
                  {
                      return barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION && std::ranges::any_of(resources,
                                                                                                           [&](const ComPtr<ID3D12Resource>& resource)
                                                                                                           {
                                                                                                               return resource.Get() == barrier.Transition.pResource;
                                                                                                           });
                  });
    retired_resources.push(std::move(resources), fence_value);
}

ComPtr<ID3D12Resource> D3d12Renderer::Impl::upload_buffer(UploadBatch& upload, const std::span<const std::byte> bytes, const D3D12_RESOURCE_STATES final_state, const std::wstring_view name)
{
    EAW_ASSERT_MSG(!bytes.empty(), "cannot upload an empty D3D12 buffer");
    const auto description = buffer_description(bytes.size());
    const auto default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    const auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D12Resource> staging;
    check_device_hresult(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)), device,
                         "CreateCommittedResource(default buffer)");
    check_device_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging)), device,
                         "CreateCommittedResource(upload buffer)");
    set_name(resource.Get(), name);
    set_name(staging.Get(), std::wstring(name) + L" staging");
    void* mapped{};
    const D3D12_RANGE no_read{0, 0};
    check_device_hresult(staging->Map(0, &no_read, &mapped), staging, "Map(upload buffer)");
    std::memcpy(mapped, bytes.data(), bytes.size());
    staging->Unmap(0, nullptr);
    upload.list->CopyBufferRegion(resource.Get(), 0, staging.Get(), 0, bytes.size());
    upload.resources.push_back(std::move(staging));
    upload.transitions.push_back(transition_barrier(resource.Get(), D3D12_RESOURCE_STATE_COMMON, final_state));
    return resource;
}

Result<D3D12_RESOURCE_DESC> D3d12Renderer::Impl::texture_description(const VisualTexture& source) const
{
    if (source.mips.empty() || source.mips.front().width == 0 || source.mips.front().height == 0)
    {
        return make_error("visual texture '" + source.name + "' has no mip levels or a zero extent");
    }
    const auto format = dxgi_format(source.format);
    if (format == DXGI_FORMAT_UNKNOWN || source.mips.size() > std::numeric_limits<std::uint16_t>::max())
    {
        return make_error("visual texture '" + source.name + "' has an unsupported format or mip count");
    }
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = source.mips.front().width;
    description.Height = source.mips.front().height;
    description.DepthOrArraySize = 1;
    description.MipLevels = static_cast<std::uint16_t>(source.mips.size());
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    std::vector<UINT> row_counts(source.mips.size());
    std::vector<UINT64> row_sizes(source.mips.size());
    UINT64 upload_size{};
    device->GetCopyableFootprints(&description, 0, static_cast<UINT>(source.mips.size()), 0, nullptr, row_counts.data(), row_sizes.data(), &upload_size);
    if (upload_size == std::numeric_limits<UINT64>::max())
    {
        return make_error("visual texture '" + source.name + "' has a mip chain D3D12 cannot lay out");
    }
    for (std::size_t mip_index = 0; mip_index < source.mips.size(); ++mip_index)
    {
        const auto& mip = source.mips[mip_index];
        if (mip.row_pitch < row_sizes[mip_index] || mip.data.size() < static_cast<std::uint64_t>(mip.row_pitch) * row_counts[mip_index])
        {
            return make_error("visual texture '" + source.name + "' mip " + std::to_string(mip_index) + " is shorter than its D3D12 footprint");
        }
    }
    return description;
}

ComPtr<ID3D12Resource> D3d12Renderer::Impl::upload_texture(UploadBatch& upload, const VisualTexture& source, const std::uint32_t descriptor_index)
{
    EAW_ASSERT_MSG(descriptor_index < maximum_scene_textures, "texture descriptor slot is outside the shader table");
    // Scene textures were validated by load_scene; the fallbacks are built by the renderer.
    const auto described = texture_description(source);
    EAW_ASSERT_MSG(described.ok(), described.ok() ? std::string{} : described.error().message);
    const auto& description = described.value();
    const auto format = description.Format;
    const auto default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> resource;
    check_device_hresult(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &description, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)), device,
                         "CreateCommittedResource(scene texture)");
    set_name(resource.Get(), std::wstring(source.name.begin(), source.name.end()));

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(source.mips.size());
    std::vector<UINT> row_counts(source.mips.size());
    std::vector<UINT64> row_sizes(source.mips.size());
    UINT64 upload_size{};
    device->GetCopyableFootprints(&description, 0, static_cast<UINT>(source.mips.size()), 0, footprints.data(), row_counts.data(), row_sizes.data(), &upload_size);
    const auto upload_heap = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
    const auto upload_description = buffer_description(upload_size);
    ComPtr<ID3D12Resource> staging;
    check_device_hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &upload_description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging)), device,
                         "CreateCommittedResource(texture staging)");
    set_name(staging.Get(), L"EAWEngine texture staging");

    std::byte* mapped{};
    const D3D12_RANGE no_read{0, 0};
    check_device_hresult(staging->Map(0, &no_read, reinterpret_cast<void**>(&mapped)), staging, "Map(texture staging)");
    for (std::size_t mip_index = 0; mip_index < source.mips.size(); ++mip_index)
    {
        const auto& mip = source.mips[mip_index];
        const auto required_size = mip.row_pitch * row_counts[mip_index];
        EAW_ASSERT_MSG(mip.data.size() >= required_size && mip.row_pitch >= row_sizes[mip_index], "visual texture mip data is shorter than its D3D12 footprint");
        for (UINT row = 0; row < row_counts[mip_index]; ++row)
        {
            std::memcpy(mapped + footprints[mip_index].Offset + static_cast<std::size_t>(row) * footprints[mip_index].Footprint.RowPitch,
                        mip.data.data() + static_cast<std::size_t>(row) * mip.row_pitch, static_cast<std::size_t>(row_sizes[mip_index]));
        }

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = resource.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = static_cast<UINT>(mip_index);
        D3D12_TEXTURE_COPY_LOCATION source_location{};
        source_location.pResource = staging.Get();
        source_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source_location.PlacedFootprint = footprints[mip_index];
        upload.list->CopyTextureRegion(&destination, 0, 0, 0, &source_location, nullptr);
    }
    staging->Unmap(0, nullptr);
    upload.resources.push_back(std::move(staging));
    upload.transitions.push_back(transition_barrier(resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = static_cast<UINT>(source.mips.size());
    auto handle = shader_heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(descriptor_index) * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device->CreateShaderResourceView(resource.Get(), &view, handle);
    return resource;
}

Status D3d12Renderer::Impl::load_scene(VisualScene scene)
{
    if (scene.vertices.empty() || scene.indices.empty() || scene.draws.empty())
    {
        return make_error("visual scene contains no renderable geometry");
    }
    if (scene.textures.size() > texture_slots.capacity())
    {
        return make_error("visual scene has " + std::to_string(scene.textures.size()) + " textures; the D3D12 texture table holds " + std::to_string(texture_slots.capacity()));
    }
    if (scene.bone_count > maximum_bone_transforms)
    {
        return make_error("visual scene has " + std::to_string(scene.bone_count) + " bones; the D3D12 bone palette holds " + std::to_string(maximum_bone_transforms));
    }
    if ((scene.meshes.empty() || scene.instances.empty()) && scene.terrain.vertices.empty())
    {
        return make_error("visual scene has no meshes, instances, or terrain");
    }
    if (scene.instances.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return make_error("visual scene has too many instances for one D3D12 instance buffer");
    }
    for (std::size_t index = 0; index < scene.meshes.size(); ++index)
    {
        const auto& mesh = scene.meshes[index];
        if (!mesh_ranges_inside(mesh, scene.draws.size()))
        {
            return make_error("visual mesh " + std::to_string(index) + " (" + mesh.name + ") references draws outside the scene");
        }
    }
    for (std::size_t index = 0; index < scene.instances.size(); ++index)
    {
        const auto& instance = scene.instances[index];
        if (instance.mesh >= scene.meshes.size())
        {
            return make_error("visual instance " + std::to_string(index) + " references a missing mesh");
        }
        if (static_cast<std::uint64_t>(instance.bone_offset) + scene.meshes[instance.mesh].bone_count > std::max(scene.bone_count, 1U))
        {
            return make_error("visual instance " + std::to_string(index) + " bone range exceeds the scene bone palette");
        }
        if (!is_finite(instance.transform))
        {
            return make_error("visual instance " + std::to_string(index) + " has a non-finite transform");
        }
    }

    // Group instances by mesh so each mesh's draws cover one contiguous instance range.
    std::vector<std::uint32_t> order(scene.instances.size());
    for (std::uint32_t index = 0; index < order.size(); ++index)
    {
        order[index] = index;
    }
    // Ranges are keyed by mesh and layer: range index = mesh * 2 + (background ? 1 : 0).
    const auto range_key = [&](const std::uint32_t index)
    {
        const auto& instance = scene.instances[index];
        return static_cast<std::size_t>(instance.mesh) * 2U + (instance.background ? 1U : 0U);
    };
    std::ranges::stable_sort(order, {}, range_key);
    std::vector<GpuInstance> gpu_instances;
    gpu_instances.reserve(order.size());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> mesh_ranges(scene.meshes.size() * 2U, {0U, 0U});
    for (const auto index : order)
    {
        const auto& instance = scene.instances[index];
        auto& range = mesh_ranges[range_key(index)];
        if (range.second == 0)
        {
            range.first = static_cast<std::uint32_t>(gpu_instances.size());
        }
        ++range.second;
        gpu_instances.push_back(
            {instance.transform, instance.tint, instance.team_color, instance.bone_offset,
             (instance.background ? background_instance_flag : 0U) | (instance.highlighted ? highlighted_instance_flag : 0U) | (instance.receives_shadows ? 0U : no_shadows_instance_flag), index + 1U,
             0U, instance.bone_offset, instance.transform});
    }

    auto upload = begin_upload(L"EAWEngine scene upload");
    const auto vertex_bytes = std::as_bytes(std::span(scene.vertices));
    const auto index_bytes = std::as_bytes(std::span(scene.indices));
    auto new_vertex_buffer = upload_buffer(upload, vertex_bytes, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, L"EAWEngine scene vertex buffer");
    auto new_index_buffer = upload_buffer(upload, index_bytes, D3D12_RESOURCE_STATE_INDEX_BUFFER, L"EAWEngine scene index buffer");
    if (gpu_instances.empty())
    {
        gpu_instances.push_back({Matrix4::identity(), {1.0F, 1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F, 1.0F}, 0, 0, 0, 0});
    }
    auto new_instance_buffer = upload_buffer(upload, std::as_bytes(std::span(gpu_instances)), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"EAWEngine scene instance buffer");

    // Textures are validated now and stream in from render(). A normal map that has not arrived
    // yet samples the flat normal; everything else samples white.
    std::vector<StreamingTexture> new_textures(scene.textures.size());
    for (std::size_t index = 0; index < scene.textures.size(); ++index)
    {
        EAW_TRY(texture_description(scene.textures[index]));
    }
    for (const auto& material : scene.materials)
    {
        if (material.normal_texture != invalid_visual_texture && material.normal_texture < new_textures.size())
        {
            new_textures[material.normal_texture].fallback_slot = flat_normal_texture_slot;
        }
    }

    std::vector<GpuMaterial> gpu_materials;
    gpu_materials.reserve(std::max<std::size_t>(scene.materials.size(), 1U));
    for (std::size_t index = 0; index < scene.materials.size(); ++index)
    {
        EAW_TRY_ASSIGN(auto material, gpu_material(scene, index));
        gpu_materials.push_back(material);
    }
    if (gpu_materials.empty())
    {
        gpu_materials.emplace_back();
    }
    return commit_scene(scene, std::move(upload), std::move(gpu_materials), std::move(new_textures), std::move(mesh_ranges), std::move(new_vertex_buffer), std::move(new_index_buffer),
                        std::move(new_instance_buffer), vertex_bytes.size(), index_bytes.size());
}

Status D3d12Renderer::Impl::commit_scene(VisualScene& scene, UploadBatch upload, std::vector<GpuMaterial> gpu_materials, std::vector<StreamingTexture> new_textures,
                                         std::vector<std::pair<std::uint32_t, std::uint32_t>> mesh_ranges, ComPtr<ID3D12Resource> new_vertex_buffer, ComPtr<ID3D12Resource> new_index_buffer,
                                         ComPtr<ID3D12Resource> new_instance_buffer, const std::size_t vertex_byte_count, const std::size_t index_byte_count)
{
    auto new_material_buffer = upload_buffer(upload, std::as_bytes(std::span(gpu_materials)), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, L"EAWEngine scene material buffer");
    auto materials_copy = gpu_materials;

    ComPtr<ID3D12Resource> new_terrain_buffer;
    const auto& terrain = scene.terrain;
    if (!terrain.vertices.empty())
    {
        if (terrain.vertices.size() % 3U != 0 || terrain.vertices.size() > std::numeric_limits<std::uint32_t>::max() || terrain.layer_count == 0 ||
            static_cast<std::uint64_t>(terrain.first_material) + terrain.layer_count > scene.materials.size())
        {
            return make_error("visual terrain has an invalid triangle count or layer range");
        }
        for (std::size_t index = 0; index < terrain.vertices.size(); ++index)
        {
            const auto& vertex = terrain.vertices[index];
            const bool finite = is_finite(vertex.position) && is_finite(vertex.normal) &&
                                std::ranges::all_of(vertex.barycentric,
                                                    [](const float value)
                                                    {
                                                        return std::isfinite(value);
                                                    });
            if (!finite || std::ranges::any_of(vertex.layers,
                                               [&](const std::uint32_t layer)
                                               {
                                                   return layer >= terrain.layer_count;
                                               }))
            {
                return make_error("visual terrain vertex " + std::to_string(index) + " has non-finite data or a layer outside the terrain");
            }
        }
        new_terrain_buffer = upload_buffer(upload, std::as_bytes(std::span(terrain.vertices)), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, L"EAWEngine terrain vertex buffer");
    }

    std::vector<std::vector<GpuDraw>> new_mesh_draws(scene.meshes.size());
    std::vector<MeshLevels> new_mesh_levels(scene.meshes.size());
    std::vector<std::uint32_t> new_mesh_bone_counts(scene.meshes.size());
    for (const auto& draw : scene.draws)
    {
        if (draw.material >= scene.materials.size() || static_cast<std::uint64_t>(draw.first_index) + draw.index_count > scene.indices.size())
        {
            return make_error("visual scene draw references data outside its buffers");
        }
    }
    for (std::size_t mesh_index = 0; mesh_index < scene.meshes.size(); ++mesh_index)
    {
        const auto& mesh = scene.meshes[mesh_index];
        new_mesh_bone_counts[mesh_index] = mesh.bone_count;
        const auto gpu_draw = [&](const std::uint32_t draw_index)
        {
            const auto& draw = scene.draws[draw_index];
            auto result = GpuDraw{draw, draw.material, scene.materials[draw.material].blend, 0, 0, false};
            result.water = scene.materials[draw.material].water;
            return result;
        };
        new_mesh_levels[mesh_index] = mesh_levels(mesh, gpu_draw);
        new_mesh_draws[mesh_index] = new_mesh_levels[mesh_index].draws.front().front();
    }
    std::vector<GpuDraw> new_draws;
    new_draws.reserve(scene.draws.size());
    for (std::size_t range_index = 0; range_index < mesh_ranges.size(); ++range_index)
    {
        const auto mesh_index = range_index / 2U;
        const bool background = (range_index % 2U) != 0;
        const auto& mesh = scene.meshes[mesh_index];
        const auto [first_instance, instance_count] = mesh_ranges[range_index];
        if (instance_count == 0)
        {
            continue;
        }
        for (auto draw_index = mesh.first_draw; draw_index < mesh.first_draw + mesh.draw_count; ++draw_index)
        {
            const auto& draw = scene.draws[draw_index];
            if (draw.material >= scene.materials.size() || static_cast<std::uint64_t>(draw.first_index) + draw.index_count > scene.indices.size())
            {
                return make_error("visual scene draw references data outside its buffers");
            }
            new_draws.push_back({draw, draw.material, scene.materials[draw.material].blend, first_instance, instance_count, background});
            new_draws.back().water = scene.materials[draw.material].water;
        }
    }
    // Backgrounds first in scene (mesh) order so later layers paint over earlier ones; then the
    // world by blend mode (opaque, alpha-tested, blended, additive).
    std::ranges::stable_sort(new_draws, {},
                             [](const GpuDraw& draw)
                             {
                                 return draw.background ? 0U : 1U + static_cast<unsigned>(draw.blend);
                             });

    // After every validation: a failed load leaves the current scene and its palette untouched.
    create_bone_buffers(scene.bone_count);
    submit_upload(std::move(upload));
    const auto retire_value = direct_queue.next_value();
    std::vector<ComPtr<ID3D12Resource>> replaced{std::move(scene_vertex_buffer), std::move(scene_index_buffer), std::move(scene_instance_buffer), std::move(scene_material_buffer),
                                                 std::move(terrain_vertex_buffer)};
    for (auto& block : added_blocks)
    {
        replaced.push_back(std::move(block.vertex_buffer));
        replaced.push_back(std::move(block.index_buffer));
    }
    added_blocks.clear();
    retire_uploaded(std::move(replaced), retire_value);
    // A texture whose identity a resident texture of the outgoing scene already has keeps that
    // texture and descriptor slot instead of uploading again; the rest of the outgoing scene's
    // textures retire.
    std::unordered_map<std::string, std::size_t> resident_by_identity;
    for (std::size_t index = 0; index < scene_textures.size(); ++index)
    {
        const auto& entry = scene_textures[index];
        if (!entry.identity.empty() && textures.contains(entry.handle))
        {
            resident_by_identity.try_emplace(entry.identity, index);
        }
    }
    last_reused_textures = 0;
    pending_texture_count = 0;
    // Validation is complete, so the CPU texture data moves into the streaming queue.
    for (std::size_t index = 0; index < new_textures.size(); ++index)
    {
        auto& entry = new_textures[index];
        entry.identity = scene.textures[index].identity;
        if (const auto reused = resident_by_identity.find(entry.identity); !entry.identity.empty() && reused != resident_by_identity.end())
        {
            entry.handle = std::exchange(scene_textures[reused->second].handle, {});
            resident_by_identity.erase(reused);
            ++last_reused_textures;
            continue;
        }
        entry.source = std::move(scene.textures[index]);
        for (const auto& mip : entry.source.mips)
        {
            entry.bytes += mip.data.size();
        }
        ++pending_texture_count;
    }
    retire_textures(scene_textures);
    scene_textures = std::move(new_textures);
    next_streaming_texture = 0;
    scene_vertex_buffer = std::move(new_vertex_buffer);
    scene_index_buffer = std::move(new_index_buffer);
    scene_instance_buffer = std::move(new_instance_buffer);
    scene_material_buffer = std::move(new_material_buffer);
    scene_gpu_materials = std::move(materials_copy);
    terrain_vertex_buffer = std::move(new_terrain_buffer);
    terrain_vertex_count = static_cast<std::uint32_t>(terrain.vertices.size());
    terrain_first_material = terrain.first_material;
    scene_loaded = true;
    scene_bounds_minimum = scene.bounds_minimum;
    scene_bounds_maximum = scene.bounds_maximum;
    // Enhanced fog is densest at the terrain's lowest point (the scene's on maps without terrain,
    // whose bounds can reach far below the ground).
    fog_floor = terrain.vertices.empty() ? scene.bounds_minimum.z : terrain.bounds_minimum.z;
    terrain_minimum = terrain.vertices.empty() ? Vector2{} : Vector2{terrain.bounds_minimum.x, terrain.bounds_minimum.y};
    terrain_maximum = terrain.vertices.empty() ? Vector2{} : Vector2{terrain.bounds_maximum.x, terrain.bounds_maximum.y};
    if (terrain_vertex_buffer)
    {
        terrain_vertex_view.BufferLocation = terrain_vertex_buffer->GetGPUVirtualAddress();
        terrain_vertex_view.SizeInBytes = static_cast<UINT>(terrain.vertices.size() * sizeof(VisualTerrainVertex));
        terrain_vertex_view.StrideInBytes = sizeof(VisualTerrainVertex);
    }
    scene_draws = std::move(new_draws);
    scene_mesh_draws = std::move(new_mesh_draws);
    scene_mesh_levels = std::move(new_mesh_levels);
    scene_mesh_bone_counts = std::move(new_mesh_bone_counts);
    lod_tracker.clear();
    fades_started = 0;
    previous_instances.clear();
    previous_bone_transforms.clear();
    scene_bone_capacity = std::max(scene.bone_count, 1U);
    dynamic_instances.reset();
    highlighted_pick_id = 0;
    scene_vertex_view.BufferLocation = scene_vertex_buffer->GetGPUVirtualAddress();
    scene_vertex_view.SizeInBytes = static_cast<UINT>(vertex_byte_count);
    scene_vertex_view.StrideInBytes = sizeof(VisualVertex);
    scene_index_view.BufferLocation = scene_index_buffer->GetGPUVirtualAddress();
    scene_index_view.SizeInBytes = static_cast<UINT>(index_byte_count);
    scene_index_view.Format = DXGI_FORMAT_R32_UINT;
    return Status::success();
}

Result<std::uint32_t> D3d12Renderer::Impl::add_meshes(VisualScene fragment)
{
    if (!scene_loaded)
    {
        return make_error("add_meshes needs a loaded scene");
    }
    if (fragment.vertices.empty() || fragment.indices.empty() || fragment.draws.empty() || fragment.meshes.empty())
    {
        return make_error("mesh fragment contains no renderable geometry");
    }
    if (!fragment.instances.empty() || !fragment.terrain.vertices.empty())
    {
        return make_error("mesh fragments carry meshes only (no instances or terrain)");
    }
    if (fragment.vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) || fragment.indices.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return make_error("mesh fragment has too many vertices or indices for one geometry block");
    }
    if (scene_mesh_draws.size() + fragment.meshes.size() > std::numeric_limits<std::uint32_t>::max() ||
        scene_gpu_materials.size() + fragment.materials.size() > std::numeric_limits<std::uint32_t>::max() || added_blocks.size() + 1U >= std::numeric_limits<std::uint32_t>::max())
    {
        return make_error("mesh fragment exceeds the scene's mesh, material, or block index range");
    }
    for (std::size_t index = 0; index < fragment.meshes.size(); ++index)
    {
        const auto& mesh = fragment.meshes[index];
        if (!mesh_ranges_inside(mesh, fragment.draws.size()) || mesh.bone_count > maximum_bone_transforms)
        {
            return make_error("fragment mesh " + std::to_string(index) + " (" + mesh.name + ") references draws outside the fragment or has too many bones");
        }
    }
    for (const auto& draw : fragment.draws)
    {
        if (draw.material >= fragment.materials.size() || static_cast<std::uint64_t>(draw.first_index) + draw.index_count > fragment.indices.size() || draw.base_vertex < 0 ||
            static_cast<std::uint64_t>(draw.base_vertex) >= fragment.vertices.size())
        {
            return make_error("fragment draw references data outside the fragment");
        }
    }
    EAW_TRY_ASSIGN(const auto texture_plan, plan_textures(fragment.textures));
    const auto& texture_ids = texture_plan.logical_ids;
    std::vector<GpuMaterial> materials = scene_gpu_materials;
    const auto first_material = static_cast<std::uint32_t>(materials.size());
    for (std::size_t index = 0; index < fragment.materials.size(); ++index)
    {
        EAW_TRY_ASSIGN(auto material, gpu_material(fragment, index, texture_ids));
        materials.push_back(material);
    }

    // Everything is validated: upload and commit.
    auto upload = begin_upload(L"EAWEngine added meshes upload");
    const auto vertex_bytes = std::as_bytes(std::span(fragment.vertices));
    const auto index_bytes = std::as_bytes(std::span(fragment.indices));
    GeometryBlock block;
    block.vertex_buffer = upload_buffer(upload, vertex_bytes, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, L"EAWEngine added vertex buffer");
    block.index_buffer = upload_buffer(upload, index_bytes, D3D12_RESOURCE_STATE_INDEX_BUFFER, L"EAWEngine added index buffer");
    auto new_material_buffer = upload_buffer(upload, std::as_bytes(std::span(materials)), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, L"EAWEngine scene material buffer");
    submit_upload(std::move(upload));
    // Frames in flight may still read the previous material buffer.
    retire_uploaded({std::move(scene_material_buffer)}, direct_queue.next_value());
    scene_material_buffer = std::move(new_material_buffer);
    scene_gpu_materials = std::move(materials);
    block.vertex_view = {block.vertex_buffer->GetGPUVirtualAddress(), static_cast<UINT>(vertex_bytes.size()), sizeof(VisualVertex)};
    block.index_view = {block.index_buffer->GetGPUVirtualAddress(), static_cast<UINT>(index_bytes.size()), DXGI_FORMAT_R32_UINT};
    added_blocks.push_back(std::move(block));
    const auto block_index = static_cast<std::uint32_t>(added_blocks.size());

    const auto first_mesh = static_cast<std::uint32_t>(scene_mesh_draws.size());
    for (const auto& mesh : fragment.meshes)
    {
        const auto gpu_draw = [&](const std::uint32_t draw_index)
        {
            const auto& draw = fragment.draws[draw_index];
            GpuDraw result{draw, first_material + draw.material, fragment.materials[draw.material].blend, 0, 0, false};
            result.block = block_index;
            result.water = fragment.materials[draw.material].water;
            return result;
        };
        auto levels = mesh_levels(mesh, gpu_draw);
        scene_mesh_draws.push_back(levels.draws.front().front());
        scene_mesh_levels.push_back(std::move(levels));
        scene_mesh_bone_counts.push_back(mesh.bone_count);
    }
    // New textures join the streaming queue; normal maps fall back to the flat normal meanwhile.
    commit_textures(fragment.textures, texture_plan);
    for (const auto& material : fragment.materials)
    {
        if (material.normal_texture != invalid_visual_texture && material.normal_texture < texture_ids.size())
        {
            const auto logical = texture_ids[material.normal_texture];
            if (logical >= fallback_texture_count && logical - fallback_texture_count < scene_textures.size())
            {
                auto& entry = scene_textures[logical - fallback_texture_count];
                if (!textures.contains(entry.handle))
                {
                    entry.fallback_slot = flat_normal_texture_slot;
                }
            }
        }
    }
    return first_mesh;
}

auto D3d12Renderer::Impl::plan_textures(const std::span<const VisualTexture> added) const -> Result<TexturePlan>
{
    // An identity already in the scene shares its logical id; the rest are new entries.
    std::unordered_map<std::string, std::uint32_t> logical_by_identity;
    for (std::size_t index = 0; index < scene_textures.size(); ++index)
    {
        if (!scene_textures[index].identity.empty())
        {
            logical_by_identity.try_emplace(scene_textures[index].identity, static_cast<std::uint32_t>(fallback_texture_count + index));
        }
    }
    TexturePlan plan;
    plan.logical_ids.resize(added.size());
    for (std::size_t index = 0; index < added.size(); ++index)
    {
        const auto& identity = added[index].identity;
        if (const auto shared = logical_by_identity.find(identity); !identity.empty() && shared != logical_by_identity.end())
        {
            plan.logical_ids[index] = shared->second;
            continue;
        }
        EAW_TRY(texture_description(added[index]));
        const auto logical = fallback_texture_count + scene_textures.size() + plan.new_sources.size();
        if (logical >= maximum_scene_textures)
        {
            return make_error("adding textures would exceed the " + std::to_string(maximum_scene_textures) + "-entry texture table");
        }
        plan.logical_ids[index] = static_cast<std::uint32_t>(logical);
        if (!identity.empty())
        {
            logical_by_identity.emplace(identity, plan.logical_ids[index]);
        }
        plan.new_sources.push_back(index);
    }
    return plan;
}

void D3d12Renderer::Impl::commit_textures(std::vector<VisualTexture>& added, const TexturePlan& plan)
{
    for (const auto index : plan.new_sources)
    {
        StreamingTexture entry;
        entry.identity = added[index].identity;
        entry.source = std::move(added[index]);
        for (const auto& mip : entry.source.mips)
        {
            entry.bytes += mip.data.size();
        }
        scene_textures.push_back(std::move(entry));
        ++pending_texture_count;
    }
}

Result<std::vector<std::uint32_t>> D3d12Renderer::Impl::add_textures(std::vector<VisualTexture> added)
{
    EAW_ASSERT_MSG(scene_loaded, "add_textures needs a loaded scene");
    EAW_TRY_ASSIGN(const auto plan, plan_textures(added));
    commit_textures(added, plan);
    std::vector<std::uint32_t> indices;
    indices.reserve(plan.logical_ids.size());
    for (const auto logical : plan.logical_ids)
    {
        indices.push_back(logical - fallback_texture_count);
    }
    return indices;
}

void D3d12Renderer::Impl::retire_textures(std::vector<StreamingTexture>& entries)
{
    if (entries.empty())
    {
        return;
    }
    const auto retire_value = direct_queue.next_value();
    std::vector<ComPtr<ID3D12Resource>> resources;
    resources.reserve(entries.size());
    for (auto& entry : entries)
    {
        if (auto texture_entry = textures.erase(entry.handle))
        {
            texture_slots.release(texture_entry->descriptor, retire_value);
            resources.push_back(std::move(texture_entry->resource));
        }
        entry.handle = {};
    }
    entries.clear();
    retire_uploaded(std::move(resources), retire_value);
}

void D3d12Renderer::Impl::stream_scene_textures()
{
    if (next_streaming_texture >= scene_textures.size())
    {
        return;
    }
    reclaim_texture_slots();
    std::vector<std::size_t> streamed;
    std::optional<UploadBatch> upload;
    std::uint64_t submitted_bytes = 0;
    // load_scene validated every queued texture, so an upload here fails only with the device
    // (fatal); there is no partially submitted batch to undo.
    while (next_streaming_texture < scene_textures.size())
    {
        auto& entry = scene_textures[next_streaming_texture];
        if (textures.contains(entry.handle))
        {
            // Reused from the previous scene.
            ++next_streaming_texture;
            continue;
        }
        if (options.texture_upload_budget_bytes != 0 && submitted_bytes != 0 && submitted_bytes + entry.bytes > options.texture_upload_budget_bytes)
        {
            break;
        }
        const auto slot = texture_slots.allocate();
        if (!slot)
        {
            break;
        }
        if (!upload)
        {
            upload = begin_upload(L"EAWEngine texture stream");
        }
        auto resource = upload_texture(*upload, entry.source, *slot);
        entry.handle = textures.insert({std::move(resource), *slot});
        submitted_bytes += entry.bytes;
        streamed.push_back(next_streaming_texture);
        ++next_streaming_texture;
    }
    if (upload)
    {
        submit_upload(std::move(*upload));
    }
    for (const auto index : streamed)
    {
        scene_textures[index].source = {};
    }
    pending_texture_count -= std::min(pending_texture_count, streamed.size());
}

void D3d12Renderer::Impl::write_texture_residency(FrameContext& frame) const
{
    auto* table = frame.residency_mapped;
    for (std::uint32_t slot = 0; slot < fallback_texture_count; ++slot)
    {
        table[slot] = slot;
    }
    for (std::size_t index = 0; index < scene_textures.size(); ++index)
    {
        const auto& entry = scene_textures[index];
        const auto* resident = textures.get(entry.handle);
        table[fallback_texture_count + index] = resident != nullptr ? resident->descriptor : entry.fallback_slot;
    }
}

void D3d12Renderer::Impl::reclaim_texture_slots()
{
    const auto reclaimed = texture_slots.reclaim(direct_queue.completed_value());
    if (reclaimed.empty())
    {
        return;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC null_view{};
    null_view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    null_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    null_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    null_view.Texture2D.MipLevels = 1;
    const auto increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (const auto slot : reclaimed)
    {
        auto handle = shader_heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(slot) * increment;
        device->CreateShaderResourceView(nullptr, &null_view, handle);
    }
}

void D3d12Renderer::Impl::set_bone_transforms(const std::span<const Matrix4> transforms)
{
    EAW_ASSERT_MSG(transforms.size() <= maximum_bone_transforms, "bone palette update exceeds the D3D12 bone palette capacity");
    EAW_ASSERT_MSG(std::ranges::all_of(transforms,
                                       [](const Matrix4& matrix)
                                       {
                                           return is_finite(matrix);
                                       }),
                   "bone palette update has non-finite values");
    bone_transforms.assign(transforms.begin(), transforms.end());
    if (bone_transforms.size() < scene_bone_capacity)
    {
        bone_transforms.resize(scene_bone_capacity, Matrix4::identity());
    }
}

auto D3d12Renderer::Impl::gpu_material(const VisualScene& scene, const std::size_t index, const std::span<const std::uint32_t> texture_ids) -> Result<GpuMaterial>
{
    const auto& material = scene.materials[index];
    const auto slot = [&](const std::uint32_t texture, const std::uint32_t fallback, const char* role) -> Result<std::uint32_t>
    {
        if (texture == invalid_visual_texture)
        {
            return fallback;
        }
        if (texture >= scene.textures.size())
        {
            return make_error("visual material " + std::to_string(index) + " (" + material.name + ") references a missing " + role + " texture");
        }
        return texture_ids.empty() ? fallback_texture_count + texture : texture_ids[texture];
    };
    const auto finite = [](const auto& values)
    {
        return std::ranges::all_of(values,
                                   [](const float value)
                                   {
                                       return std::isfinite(value);
                                   });
    };
    if (!is_finite(material.tint) || !is_finite(material.diffuse) || !is_finite(material.specular) || !is_finite(material.emissive) || !finite(material.uv_scroll) ||
        !std::isfinite(material.alpha_test_threshold) || !std::isfinite(material.uv_scale) || !is_finite(material.planet.atmosphere) || !is_finite(material.planet.city_color) ||
        !std::isfinite(material.planet.atmosphere_power) || !std::isfinite(material.planet.cloud_scroll))
    {
        return make_error("visual material " + std::to_string(index) + " (" + material.name + ") has non-finite parameters");
    }
    GpuMaterial result;
    result.tint = material.tint;
    result.diffuse = material.diffuse;
    result.specular = {material.specular[0], material.specular[1], material.specular[2], material.uv_scale};
    result.emissive = {material.emissive[0], material.emissive[1], material.emissive[2], 0.0F};
    result.uv_scroll = material.uv_scroll;
    result.alpha_cutoff = material.alpha_test_threshold;
    EAW_TRY_ASSIGN(result.base_texture, slot(material.base_color_texture, white_texture_slot, "base color"));
    EAW_TRY_ASSIGN(result.normal_texture, slot(material.normal_texture, flat_normal_texture_slot, "normal"));
    EAW_TRY_ASSIGN(result.gloss_texture, slot(material.gloss_texture, white_texture_slot, "gloss"));
    result.lighting = static_cast<std::uint32_t>(material.lighting);
    result.flags = (material.blend == VisualBlendMode::alpha_test ? material_alpha_test : 0U) | (material.blend == VisualBlendMode::alpha_blend ? material_alpha_blend : 0U) |
                   (material.blend == VisualBlendMode::additive ? material_additive : 0U) | (material.team_color_mask ? material_team_color : 0U) |
                   (material.emissive_mask ? material_emissive_mask : 0U) | (static_cast<std::uint32_t>(material.gloss) << material_gloss_shift);
    if (material.lighting == VisualLightingModel::planet)
    {
        const auto& planet = material.planet;
        result.atmosphere = {planet.atmosphere.r, planet.atmosphere.g, planet.atmosphere.b, planet.atmosphere.a};
        result.city_color_power = {planet.city_color.r, planet.city_color.g, planet.city_color.b, planet.atmosphere_power};
        result.cloud_scroll = planet.cloud_scroll;
        EAW_TRY_ASSIGN(result.cloud_texture, slot(planet.cloud_texture, white_texture_slot, "cloud"));
        result.flags |= planet.cloud_texture != invalid_visual_texture ? material_clouds : 0U;
        EAW_TRY_ASSIGN(result.cloud_normal_texture, slot(planet.cloud_normal_texture, flat_normal_texture_slot, "cloud normal"));
    }
    return result;
}

} // namespace eaw
