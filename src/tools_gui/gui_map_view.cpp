// Map previews: land heightmap and space viewports, object markers, and the large preview window.

#include "gui_internal.hpp"

namespace eaw::tools {
namespace {

ImU32 terrain_height_color(const float height)
{
    struct Stop
    {
        float position;
        std::array<float, 3> color;
    };
    static constexpr std::array stops{Stop{0.00F, {25.0F, 70.0F, 82.0F}},   Stop{0.18F, {38.0F, 105.0F, 83.0F}},   Stop{0.48F, {84.0F, 132.0F, 72.0F}},
                                      Stop{0.72F, {137.0F, 112.0F, 70.0F}}, Stop{0.90F, {151.0F, 143.0F, 120.0F}}, Stop{1.00F, {219.0F, 224.0F, 218.0F}}};
    const auto clamped = std::clamp(height, 0.0F, 1.0F);
    std::size_t upper = 1;
    while (upper + 1 < stops.size() && clamped > stops[upper].position)
    {
        ++upper;
    }
    const auto& left = stops[upper - 1];
    const auto& right = stops[upper];
    const auto mix = (clamped - left.position) / std::max(right.position - left.position, 0.0001F);
    const auto channel = [&](const std::size_t index)
    {
        return static_cast<int>(std::lerp(left.color[index], right.color[index], mix));
    };
    return IM_COL32(channel(0), channel(1), channel(2), 255);
}

ImU32 passability_color(const std::uint32_t value)
{
    static constexpr std::array<ImU32, 8> palette{IM_COL32(48, 170, 117, 255),  IM_COL32(225, 87, 75, 255),  IM_COL32(228, 164, 62, 255),  IM_COL32(76, 140, 207, 255),
                                                  IM_COL32(151, 101, 203, 255), IM_COL32(62, 183, 190, 255), IM_COL32(205, 102, 154, 255), IM_COL32(154, 158, 165, 255)};
    const auto mixed = value ^ (value >> 8U) ^ (value >> 16U) ^ (value >> 24U);
    return palette[mixed % palette.size()];
}

ImU32 object_player_color(const std::uint32_t player)
{
    static constexpr std::array<ImU32, 8> palette{IM_COL32(188, 199, 207, 255), IM_COL32(70, 156, 222, 255),  IM_COL32(228, 88, 76, 255),  IM_COL32(235, 179, 67, 255),
                                                  IM_COL32(112, 195, 130, 255), IM_COL32(166, 111, 215, 255), IM_COL32(67, 190, 194, 255), IM_COL32(219, 116, 175, 255)};
    return palette[player % palette.size()];
}

float object_marker_radius(const TedPlacedObject& object, const float scale)
{
    return std::clamp(std::sqrt(std::max(std::fabs(object.scale), 0.1F)), 0.75F, 2.25F) * 3.5F * scale;
}

void map_object_tooltip(const GuiApp::State& state, const std::size_t object_index)
{
    const auto& object = state.preview_map->placed_objects[object_index];
    ImGui::BeginTooltip();
    const MapObjectPlacement* placement = nullptr;
    if (state.map_object_layout && object_index < state.map_object_placement.size() && state.map_object_placement[object_index] >= 0)
    {
        placement = &state.map_object_layout->placements[static_cast<std::size_t>(state.map_object_placement[object_index])];
    }
    if (placement != nullptr && placement->type != nullptr)
    {
        ImGui::Text("%s  (%s)", placement->type->name.c_str(), placement->type->type.c_str());
        ImGui::TextColored(muted, "%s:%zu", placement->type->source_path.c_str(), placement->type->line);
        if (!placement->model_path.empty())
        {
            ImGui::Text("Model: %s", placement->model_path.c_str());
        }
        else if (!placement->model_reference.empty())
        {
            ImGui::Text("Model: %s (not found)", placement->model_reference.c_str());
        }
        else
        {
            ImGui::TextUnformatted("Model: none for this map type");
        }
    }
    else if (state.map_object_layout)
    {
        ImGui::TextUnformatted("Unresolved object type");
    }
    ImGui::Text("Type CRC: 0x%08X", object.crc);
    ImGui::Text("Instance: %u  |  Player code: %u", object.instance_id, object.player);
    ImGui::Text("Position: %.1f, %.1f, %.1f", object.position.x, object.position.y, object.position.z);
    ImGui::Text("Rotation: %.1f, %.1f, %.1f  |  Scale: %.2f", object.rotation.x, object.rotation.y, object.rotation.z, object.scale);
    ImGui::EndTooltip();
}

// Bounding radius of a placed object's model in world units, or zero without a drawn model.
float map_object_radius(const GuiApp::State& state, const std::size_t object_index)
{
    if (!state.map_object_layout || object_index >= state.map_object_placement.size() || state.map_object_placement[object_index] < 0)
    {
        return 0.0F;
    }
    const auto& placement = state.map_object_layout->placements[static_cast<std::size_t>(state.map_object_placement[object_index])];
    const auto found = state.map_models.find(placement.model_path);
    if (found == state.map_models.end() || !found->second.valid)
    {
        return 0.0F;
    }
    return found->second.radius * std::fabs(state.preview_map->placed_objects[object_index].scale);
}

void render_map_object_status(const GuiApp::State& state)
{
    if (!state.map_object_layout)
    {
        for (const auto& message : state.map_object_warnings)
        {
            ImGui::TextColored(danger, "%s", message.c_str());
        }
        return;
    }
    const auto& layout = *state.map_object_layout;
    const auto drawn = static_cast<std::size_t>(std::ranges::count(state.map_object_drawn, char{1}));
    ImGui::TextColored(muted, "%zu objects drawn as models  |  %zu untyped  |  %zu without model  |  %zu missing model files", drawn, layout.untyped, layout.without_model, layout.missing_model_files);
    if (!state.map_object_warnings.empty() && ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        for (std::size_t index = 0; index < std::min<std::size_t>(state.map_object_warnings.size(), 12); ++index)
        {
            ImGui::TextUnformatted(state.map_object_warnings[index].c_str());
        }
        ImGui::EndTooltip();
    }
}

std::vector<PreviewTriangle> map_preview_triangles(const TedMap& map, const int triangle_budget, const float vertical_scale, const bool show_passability)
{
    std::vector<PreviewTriangle> triangles;
    const auto width = map.terrain_width;
    const auto height = map.terrain_height;
    const auto expected = static_cast<std::size_t>(width) * height;
    if (width < 2 || height < 2 || map.elevation_map.size() < expected)
    {
        return triangles;
    }

    const auto budget = static_cast<std::size_t>(std::max(triangle_budget, 2));
    std::uint32_t stride = 1;
    const auto cell_count = [&](const std::uint32_t step)
    {
        const auto columns = (static_cast<std::size_t>(width - 1U) + step - 1U) / step;
        const auto rows = (static_cast<std::size_t>(height - 1U) + step - 1U) / step;
        return columns * rows * 2U;
    };
    while (cell_count(stride) > budget)
    {
        ++stride;
    }
    triangles.reserve(cell_count(stride));

    const auto elevation_range = std::max(1.0F, static_cast<float>(map.maximum_elevation - map.minimum_elevation));
    const auto horizontal_extent = static_cast<float>(std::max(width - 1U, height - 1U));
    const auto vertical_extent = horizontal_extent * 0.36F * vertical_scale;
    const auto center_x = static_cast<float>(width - 1U) * 0.5F;
    const auto center_y = static_cast<float>(height - 1U) * 0.5F;
    const auto point = [&](const std::uint32_t x, const std::uint32_t y)
    {
        const auto index = static_cast<std::size_t>(y) * width + x;
        const auto normalized = (static_cast<float>(map.elevation_map[index]) - map.minimum_elevation) / elevation_range;
        return Vector3{static_cast<float>(x) - center_x, static_cast<float>(y) - center_y, (normalized - 0.5F) * vertical_extent};
    };
    const auto color = [&](const std::uint32_t x, const std::uint32_t y)
    {
        const auto index = static_cast<std::size_t>(y) * width + x;
        if (show_passability && index < map.passability_map.size())
        {
            return passability_color(map.passability_map[index]);
        }
        const auto normalized = (static_cast<float>(map.elevation_map[index]) - map.minimum_elevation) / elevation_range;
        return terrain_height_color(normalized);
    };

    for (std::uint32_t y = 0; y + 1U < height; y += stride)
    {
        const auto next_y = std::min(y + stride, height - 1U);
        for (std::uint32_t x = 0; x + 1U < width; x += stride)
        {
            const auto next_x = std::min(x + stride, width - 1U);
            const auto a = point(x, y);
            const auto b = point(next_x, y);
            const auto c = point(next_x, next_y);
            const auto d = point(x, next_y);
            const auto first_x = (x + 2U * next_x) / 3U;
            const auto first_y = (2U * y + next_y) / 3U;
            const auto second_x = (2U * x + next_x) / 3U;
            const auto second_y = (y + 2U * next_y) / 3U;
            triangles.push_back({{a, b, c}, color(first_x, first_y)});
            triangles.push_back({{a, c, d}, color(second_x, second_y)});
        }
    }
    return triangles;
}

// Uploads per-model instance lists for the current map. `anchor` maps an object position into
// preview space and `unit_scale` converts model/world units to preview units (1 for space maps,
// terrain samples per world unit for land maps).
void update_map_instances(GuiApp::State& state, const std::function<Vector3(const TedPlacedObject&)>& anchor, const float unit_scale)
{
    const auto& map = *state.preview_map;
    state.map_object_drawn.assign(map.placed_objects.size(), 0);
    std::unordered_map<std::string, std::vector<PreviewInstance>> instances;
    if (state.map_object_layout)
    {
        auto scale = Matrix4::identity();
        scale(0, 0) = scale(1, 1) = scale(2, 2) = unit_scale;
        for (const auto& placement : state.map_object_layout->placements)
        {
            const auto found = state.map_models.find(placement.model_path);
            if (placement.model_path.empty() || found == state.map_models.end() || !found->second.valid)
            {
                continue;
            }
            const auto& object = map.placed_objects[placement.object_index];
            auto transform = placement.transform * scale;
            const auto origin = anchor(object);
            set_translation(transform, origin);
            PreviewInstance instance;
            instance.transform = transform;
            instances[placement.model_path].push_back(instance);
            state.map_object_drawn[placement.object_index] = 1;
        }
    }
    for (auto& [path, model] : state.map_models)
    {
        if (!model.valid)
        {
            continue;
        }
        const auto found = instances.find(path);
        const auto list = found == instances.end() ? std::span<const PreviewInstance>{} : std::span<const PreviewInstance>{found->second};
        static_cast<void>(model.mesh.set_instances(state.renderer.device(), state.renderer.context(), list));
    }
    state.map_object_cached = true;
}

std::vector<const PreviewMesh*> map_object_meshes(const GuiApp::State& state)
{
    std::vector<const PreviewMesh*> meshes;
    for (const auto& [path, model] : state.map_models)
    {
        if (model.valid && model.mesh.ready())
        {
            meshes.push_back(&model.mesh);
        }
    }
    return meshes;
}

void render_space_map_viewport(GuiApp::State& state, const bool reserve_footer)
{
    if (!state.preview_map)
    {
        return;
    }
    const auto& map = *state.preview_map;
    std::vector<std::size_t> positioned;
    positioned.reserve(map.placed_objects.size());
    for (std::size_t index = 0; index < map.placed_objects.size(); ++index)
    {
        if (map.placed_objects[index].has_transform)
        {
            positioned.push_back(index);
        }
    }

    ImGui::Checkbox("Map bounds", &state.map_preview_show_grid);
    ImGui::SameLine();
    ImGui::Checkbox("Objects", &state.map_preview_show_objects);
    ImGui::SameLine();
    if (ImGui::Checkbox("3D altitude", &state.map_preview_show_altitude))
    {
        state.map_object_cached = false;
    }
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat("##space-marker-scale", &state.map_preview_marker_scale, 0.5F, 2.5F, "Marker size: %.2fx");
    if (!state.map_object_cached)
    {
        const auto altitude = state.map_preview_show_altitude;
        update_map_instances(
            state,
            [altitude](const TedPlacedObject& object)
            {
                return Vector3{object.position.x, object.position.y, altitude ? object.position.z : 0.0F};
            },
            1.0F);
    }
    render_map_object_status(state);
    ImGui::TextColored(muted, "%zu positioned objects  |  %zu types  |  %.0f x %.0f bounds", positioned.size(), map.object_types.size(), map.world_width, map.world_height);

    auto canvas_size = ImGui::GetContentRegionAvail();
    if (reserve_footer)
    {
        canvas_size.y -= ImGui::GetFrameHeightWithSpacing();
    }
    canvas_size.x = std::max(canvas_size.x, 120.0F);
    canvas_size.y = std::max(canvas_size.y, 260.0F);
    ImGui::InvisibleButton("space-map-canvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft);
    const auto canvas_min = ImGui::GetItemRectMin();
    const auto canvas_max = ImGui::GetItemRectMax();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        state.map_preview_yaw += ImGui::GetIO().MouseDelta.x * 0.012F;
        state.map_preview_pitch = std::clamp(state.map_preview_pitch - ImGui::GetIO().MouseDelta.y * 0.012F, -1.45F, 1.45F);
    }
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0F)
    {
        state.map_preview_zoom = std::clamp(state.map_preview_zoom * std::pow(1.12F, ImGui::GetIO().MouseWheel), 0.15F, 12.0F);
    }

    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvas_min, canvas_max, IM_COL32(7, 13, 20, 255), 6.0F);
    draw->AddRect(canvas_min, canvas_max, IM_COL32(40, 78, 91, 255), 6.0F);
    if (positioned.empty())
    {
        draw->AddText({canvas_min.x + 14.0F, canvas_min.y + 14.0F}, IM_COL32(150, 164, 174, 255), "No positioned objects were decoded from this space map");
        return;
    }

