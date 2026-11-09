// The 3D model preview with its animation pose.

#include "gui_internal.hpp"
#include "core/string_utils.hpp"

namespace eaw::tools {
namespace {

std::vector<AlaPoseBone> preview_pose(const GuiApp::State& state)
{
    if (!state.preview_model)
    {
        return {};
    }
    if (state.preview_animation)
    {
        return state.preview_animation->sample_pose(*state.preview_model, state.preview_time);
    }
    const auto transforms = state.preview_model->bind_pose();
    std::vector<AlaPoseBone> pose;
    pose.reserve(transforms.size());
    for (std::size_t index = 0; index < transforms.size(); ++index)
    {
        pose.push_back({transforms[index], state.preview_model->bones[index].visible});
    }
    return pose;
}

} // namespace

void render_model_viewport(GuiApp::State& state)
{
    if (!state.preview_model)
    {
        return;
    }

    const char* animation_label = "Bind pose";
    std::string selected_animation_name;
    if (state.preview_animation_index >= 0 && static_cast<std::size_t>(state.preview_animation_index) < state.preview_animation_assets.size())
    {
        selected_animation_name = std::filesystem::path(state.preview_animation_assets[static_cast<std::size_t>(state.preview_animation_index)].virtual_path).filename().string();
        animation_label = selected_animation_name.c_str();
    }
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::BeginCombo("##preview-animation", animation_label))
    {
        if (ImGui::Selectable("Bind pose", state.preview_animation_index < 0))
        {
            state.select_preview_animation(-1);
            state.model_mesh_cached = false;
        }
        for (std::size_t index = 0; index < state.preview_animation_assets.size(); ++index)
        {
            const auto name = std::filesystem::path(state.preview_animation_assets[index].virtual_path).filename().string();
            if (ImGui::Selectable(name.c_str(), state.preview_animation_index == static_cast<int>(index)))
            {
                state.select_preview_animation(static_cast<int>(index));
                state.model_mesh_cached = false;
            }
        }
        ImGui::EndCombo();
    }

    if (state.preview_animation)
    {
        if (ImGui::Button(state.preview_playing ? "Pause" : "Play"))
        {
            state.preview_playing = !state.preview_playing;
        }
        ImGui::SameLine();
        const auto duration = static_cast<float>(state.preview_animation->duration_seconds());
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::SliderFloat("##animation-time", &state.preview_time, 0.0F, std::max(duration, 0.001F), "%.3f s");
        if (state.preview_playing && duration > 0.0F)
        {
            state.preview_time = std::fmod(state.preview_time + ImGui::GetIO().DeltaTime, duration);
        }
    }

    ImGui::Checkbox("Skeleton", &state.preview_show_skeleton);
    ImGui::SameLine();
    if (ImGui::Checkbox("Collision", &state.preview_show_collision))
    {
        state.model_mesh_cached = false;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset camera"))
    {
        state.preview_yaw = -0.65F;
        state.preview_pitch = 0.35F;
        state.preview_zoom = 1.0F;
    }
    if (!state.preview_texture_warnings.empty())
    {
        ImGui::TextColored(warning, "%zu texture(s) unavailable", state.preview_texture_warnings.size());
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            for (const auto& warning_text : state.preview_texture_warnings)
            {
                ImGui::TextUnformatted(warning_text.c_str());
            }
            ImGui::EndTooltip();
        }
    }

    // Static models upload once; animated models re-skin on the CPU and re-upload every frame.
    const auto pose = preview_pose(state);
    if ((!state.model_mesh_cached || state.preview_animation || state.model_mesh_collision != state.preview_show_collision) && state.renderer.ready())
    {
        const auto geometry = build_preview_geometry(*state.preview_model, &pose, state.preview_inverse_bind, state.preview_show_collision,
                                                     [&state](const std::string& reference) -> ID3D11ShaderResourceView*
                                                     {
                                                         const auto found = state.preview_textures.find(ascii_lower(reference));
                                                         return found == state.preview_textures.end() ? nullptr : found->second.view.Get();
                                                     });
        const PreviewInstance identity;
        if (!geometry.indices.empty() && state.model_mesh.upload(state.renderer.device(), state.renderer.context(), geometry.vertices, geometry.indices, geometry.parts))
        {
            static_cast<void>(state.model_mesh.set_instances(state.renderer.device(), state.renderer.context(), std::span<const PreviewInstance>{&identity, 1}));
            state.model_minimum = geometry.minimum;
            state.model_maximum = geometry.maximum;
        }
        else
        {
            static_cast<void>(state.model_mesh.set_instances(state.renderer.device(), state.renderer.context(), {}));
        }
        state.model_mesh_cached = true;
        state.model_mesh_collision = state.preview_show_collision;
    }
    ImGui::TextColored(muted, "%zu DDS loaded  |  %llu triangles", state.preview_textures.size(), static_cast<unsigned long long>(state.preview_model->triangle_count()));

