// The 3D model preview with its animation pose.

#include "assets/alo_interpretation.hpp"
#include "assets/animation.hpp"
#include "assets/pose.hpp"
#include "assets/skeleton.hpp"
#include "gui_internal.hpp"
#include "core/string_utils.hpp"
#include "formats/alo_model_helpers.hpp"

namespace eaw::tools {
namespace {

std::vector<PoseBone> preview_pose(const GuiApp::State& state)
{
    if (!state.preview_skeleton)
    {
        return {};
    }
    if (state.preview_animation)
    {
        return sample_pose(*state.preview_animation, *state.preview_skeleton, state.preview_time);
    }
    const auto transforms = skeleton_bind_pose(*state.preview_skeleton);
    std::vector<PoseBone> pose;
    pose.reserve(transforms.size());
    for (std::size_t index = 0; index < transforms.size(); ++index)
    {
        pose.push_back({transforms[index], state.preview_skeleton->bones[index].visible});
    }
    return pose;
}

// Marker colours of hardpoint kinds (XML Type HARD_POINT_WEAPON_*, _SHIELD_GENERATOR, _ENGINE, ...).
ImU32 hardpoint_color(const std::string_view kind)
{
    if (ascii_find_ignoring_case(kind, "WEAPON") != std::string_view::npos)
    {
        return IM_COL32(236, 92, 80, 255);
    }
    if (ascii_find_ignoring_case(kind, "SHIELD") != std::string_view::npos)
    {
        return IM_COL32(86, 160, 240, 255);
    }
    if (ascii_find_ignoring_case(kind, "ENGINE") != std::string_view::npos)
    {
        return IM_COL32(240, 168, 64, 255);
    }
    return IM_COL32(190, 196, 204, 255);
}

// The damage state, detail level, object type, and hardpoint controls; true when the geometry must
// be rebuilt.
bool render_appearance_controls(GuiApp::State& state)
{
    bool changed = false;
    const auto highest = static_cast<int>(state.preview_variants.highest_alternate);
    if (highest > 0)
    {
        ImGui::SetNextItemWidth(170.0F);
        changed |= ImGui::SliderInt("State", &state.preview_alternate, 0, highest, state.preview_alternate == 0 ? "Intact (ALT0)" : "Damaged (ALT%d)");
    }
    const auto& levels = state.preview_variants.detail_levels;
    if (levels.size() > 1)
    {
        auto shown = state.preview_detail < 0 ? static_cast<int>(levels.size()) - 1 : state.preview_detail;
        const auto label = "LOD" + std::to_string(levels[static_cast<std::size_t>(std::clamp(shown, 0, static_cast<int>(levels.size()) - 1))]);
        if (highest > 0)
        {
            ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(170.0F);
        if (ImGui::SliderInt("Detail", &shown, 0, static_cast<int>(levels.size()) - 1, label.c_str()))
        {
            state.preview_detail = shown;
            changed = true;
        }
    }
    if (state.preview_types.empty())
    {
        return changed;
    }
    const auto* current = state.preview_type_index >= 0 ? state.preview_types[static_cast<std::size_t>(state.preview_type_index)] : nullptr;
    ImGui::SetNextItemWidth(-90.0F);
    if (ImGui::BeginCombo("Object type", current != nullptr ? current->name.c_str() : "None"))
    {
        if (ImGui::Selectable("None", current == nullptr))
        {
            state.select_preview_type(-1);
        }
        for (std::size_t index = 0; index < state.preview_types.size(); ++index)
        {
            const auto label = state.preview_types[index]->name + "  (" + state.preview_types[index]->type + ")";
            if (ImGui::Selectable(label.c_str(), state.preview_type_index == static_cast<int>(index)))
            {
                state.select_preview_type(static_cast<int>(index));
            }
        }
        ImGui::EndCombo();
    }
    if (!state.preview_type_warnings.empty())
    {
        ImGui::SameLine();
        ImGui::TextColored(warning, "%zu", state.preview_type_warnings.size());
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            for (const auto& text : state.preview_type_warnings)
            {
                ImGui::TextUnformatted(text.c_str());
            }
            ImGui::EndTooltip();
        }
    }
    auto& hardpoints = state.preview_hardpoints;
    if (hardpoints.empty())
    {
        return changed;
    }
    const auto header = "Hardpoints (" + std::to_string(hardpoints.size()) + ")###hardpoints";
    if (!ImGui::CollapsingHeader(header.c_str()))
    {
        return changed;
    }
    const auto set_all = [&](const bool shown, const bool destroyed)
    {
        for (auto& hardpoint : hardpoints)
        {
            hardpoint.shown = shown;
            hardpoint.destroyed = destroyed;
        }
        changed = true;
    };
    if (ImGui::SmallButton("Show all"))
    {
        set_all(true, false);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Hide all"))
    {
        set_all(false, false);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Destroy all"))
    {
        set_all(true, true);
    }
    if (ImGui::BeginTable("##hardpoints", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, {0.0F, 150.0F}))
    {
        ImGui::TableSetupColumn("Show", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Destroyed", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Hardpoint");
        ImGui::TableSetupColumn("Type");
        ImGui::TableHeadersRow();
        for (std::size_t index = 0; index < hardpoints.size(); ++index)
        {
            auto& hardpoint = hardpoints[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            changed |= ImGui::Checkbox("##shown", &hardpoint.shown);
            ImGui::TableNextColumn();
            changed |= ImGui::Checkbox("##destroyed", &hardpoint.destroyed);
            ImGui::TableNextColumn();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(hardpoint_color(hardpoint.kind)), "%s", hardpoint.name.c_str());
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Bone: %s", hardpoint.bone && *hardpoint.bone < state.preview_skeleton->bones.size() ? state.preview_skeleton->bones[*hardpoint.bone].name.c_str() : "(not found)");
                ImGui::Text("Model: %s", hardpoint.attached_path.empty() ? "(none)" : hardpoint.attached_path.c_str());
                ImGui::Text("Damage decal: %s", hardpoint.decal_bone ? state.preview_skeleton->bones[*hardpoint.decal_bone].name.c_str() : "(none)");
                ImGui::EndTooltip();
            }
            ImGui::TableNextColumn();
            ImGui::TextColored(muted, "%s", hardpoint.kind.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    return changed;
}

// The model's geometry in the chosen appearance, with the shown hardpoints' attached models.
PreviewGeometry appearance_geometry(GuiApp::State& state, const std::vector<PoseBone>& pose)
{
    const auto texture = [&state](const std::string& reference) -> ID3D11ShaderResourceView*
    {
        const auto found = state.preview_textures.find(ascii_lower(reference));
        return found == state.preview_textures.end() ? nullptr : found->second.view.Get();
    };
    const auto& model = *state.preview_model;
    const auto& levels = state.preview_variants.detail_levels;
    AloMeshSelection selection{.alternate = static_cast<std::uint32_t>(std::max(state.preview_alternate, 0)), .apply_bind_pose_bones = false};
    if (state.preview_detail >= 0 && static_cast<std::size_t>(state.preview_detail) < levels.size())
    {
        selection.detail_level = levels[static_cast<std::size_t>(state.preview_detail)];
    }
    // Scorch marks of intact hardpoints stay hidden, as in battle.
    std::vector<std::uint32_t> decals;
    for (const auto& hardpoint : state.preview_hardpoints)
    {
        if (hardpoint.decal_bone && !hardpoint.destroyed)
        {
            decals.push_back(*hardpoint.decal_bone);
        }
    }
    std::vector<bool> hidden_bones;
    if (!decals.empty())
    {
        std::vector<std::int32_t> parents;
        for (const auto& bone : state.preview_skeleton->bones)
        {
            parents.push_back(bone.parent);
        }
        hidden_bones = bones_in_subtrees(parents, decals);
    }
    auto geometry = build_preview_geometry(model, &pose, state.preview_inverse_bind, state.preview_show_collision, texture, alo_mesh_visibility(model, selection), hidden_bones);
    for (const auto& hardpoint : state.preview_hardpoints)
    {
        if (hardpoint.shown && !hardpoint.destroyed && hardpoint.attached && hardpoint.bone && *hardpoint.bone < pose.size())
        {
            append_preview_geometry(geometry, build_preview_geometry(*hardpoint.attached, nullptr, {}, false, texture, {}, {}, pose[*hardpoint.bone].transform));
        }
    }
    return geometry;
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
        const auto duration = animation_duration(*state.preview_animation);
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
    ImGui::Checkbox("Hardpoint labels", &state.preview_show_hardpoint_labels);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset camera"))
    {
        state.preview_yaw = -0.65F;
        state.preview_pitch = 0.35F;
        state.preview_zoom = 1.0F;
    }
    if (render_appearance_controls(state))
    {
        state.model_mesh_cached = false;
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
        const auto geometry = appearance_geometry(state, pose);
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
    ImGui::TextColored(muted, "%zu DDS loaded  |  %llu triangles", state.preview_textures.size(), static_cast<unsigned long long>(alo_triangle_count(*state.preview_model)));

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
        for (std::size_t index = 0; index < pose.size() && index < state.preview_skeleton->bones.size(); ++index)
        {
            const auto parent = state.preview_skeleton->bones[index].parent;
            if (parent < 0 || static_cast<std::size_t>(parent) >= pose.size())
            {
                continue;
            }
            const auto point = translation(pose[index].transform);
            const auto parent_point = translation(pose[static_cast<std::size_t>(parent)].transform);
            draw->AddLine(project(parent_point), project(point), IM_COL32(255, 202, 74, 230), 1.5F);
        }
    }
    for (const auto& hardpoint : state.preview_hardpoints)
    {
        if (!hardpoint.shown || !hardpoint.bone || *hardpoint.bone >= pose.size())
        {
            continue;
        }
        const auto at = project(translation(pose[*hardpoint.bone].transform));
        const auto color = hardpoint_color(hardpoint.kind);
        draw->AddCircle(at, 5.0F, color, 12, 2.0F);
        if (hardpoint.destroyed)
        {
            draw->AddLine({at.x - 4.0F, at.y - 4.0F}, {at.x + 4.0F, at.y + 4.0F}, color, 2.0F);
            draw->AddLine({at.x - 4.0F, at.y + 4.0F}, {at.x + 4.0F, at.y - 4.0F}, color, 2.0F);
        }
        if (state.preview_show_hardpoint_labels)
        {
            draw->AddText({at.x + 7.0F, at.y - 7.0F}, color, hardpoint.name.c_str());
        }
    }
    draw->AddText({canvas_min.x + 12.0F, canvas_min.y + 10.0F}, IM_COL32(178, 197, 205, 255), "Drag to orbit  |  Wheel to zoom");
    draw->PopClipRect();
}

void render_large_model_preview(GuiApp::State& state)
{
    if (!state.model_preview_large_window)
    {
        return;
    }
    if (!state.preview_model)
    {
        state.model_preview_large_window = false;
        return;
    }
    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 window_size{std::max(760.0F, viewport->WorkSize.x * 0.86F), std::max(560.0F, viewport->WorkSize.y * 0.86F)};
    const ImVec2 window_center{viewport->WorkPos.x + viewport->WorkSize.x * 0.5F, viewport->WorkPos.y + viewport->WorkSize.y * 0.5F};
    ImGui::SetNextWindowSize(window_size, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(window_center, ImGuiCond_Appearing, {0.5F, 0.5F});
    if (ImGui::Begin("Model Preview###large-model-preview", &state.model_preview_large_window, ImGuiWindowFlags_NoCollapse))
    {
        if (state.selected_asset >= 0 && static_cast<std::size_t>(state.selected_asset) < state.assets.size())
        {
            ImGui::TextColored(muted, "%s", state.assets[static_cast<std::size_t>(state.selected_asset)].virtual_path.c_str());
            ImGui::Separator();
        }
        ImGui::PushID("large-model-preview-content");
        render_model_viewport(state);
        ImGui::PopID();
    }
    ImGui::End();
}

} // namespace eaw::tools