    Vector3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vector3 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    const auto preview_point = [&](const TedPlacedObject& object)
    {
        return Vector3{object.position.x, object.position.y, state.map_preview_show_altitude ? object.position.z : 0.0F};
    };
    for (const auto index : positioned)
    {
        const auto point = preview_point(map.placed_objects[index]);
        minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y), std::min(minimum.z, point.z)};
        maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y), std::max(maximum.z, point.z)};
    }
    if (map.world_width > 0.0F && map.world_height > 0.0F)
    {
        minimum.x = std::min(minimum.x, map.world_width * -0.5F);
        maximum.x = std::max(maximum.x, map.world_width * 0.5F);
        minimum.y = std::min(minimum.y, map.world_height * -0.5F);
        maximum.y = std::max(maximum.y, map.world_height * 0.5F);
    }
    minimum.z = std::min(minimum.z, 0.0F);
    maximum.z = std::max(maximum.z, 0.0F);
    const Vector3 center{(minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F, (minimum.z + maximum.z) * 0.5F};
    const auto extent = std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z, 0.001F});
    const auto scale = std::min(canvas_size.x, canvas_size.y) * 0.78F / extent * state.map_preview_zoom;
    const auto screen_center = ImVec2{(canvas_min.x + canvas_max.x) * 0.5F, (canvas_min.y + canvas_max.y) * 0.5F};
    const auto yaw_cos = std::cos(state.map_preview_yaw);
    const auto yaw_sin = std::sin(state.map_preview_yaw);
    const auto pitch_cos = std::cos(state.map_preview_pitch);
    const auto pitch_sin = std::sin(state.map_preview_pitch);
    const auto project = [&](const Vector3& point, float* depth = nullptr)
    {
        const auto x = point.x - center.x;
        const auto y = point.y - center.y;
        const auto z = point.z - center.z;
        const auto horizontal = yaw_cos * x + yaw_sin * y;
        const auto forward = -yaw_sin * x + yaw_cos * y;
        const auto vertical = -pitch_sin * forward + pitch_cos * z;
        if (depth != nullptr)
        {
            *depth = pitch_cos * forward + pitch_sin * z;
        }
        return ImVec2{screen_center.x + horizontal * scale, screen_center.y - vertical * scale};
    };

    float largest_model = 0.0F;
    for (const auto& [path, model] : state.map_models)
    {
        largest_model = std::max(largest_model, model.radius);
    }
    std::vector<const PreviewMesh*> meshes;
    if (state.map_preview_show_objects)
    {
        meshes = map_object_meshes(state);
    }
    draw->PushClipRect(canvas_min, canvas_max, true);
    draw_gpu_preview(state, state.map_target, canvas_min, canvas_max, center, state.map_preview_yaw, state.map_preview_pitch, scale, extent * 1.5F + largest_model * 4.0F, meshes);

    struct SpaceMarker
    {
        ImVec2 point;
        ImVec2 ground;
        float depth{};
        float radius{};
        ImU32 color{};
        ImU32 outline{};
        std::size_t object_index{};
        bool visible{true};
    };
    std::vector<SpaceMarker> markers;
    const auto marker_stride = std::max<std::size_t>(1, (positioned.size() + 5999U) / 6000U);
    markers.reserve((positioned.size() + marker_stride - 1U) / marker_stride);
    for (std::size_t shown = 0; state.map_preview_show_objects && shown < positioned.size(); shown += marker_stride)
    {
        const auto object_index = positioned[shown];
        const auto& object = map.placed_objects[object_index];
        const auto point = preview_point(object);
        SpaceMarker marker;
        marker.point = project(point, &marker.depth);
        marker.ground = project({point.x, point.y, 0.0F});
        marker.radius = object_marker_radius(object, state.map_preview_marker_scale);
        marker.color = passability_color(object.crc);
        marker.outline = object_player_color(object.player);
        marker.object_index = object_index;
        // Objects drawn as models keep an invisible hover target sized to their projected bounds.
        if (object_index < state.map_object_drawn.size() && state.map_object_drawn[object_index] != 0)
        {
            marker.visible = false;
            marker.radius = std::max(marker.radius, map_object_radius(state, object_index) * scale);
        }
        markers.push_back(marker);
    }
    std::ranges::sort(markers, {}, &SpaceMarker::depth);

    if (state.map_preview_show_grid)
    {
        const auto grid_color = IM_COL32(72, 120, 139, 75);
        const auto boundary_color = IM_COL32(91, 169, 184, 155);
        const auto left = map.world_width > 0.0F ? map.world_width * -0.5F : minimum.x;
        const auto right = map.world_width > 0.0F ? map.world_width * 0.5F : maximum.x;
        const auto bottom = map.world_height > 0.0F ? map.world_height * -0.5F : minimum.y;
        const auto top = map.world_height > 0.0F ? map.world_height * 0.5F : maximum.y;
        for (int index = 0; index <= 10; ++index)
        {
            const auto t = static_cast<float>(index) / 10.0F;
            const auto x = std::lerp(left, right, t);
            const auto y = std::lerp(bottom, top, t);
            draw->AddLine(project({x, bottom, 0.0F}), project({x, top, 0.0F}), grid_color);
            draw->AddLine(project({left, y, 0.0F}), project({right, y, 0.0F}), grid_color);
        }
        const std::array corners{project({left, bottom, 0.0F}), project({right, bottom, 0.0F}), project({right, top, 0.0F}), project({left, top, 0.0F})};
        for (std::size_t index = 0; index < corners.size(); ++index)
        {
            draw->AddLine(corners[index], corners[(index + 1U) % corners.size()], boundary_color, 1.5F);
        }
    }
    if (state.map_preview_show_altitude && markers.size() <= 2000)
    {
        for (const auto& marker : markers)
        {
            draw->AddLine(marker.ground, marker.point, IM_COL32(105, 145, 158, 55));
        }
    }
    for (const auto& marker : markers)
    {
        if (!marker.visible)
        {
            continue;
        }
        const auto radius = marker.radius;
        const std::array diamond{ImVec2{marker.point.x, marker.point.y - radius}, ImVec2{marker.point.x + radius, marker.point.y}, ImVec2{marker.point.x, marker.point.y + radius},
                                 ImVec2{marker.point.x - radius, marker.point.y}};
        draw->AddConvexPolyFilled(diamond.data(), static_cast<int>(diamond.size()), marker.color);
        draw->AddPolyline(diamond.data(), static_cast<int>(diamond.size()), marker.outline, ImDrawFlags_Closed, 1.25F);
    }
    draw->AddText({canvas_min.x + 12.0F, canvas_min.y + 10.0F}, IM_COL32(178, 197, 205, 255), "Drag to orbit  |  Wheel to zoom  |  Hover an object for details");
    draw->PopClipRect();

    if (ImGui::IsItemHovered())
    {
        const auto mouse = ImGui::GetIO().MousePos;
        const SpaceMarker* hovered = nullptr;
        float nearest = std::numeric_limits<float>::max();
        for (const auto& marker : markers)
        {
            const auto dx = mouse.x - marker.point.x;
            const auto dy = mouse.y - marker.point.y;
            const auto distance = dx * dx + dy * dy;
            const auto hit_radius = marker.radius + 4.0F;
            if (distance <= hit_radius * hit_radius && distance < nearest)
            {
                hovered = &marker;
                nearest = distance;
            }
        }
        if (hovered != nullptr)
        {
            map_object_tooltip(state, hovered->object_index);
        }
    }
}

} // namespace

