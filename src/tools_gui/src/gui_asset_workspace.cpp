// The asset browser workspace.

#include "core/string_utils.hpp"
#include "tools_gui/gui_internal.hpp"

namespace eaw::tools {

void render_asset_workspace(GuiApp::State& state, HWND owner)
{
    ImGui::TextUnformatted("ASSET INSPECTOR");
    ImGui::SameLine();
    ImGui::TextColored(muted, "Layered game and mod resources");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(-125.0F);
    ImGui::InputTextWithHint("##game-root", "Empire at War installation", &state.game_root);
    ImGui::SameLine();
    if (ImGui::Button("Browse...", {110.0F, 0.0F}))
    {
        if (const auto path = pick_folder(owner, L"Select Empire at War installation"); !path)
        {
            state.fail(path.error());
        }
        else if (*path)
        {
            state.game_root = path_to_utf8(**path);
        }
    }

    if (ImGui::RadioButton("Forces of Corruption", state.variant == GameVariant::forces_of_corruption))
    {
        state.variant = GameVariant::forces_of_corruption;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Empire at War", state.variant == GameVariant::empire_at_war))
    {
        state.variant = GameVariant::empire_at_war;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add mod layer..."))
    {
        if (const auto path = pick_folder(owner, L"Select a mod folder"); !path)
        {
            state.fail(path.error());
        }
        else if (*path)
        {
            state.mods.push_back(**path);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Load assets"))
    {
        state.load_assets();
    }

    if (!state.mods.empty())
    {
        ImGui::TextColored(muted, "Mod priority (first wins):");
        for (std::size_t index = 0; index < state.mods.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(index));
            ImGui::BulletText("%s", path_to_utf8(state.mods[index]).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
            {
                state.mods.erase(state.mods.begin() + static_cast<std::ptrdiff_t>(index));
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-405.0F);
    ImGui::InputTextWithHint("##asset-search", "Search virtual paths...", &state.asset_query);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(155.0F);
    ImGui::InputTextWithHint("##extension", "Extension, e.g. .xml", &state.asset_extension);
    ImGui::SameLine();
    auto normalized_extension = ascii_lower(state.asset_extension);
    if (!normalized_extension.empty() && normalized_extension.front() != '.')
    {
        normalized_extension.insert(normalized_extension.begin(), '.');
    }
    const char* known_extension_label = normalized_extension.empty() ? known_asset_extensions.front().label : "Custom extension";
    for (const auto& known : known_asset_extensions)
    {
        if (normalized_extension == known.extension)
        {
            known_extension_label = known.label;
            break;
        }
    }
    ImGui::SetNextItemWidth(235.0F);
    if (ImGui::BeginCombo("##known-extension", known_extension_label))
    {
        for (const auto& known : known_asset_extensions)
        {
            const auto selected = normalized_extension == known.extension;
            if (ImGui::Selectable(known.label, selected))
            {
                state.asset_extension = known.extension;
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Choose a known game asset type or enter a custom extension");
    }

    std::vector<int> visible;
    visible.reserve(state.assets.size());
    const auto query = ascii_lower(state.asset_query);
    auto extension = ascii_lower(state.asset_extension);
    if (!extension.empty() && extension.front() != '.')
    {
        extension.insert(extension.begin(), '.');
    }
    std::uint64_t total_bytes = 0;
    for (std::size_t index = 0; index < state.assets.size(); ++index)
    {
        const auto& asset = state.assets[index];
        if (!query.empty() && ascii_lower(asset.virtual_path).find(query) == std::string::npos)
        {
            continue;
        }
        if (!extension.empty() && ascii_lower(std::filesystem::path(asset.virtual_path).extension().string()) != extension)
        {
            continue;
        }
        visible.push_back(static_cast<int>(index));
        total_bytes += asset.size;
    }

    ImGui::TextColored(accent, "%zu assets", visible.size());
    ImGui::SameLine();
    ImGui::TextColored(muted, "  %s visible  •  %zu mounted layers", human_size(total_bytes).c_str(), state.vfs.mounts().size());

    const float preview_width = std::max(360.0F, ImGui::GetContentRegionAvail().x * ((state.preview_model || state.preview_map) ? 0.52F : 0.40F));
    if (ImGui::BeginTable("asset-layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, {-1.0F, -1.0F}))
    {
        ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch, 0.60F);
        ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthFixed, preview_width);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginTable("asset-table", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
                              {-1.0F, -1.0F}))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Virtual path", ImGuiTableColumnFlags_WidthStretch, 0.67F);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 82.0F);
            ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthStretch, 0.33F);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible.size()));
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const int asset_index = visible[static_cast<std::size_t>(row)];
                    const auto& asset = state.assets[static_cast<std::size_t>(asset_index)];
                    ImGui::PushID(asset_index);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::Selectable(asset.virtual_path.c_str(), state.selected_asset == asset_index, ImGuiSelectableFlags_SpanAllColumns))
                    {
                        state.inspect_asset(asset_index);
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(human_size(asset.size).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(asset.mount_name.c_str());
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("asset-preview", {-1.0F, -1.0F}, ImGuiChildFlags_Borders);
        if (state.asset_preview.empty())
        {
            ImGui::TextColored(muted, "Select an asset to inspect its container, header, and contents.");
        }
        else if (state.preview_model || state.preview_map)
        {
            if (ImGui::BeginTabBar("visual-preview-tabs"))
            {
                if (ImGui::BeginTabItem("3D Preview"))
                {
                    if (state.preview_model)
                    {
                        if (!state.model_preview_large_window)
                        {
                            ImGui::PushID("embedded-model-preview");
                            render_model_viewport(state);
                            ImGui::PopID();
                            if (ImGui::Button("Preview"))
                            {
                                state.model_preview_large_window = true;
                            }
                        }
                        else
                        {
                            ImGui::TextColored(muted, "Model preview is open in a separate window.");
                            if (ImGui::SmallButton("Close large preview"))
                            {
                                state.model_preview_large_window = false;
                            }
                        }
                    }
                    else
                    {
                        if (!state.map_preview_large_window)
                        {
                            ImGui::PushID("embedded-map-preview");
                            render_map_viewport(state, true);
                            ImGui::PopID();
                            if (ImGui::Button("Preview"))
                            {
                                state.map_preview_large_window = true;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Reset camera"))
                            {
                                state.map_preview_yaw = -0.65F;
                                state.map_preview_pitch = 0.72F;
                                state.map_preview_zoom = 1.0F;
                            }
                        }
                        else
                        {
                            ImGui::TextColored(muted, "Map preview is open in a separate window.");
                            if (ImGui::SmallButton("Close large preview"))
                            {
                                state.map_preview_large_window = false;
                            }
                        }
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Metadata"))
                {
                    ImGui::TextUnformatted(state.asset_preview.c_str());
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        else
        {
            ImGui::TextUnformatted(state.asset_preview.c_str());
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
}

} // namespace eaw::tools