    auto canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.x = std::max(canvas_size.x, 120.0F);
    canvas_size.y = std::max(canvas_size.y, 260.0F);
    ImGui::InvisibleButton("model-canvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft);
    const auto canvas_min = ImGui::GetItemRectMin();
    const auto canvas_max = ImGui::GetItemRectMax();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        state.preview_yaw += ImGui::GetIO().MouseDelta.x * 0.012F;
        state.preview_pitch = std::clamp(state.preview_pitch - ImGui::GetIO().MouseDelta.y * 0.012F, -1.45F, 1.45F);
    }
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0F)
    {
        state.preview_zoom = std::clamp(state.preview_zoom * std::pow(1.12F, ImGui::GetIO().MouseWheel), 0.15F, 12.0F);
    }

    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvas_min, canvas_max, IM_COL32(7, 13, 20, 255), 6.0F);
    draw->AddRect(canvas_min, canvas_max, IM_COL32(40, 78, 91, 255), 6.0F);
    if (!state.model_mesh.ready() || state.model_minimum.x > state.model_maximum.x)
    {
        draw->AddText({canvas_min.x + 14.0F, canvas_min.y + 14.0F}, IM_COL32(150, 164, 174, 255),
                      state.renderer.ready() ? "No renderable triangle stream in this model" : "GPU preview unavailable on this device");
        return;
    }

    const auto& minimum = state.model_minimum;
    const auto& maximum = state.model_maximum;
    const Vector3 center{(minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F, (minimum.z + maximum.z) * 0.5F};
    const auto extent = std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z, 0.001F});
    const auto scale = std::min(canvas_size.x, canvas_size.y) * 0.78F / extent * state.preview_zoom;
    const auto screen_center = ImVec2{(canvas_min.x + canvas_max.x) * 0.5F, (canvas_min.y + canvas_max.y) * 0.5F};
    const auto yaw_cos = std::cos(state.preview_yaw);
    const auto yaw_sin = std::sin(state.preview_yaw);
    const auto pitch_cos = std::cos(state.preview_pitch);
    const auto pitch_sin = std::sin(state.preview_pitch);
    const auto project = [&](const Vector3& point)
    {
        const auto x = point.x - center.x;
        const auto y = point.y - center.y;
        const auto z = point.z - center.z;
        const auto horizontal = yaw_cos * x + yaw_sin * y;
        const auto forward = -yaw_sin * x + yaw_cos * y;
        const auto vertical = -pitch_sin * forward + pitch_cos * z;
        return ImVec2{screen_center.x + horizontal * scale, screen_center.y - vertical * scale};
    };

    draw->PushClipRect(canvas_min, canvas_max, true);
    const PreviewMesh* meshes[]{&state.model_mesh};
    draw_gpu_preview(state, state.model_target, canvas_min, canvas_max, center, state.preview_yaw, state.preview_pitch, scale, extent * 2.0F, meshes);
    if (state.preview_show_skeleton)
    {
        for (std::size_t index = 0; index < pose.size() && index < state.preview_model->bones.size(); ++index)
        {
            const auto parent = state.preview_model->bones[index].parent;
            if (parent < 0 || static_cast<std::size_t>(parent) >= pose.size())
            {
                continue;
            }
            const auto point = translation(pose[index].transform);
            const auto parent_point = translation(pose[static_cast<std::size_t>(parent)].transform);
            draw->AddLine(project(parent_point), project(point), IM_COL32(255, 202, 74, 230), 1.5F);
        }
    }
    draw->AddText({canvas_min.x + 12.0F, canvas_min.y + 10.0F}, IM_COL32(178, 197, 205, 255), "Drag to orbit  |  Wheel to zoom");
    draw->PopClipRect();
}

} // namespace eaw::tools