void render_map_viewport(GuiApp::State& state, const bool reserve_footer)
{
    if (!state.preview_map)
    {
        return;
    }
    const auto& map = *state.preview_map;
    if (map.type == TedMapType::space)
    {
        render_space_map_viewport(state, reserve_footer);
        return;
    }
    const auto has_terrain = map.terrain_width >= 2 && map.terrain_height >= 2 && map.elevation_map.size() >= static_cast<std::size_t>(map.terrain_width) * map.terrain_height;

    ImGui::Checkbox("Reference grid", &state.map_preview_show_grid);
    ImGui::SameLine();
    ImGui::Checkbox("Objects", &state.map_preview_show_objects);
    ImGui::SameLine();
    if (!has_terrain || map.passability_map.empty())
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("Passability", &state.map_preview_show_passability))
    {
        state.map_preview_cached = false;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Color raw passability classes; class meanings vary by map type");
    }
    if (!has_terrain || map.passability_map.empty())
    {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Wireframe", &state.map_preview_show_wireframe);

    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::SliderFloat("##map-vertical-scale", &state.map_preview_vertical_scale, 0.0F, 3.0F, "Elevation scale: %.2fx"))
    {
        state.map_preview_cached = false;
    }
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::SliderInt("##map-triangle-budget", &state.map_preview_triangle_budget, 2000, 18000, "Terrain detail: up to %d triangles"))
    {
        state.map_preview_cached = false;
    }

    if (!state.map_preview_cached)
    {
        state.map_preview_triangles = map_preview_triangles(map, state.map_preview_triangle_budget, state.map_preview_vertical_scale, state.map_preview_show_passability);
        state.map_preview_cached = true;
        state.terrain_mesh_cached = false;
        state.map_object_cached = false;
    }
    if (!state.terrain_mesh_cached && state.renderer.ready())
    {
        std::vector<PreviewVertex> vertices;
        std::vector<std::uint32_t> indices;
        vertices.reserve(state.map_preview_triangles.size() * 3);
        indices.reserve(state.map_preview_triangles.size() * 3);
        for (const auto& triangle : state.map_preview_triangles)
        {
            for (const auto& point : triangle.points)
            {
                indices.push_back(static_cast<std::uint32_t>(vertices.size()));
                vertices.push_back({{point.x, point.y, point.z}, {}, triangle.color});
            }
        }
        const std::vector<PreviewPart> parts{{0, static_cast<std::uint32_t>(indices.size()), nullptr, PreviewBlend::opaque, 0.5F}};
        const PreviewInstance identity;
        if (!indices.empty() && state.terrain_mesh.upload(state.renderer.device(), state.renderer.context(), vertices, indices, parts))
        {
            static_cast<void>(state.terrain_mesh.set_instances(state.renderer.device(), state.renderer.context(), std::span<const PreviewInstance>{&identity, 1}));
        }
        state.terrain_mesh_cached = true;
    }

    // Placed objects use editor coordinates at 20 world units per terrain sample, with Z elevation
    // in the heightmap's 20x stored scale. Model geometry keeps uniform world proportions.
    const auto elevation_range = std::max(1.0F, static_cast<float>(map.maximum_elevation - map.minimum_elevation));
    const auto horizontal_extent = static_cast<float>(std::max(map.terrain_width - 1U, map.terrain_height - 1U));
    const auto vertical_extent = horizontal_extent * 0.36F * state.map_preview_vertical_scale;
    const auto editor_width = static_cast<float>(map.terrain_width) * 20.0F;
    const auto editor_height = static_cast<float>(map.terrain_height) * 20.0F;
    const auto grid_center_x = static_cast<float>(map.terrain_width - 1U) * 0.5F;
    const auto grid_center_y = static_cast<float>(map.terrain_height - 1U) * 0.5F;
    const auto land_point = [&](const TedPlacedObject& object)
    {
        return Vector3{object.position.x / editor_width * static_cast<float>(map.terrain_width - 1U) - grid_center_x,
                       object.position.y / editor_height * static_cast<float>(map.terrain_height - 1U) - grid_center_y,
                       ((object.position.z * 20.0F - map.minimum_elevation) / elevation_range - 0.5F) * vertical_extent};
    };
    const auto land_unit_scale = static_cast<float>(map.terrain_width - 1U) / std::max(editor_width, 1.0F);
    const auto has_object_space = map.world_width > 0.0F && map.world_height > 0.0F;
    if (has_object_space && !state.map_object_cached)
    {
        update_map_instances(state, land_point, land_unit_scale);
    }
    ImGui::TextColored(muted, "%u x %u grid  |  %zu terrain triangles  |  elevation %d..%d", map.terrain_width, map.terrain_height, state.map_preview_triangles.size(), map.minimum_elevation,
                       map.maximum_elevation);
    render_map_object_status(state);

    auto canvas_size = ImGui::GetContentRegionAvail();
    if (reserve_footer)
    {
        canvas_size.y -= ImGui::GetFrameHeightWithSpacing();
    }
    canvas_size.x = std::max(canvas_size.x, 120.0F);
    canvas_size.y = std::max(canvas_size.y, 260.0F);
    ImGui::InvisibleButton("map-canvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft);
    const auto canvas_min = ImGui::GetItemRectMin();
    const auto canvas_max = ImGui::GetItemRectMax();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        state.map_preview_yaw += ImGui::GetIO().MouseDelta.x * 0.012F;
        state.map_preview_pitch = std::clamp(state.map_preview_pitch - ImGui::GetIO().MouseDelta.y * 0.012F, -1.45F, 1.45F);
    }
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0F)
    {
        state.map_preview_zoom = std::clamp(state.map_preview_zoom * std::pow(1.12F, ImGui::GetIO().MouseWheel), 0.15F, 12.0F);
    }

    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvas_min, canvas_max, IM_COL32(7, 13, 20, 255), 6.0F);
    draw->AddRect(canvas_min, canvas_max, IM_COL32(40, 78, 91, 255), 6.0F);
    if (state.map_preview_triangles.empty())
    {
        draw->AddText({canvas_min.x + 14.0F, canvas_min.y + 14.0F}, IM_COL32(150, 164, 174, 255), "No renderable terrain heightmap in this map");
        return;
    }

    Vector3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vector3 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (const auto& triangle : state.map_preview_triangles)
    {
        for (const auto& point : triangle.points)
        {
            minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y), std::min(minimum.z, point.z)};
            maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y), std::max(maximum.z, point.z)};
        }
    }
    const Vector3 center{(minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F, (minimum.z + maximum.z) * 0.5F};
    const auto extent = std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z, 0.001F});
    const auto scale = std::min(canvas_size.x, canvas_size.y) * 0.78F / extent * state.map_preview_zoom;
    const auto screen_center = ImVec2{(canvas_min.x + canvas_max.x) * 0.5F, (canvas_min.y + canvas_max.y) * 0.5F};
    const auto yaw_cos = std::cos(state.map_preview_yaw);
    const auto yaw_sin = std::sin(state.map_preview_yaw);
    const auto pitch_cos = std::cos(state.map_preview_pitch);
    const auto pitch_sin = std::sin(state.map_preview_pitch);
    const auto project = [&](const Vector3& point, float* depth = nullptr)
    {
        const auto x = point.x - center.x;
        const auto y = point.y - center.y;
        const auto z = point.z - center.z;
        const auto horizontal = yaw_cos * x + yaw_sin * y;
        const auto forward = -yaw_sin * x + yaw_cos * y;
        const auto vertical = -pitch_sin * forward + pitch_cos * z;
        if (depth != nullptr)
        {
            *depth = pitch_cos * forward + pitch_sin * z;
        }
        return ImVec2{screen_center.x + horizontal * scale, screen_center.y - vertical * scale};
    };

    std::vector<const PreviewMesh*> meshes{&state.terrain_mesh};
    if (state.map_preview_show_objects && has_object_space)
    {
        const auto objects = map_object_meshes(state);
        meshes.insert(meshes.end(), objects.begin(), objects.end());
    }
    float largest_model = 0.0F;
    for (const auto& [path, model] : state.map_models)
    {
        largest_model = std::max(largest_model, model.radius * land_unit_scale);
    }
    draw->PushClipRect(canvas_min, canvas_max, true);
    draw_gpu_preview(state, state.map_target, canvas_min, canvas_max, center, state.map_preview_yaw, state.map_preview_pitch, scale, extent * 1.5F + largest_model * 4.0F, meshes);

    struct LandMarker
    {
        ImVec2 point;
        float depth{};
        float radius{};
        ImU32 color{};
        ImU32 outline{};
        std::size_t object_index{};
        bool visible{true};
    };
    std::vector<LandMarker> markers;
    if (state.map_preview_show_objects && has_object_space)
    {
        std::vector<std::size_t> positioned;
        positioned.reserve(map.placed_objects.size());
        for (std::size_t index = 0; index < map.placed_objects.size(); ++index)
        {
            if (map.placed_objects[index].has_transform)
            {
                positioned.push_back(index);
            }
        }
        const auto marker_stride = std::max<std::size_t>(1, (positioned.size() + 5999U) / 6000U);
        markers.reserve((positioned.size() + marker_stride - 1U) / marker_stride);
        for (std::size_t shown = 0; shown < positioned.size(); shown += marker_stride)
        {
            const auto object_index = positioned[shown];
            const auto& object = map.placed_objects[object_index];
            LandMarker marker;
            marker.point = project(land_point(object), &marker.depth);
            marker.radius = object_marker_radius(object, state.map_preview_marker_scale);
            marker.color = passability_color(object.crc);
            marker.outline = object_player_color(object.player);
            marker.object_index = object_index;
            if (object_index < state.map_object_drawn.size() && state.map_object_drawn[object_index] != 0)
            {
                marker.visible = false;
                marker.radius = std::max(marker.radius, map_object_radius(state, object_index) * land_unit_scale * scale);
            }
            markers.push_back(marker);
        }
        std::ranges::sort(markers, {}, &LandMarker::depth);
    }

    if (state.map_preview_show_grid)
    {
        const auto grid_color = IM_COL32(93, 157, 169, 105);
        for (int index = 0; index <= 10; ++index)
        {
            const auto t = static_cast<float>(index) / 10.0F;
            const auto x = std::lerp(minimum.x, maximum.x, t);
            const auto y = std::lerp(minimum.y, maximum.y, t);
            draw->AddLine(project({x, minimum.y, minimum.z}), project({x, maximum.y, minimum.z}), grid_color);
            draw->AddLine(project({minimum.x, y, minimum.z}), project({maximum.x, y, minimum.z}), grid_color);
        }
    }
    if (state.map_preview_show_wireframe)
    {
        const auto wire_stride = std::max<std::size_t>(1, (state.map_preview_triangles.size() + 3999U) / 4000U);
        for (std::size_t index = 0; index < state.map_preview_triangles.size(); index += wire_stride)
        {
            const auto& triangle = state.map_preview_triangles[index];
            draw->AddTriangle(project(triangle.points[0]), project(triangle.points[1]), project(triangle.points[2]), IM_COL32(8, 16, 20, 95));
        }
    }
    for (const auto& marker : markers)
    {
        if (!marker.visible)
        {
            continue;
        }
        const auto radius = marker.radius;
        const std::array diamond{ImVec2{marker.point.x, marker.point.y - radius}, ImVec2{marker.point.x + radius, marker.point.y}, ImVec2{marker.point.x, marker.point.y + radius},
                                 ImVec2{marker.point.x - radius, marker.point.y}};
        draw->AddConvexPolyFilled(diamond.data(), static_cast<int>(diamond.size()), marker.color);
        draw->AddPolyline(diamond.data(), static_cast<int>(diamond.size()), marker.outline, ImDrawFlags_Closed, 1.25F);
    }
    draw->AddText({canvas_min.x + 12.0F, canvas_min.y + 10.0F}, IM_COL32(178, 197, 205, 255),
                  state.map_preview_show_objects ? "Drag to orbit  |  Wheel to zoom  |  Hover an object for details" : "Drag to orbit  |  Wheel to zoom");
    draw->PopClipRect();

    if (state.map_preview_show_objects && ImGui::IsItemHovered())
    {
        const auto mouse = ImGui::GetIO().MousePos;
        const LandMarker* hovered = nullptr;
        float nearest = std::numeric_limits<float>::max();
        for (const auto& marker : markers)
        {
            const auto dx = mouse.x - marker.point.x;
            const auto dy = mouse.y - marker.point.y;
            const auto distance = dx * dx + dy * dy;
            const auto hit_radius = marker.radius + 4.0F;
            if (distance <= hit_radius * hit_radius && distance < nearest)
            {
                hovered = &marker;
                nearest = distance;
            }
        }
        if (hovered != nullptr)
        {
            map_object_tooltip(state, hovered->object_index);
        }
    }
}

