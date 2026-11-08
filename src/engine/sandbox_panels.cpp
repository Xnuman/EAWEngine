#include "sandbox_panels.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

namespace eaw {
namespace {

bool contains_folded(const std::string_view text, const std::string_view needle)
{
    if (needle.empty())
    {
        return true;
    }
    const auto lower = [](const char character)
    {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    };
    return std::ranges::search(text, needle, {}, lower, lower).begin() != text.end();
}

} // namespace

void draw_sandbox_menu(SandboxPanels& panels, bool& show_bounds, bool& show_passability)
{
    if (!panels.menu)
    {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(12.0F, 12.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.8F);
    if (ImGui::Begin("Sandbox (F1)", &panels.menu, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::TextUnformatted("Panels");
        ImGui::Checkbox("Statistics", &panels.statistics);
        ImGui::Checkbox("Spawn", &panels.spawn);
        ImGui::Separator();
        ImGui::TextUnformatted("View");
        ImGui::Checkbox("Bounds (B)", &show_bounds);
        ImGui::Checkbox("Passability (G)", &show_passability);
    }
    ImGui::End();
}

SpawnPanelActions draw_spawn_panel(SandboxPanels& panels, const SpawnPanelView& view)
{
    SpawnPanelActions actions;
    if (!panels.spawn)
    {
        return actions;
    }
    ImGui::SetNextWindowPos(ImVec2(12.0F, 220.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0F, 460.0F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85F);
    if (!ImGui::Begin("Spawn", &panels.spawn, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return actions;
    }
    if (view.names.empty())
    {
        ImGui::TextUnformatted("No spawnable types (open a map).");
        ImGui::End();
        return actions;
    }
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##filter", "Search types", panels.filter.data(), panels.filter.size());
    const std::string_view filter(panels.filter.data());
    std::vector<std::size_t> shown;
    shown.reserve(view.names.size());
    for (std::size_t index = 0; index < view.names.size(); ++index)
    {
        if (contains_folded(view.names[index], filter))
        {
            shown.push_back(index);
        }
    }
    const auto list_height = std::max(ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 4.0F, 80.0F);
    if (ImGui::BeginChild("##types", ImVec2(0.0F, list_height), ImGuiChildFlags_Borders))
    {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const auto index = shown[static_cast<std::size_t>(row)];
                const auto& name = view.names[index];
                const bool loaded = view.loaded && view.loaded(name);
                const auto label = name + (loaded ? "" : "  (loads on spawn)") + "##" + std::to_string(index);
                if (ImGui::Selectable(label.c_str(), view.choice != nullptr && *view.choice == index) && view.choice != nullptr)
                {
                    *view.choice = index;
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::Text("%zu of %zu types", shown.size(), view.names.size());
    if (view.count != nullptr)
    {
        int count = static_cast<int>(*view.count);
        ImGui::SetNextItemWidth(120.0F);
        if (ImGui::InputInt("Count", &count))
        {
            *view.count = static_cast<std::uint32_t>(std::clamp(count, 1, 100));
        }
    }
    if (view.owner != nullptr)
    {
        ImGui::SameLine();
        const auto preview = std::to_string(*view.owner);
        ImGui::SetNextItemWidth(80.0F);
        if (ImGui::BeginCombo("Owner", preview.c_str()))
        {
            for (std::uint32_t owner = 0; owner < 8U; ++owner)
            {
                if (owner == view.neutral_owner)
                {
                    continue;
                }
                if (ImGui::Selectable(std::to_string(owner).c_str(), *view.owner == owner))
                {
                    *view.owner = owner;
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::BeginDisabled(!view.can_spawn);
    if (ImGui::Button("Spawn at view center"))
    {
        actions.spawn_at_view_center = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("or N at the cursor");
    if (!view.status.empty())
    {
        ImGui::TextUnformatted(view.status.c_str());
    }
    ImGui::End();
    return actions;
}

} // namespace eaw