void render_large_map_preview(GuiApp::State& state)
{
    if (!state.map_preview_large_window)
    {
        return;
    }
    if (!state.preview_map)
    {
        state.map_preview_large_window = false;
        return;
    }

    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 window_size{std::max(760.0F, viewport->WorkSize.x * 0.86F), std::max(560.0F, viewport->WorkSize.y * 0.86F)};
    const ImVec2 window_center{viewport->WorkPos.x + viewport->WorkSize.x * 0.5F, viewport->WorkPos.y + viewport->WorkSize.y * 0.5F};
    ImGui::SetNextWindowSize(window_size, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(window_center, ImGuiCond_Appearing, {0.5F, 0.5F});
    if (ImGui::Begin("Map Preview###large-map-preview", &state.map_preview_large_window, ImGuiWindowFlags_NoCollapse))
    {
        if (state.selected_asset >= 0 && static_cast<std::size_t>(state.selected_asset) < state.assets.size())
        {
            ImGui::TextColored(muted, "%s", state.assets[static_cast<std::size_t>(state.selected_asset)].virtual_path.c_str());
            ImGui::Separator();
        }
        ImGui::PushID("large-map-preview-content");
        render_map_viewport(state, true);
        ImGui::PopID();
        if (ImGui::Button("Reset camera"))
        {
            state.map_preview_yaw = -0.65F;
            state.map_preview_pitch = 0.72F;
            state.map_preview_zoom = 1.0F;
        }
    }
    ImGui::End();
}

} // namespace eaw::tools
