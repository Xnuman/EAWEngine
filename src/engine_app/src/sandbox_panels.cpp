#include "engine_app/sandbox_panels.hpp"

#include "core/string_utils.hpp"

#include <imgui.h>

#include <algorithm>
#include <set>
#include <string_view>
#include <vector>

namespace eaw::engine_app {
namespace {

// Where each panel first opens (the gap to the bar and between panels, and the panels' first
// sizes): side by side below the bar, so none covers another. Engine choices.
constexpr float panel_margin = 12.0F;
constexpr float spawn_width = 360.0F;
constexpr float spawn_height = 460.0F;
constexpr float statistics_width = 430.0F;
constexpr float side_width = 320.0F;
constexpr float camera_height = 150.0F;

bool contains_folded(const std::string_view text, const std::string_view needle)
{
    return eaw::ascii_find_ignoring_case(text, needle) != std::string_view::npos;
}

// The panels' column origin below the bar (the bar's height whether it is shown or not, so a panel
// first opened with the bar hidden does not move under it later).
float panel_top()
{
    return ImGui::GetFrameHeight() + panel_margin;
}

// Opens a panel at its first-use place; afterwards it is where the user left it.
void first_place(const float x, const float y, const float width, const float height)
{
    ImGui::SetNextWindowPos(ImVec2(x, panel_top() + y), ImGuiCond_FirstUseEver);
    if (width > 0.0F)
    {
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
    }
}

// The right-hand column, from the window's right edge.
float right_column()
{
    return std::max(ImGui::GetIO().DisplaySize.x - side_width - panel_margin, panel_margin);
}

constexpr std::array<std::string_view, 8> categories{"All", "Infantry", "Vehicles", "Heroes", "Structures", "Space units", "Props", "Other"};

} // namespace

std::string_view to_string(const SandboxCameraMode mode) noexcept
{
    switch (mode)
    {
    case SandboxCameraMode::fixed:
        return "fixed";
    case SandboxCameraMode::debug:
        return "debug";
    case SandboxCameraMode::free:
        return "free";
    }
    return "fixed";
}

std::string_view spawn_category(const std::string_view kind) noexcept
{
    const auto is = [&](const std::initializer_list<std::string_view> names)
    {
        return std::ranges::any_of(names,
                                   [&](const std::string_view name)
                                   {
                                       return ascii_equal_ignoring_case(kind, name);
                                   });
    };
    if (is({"GroundInfantry", "GroundCompany"}))
    {
        return categories[1];
    }
    if (is({"GroundVehicle", "Mobile_Defense_Unit", "TransportUnit", "LandBombingRunUnit"}))
    {
        return categories[2];
    }
    if (is({"HeroUnit", "GenericHeroUnit", "HeroCompany", "UniqueUnit"}))
    {
        return categories[3];
    }
    if (is({"GroundStructure", "GroundBuildable", "SecondaryStructure", "SpecialStructure", "StarBase", "SpaceStructure", "SpaceBuildable"}))
    {
        return categories[4];
    }
    if (is({"SpaceUnit", "Squadron"}))
    {
        return categories[5];
    }
    if (ascii_lower(kind).starts_with("prop") || is({"SpaceProp"}))
    {
        return categories[6];
    }
    return categories[7];
}

void draw_sandbox_bar(SandboxPanels& panels, bool& show_bounds, bool& show_passability)
{
    if (!panels.bar || !ImGui::BeginMainMenuBar())
    {
        return;
    }
    if (ImGui::BeginMenu("Panels"))
    {
        ImGui::MenuItem("Statistics", nullptr, &panels.statistics);
        ImGui::MenuItem("Spawn", nullptr, &panels.spawn);
        ImGui::MenuItem("Sound", nullptr, &panels.sound);
        ImGui::MenuItem("Camera", nullptr, &panels.camera);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Bounds", "B", &show_bounds);
        ImGui::MenuItem("Passability", "G", &show_passability);
        ImGui::EndMenu();
    }
    ImGui::TextDisabled("F1 hides this bar");
    ImGui::EndMainMenuBar();
}

void draw_statistics_panel(SandboxPanels& panels, const std::span<const std::string> lines)
{
    if (!panels.statistics)
    {
        return;
    }
    first_place(panel_margin + spawn_width + panel_margin, 0.0F, statistics_width, 0.0F);
    ImGui::SetNextWindowBgAlpha(0.7F);
    if (ImGui::Begin("Statistics", &panels.statistics, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing))
    {
        for (const auto& line : lines)
        {
            if (line.empty())
            {
                ImGui::Separator();
            }
            else
            {
                ImGui::TextUnformatted(line.c_str());
            }
        }
    }
    ImGui::End();
}

bool draw_camera_panel(SandboxPanels& panels, const CameraPanelView& view)
{
    if (!panels.camera || view.mode == nullptr)
    {
        return false;
    }
    first_place(right_column(), 0.0F, side_width, camera_height);
    ImGui::SetNextWindowBgAlpha(0.85F);
    bool changed = false;
    if (ImGui::Begin("Camera", &panels.camera, ImGuiWindowFlags_NoCollapse))
    {
        auto mode = static_cast<int>(*view.mode);
        ImGui::BeginDisabled(!view.map);
        changed |= ImGui::RadioButton("Fixed", &mode, static_cast<int>(SandboxCameraMode::fixed));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Debug", &mode, static_cast<int>(SandboxCameraMode::debug));
        ImGui::EndDisabled();
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Free", &mode, static_cast<int>(SandboxCameraMode::free));
        *view.mode = static_cast<SandboxCameraMode>(mode);
        if (view.fly_speed != nullptr)
        {
            ImGui::SliderFloat("Free speed", view.fly_speed, 10.0F, 5000.0F, "%.0f", ImGuiSliderFlags_Logarithmic);
        }
        ImGui::TextDisabled(*view.mode == SandboxCameraMode::free    ? "Right-drag look | WASD move | Q/E down/up | Shift fast | C next mode"
                            : *view.mode == SandboxCameraMode::debug ? "Drag rotate | right-drag pan | WASD pan | C next mode"
                                                                     : "WASD pan | wheel zoom | C next mode");
    }
    ImGui::End();
    return changed;
}

bool draw_sound_panel(SandboxPanels& panels, const SoundPanelView& view)
{
    if (!panels.sound)
    {
        return false;
    }
    first_place(right_column(), camera_height + panel_margin, side_width, 0.0F);
    ImGui::SetNextWindowBgAlpha(0.85F);
    bool changed = false;
    if (ImGui::Begin("Sound", &panels.sound, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::TextWrapped("%s", view.status.c_str());
        if (view.master != nullptr)
        {
            changed |= ImGui::SliderFloat("Master", view.master, 0.0F, 1.0F, "%.2f");
        }
        ImGui::Separator();
        for (std::size_t index = 0; index < view.categories.size() && index < view.category_names.size(); ++index)
        {
            const std::string label(view.category_names[index]);
            changed |= ImGui::SliderFloat(label.c_str(), &view.categories[index], 0.0F, 1.0F, "%.2f");
        }
    }
    ImGui::End();
    return changed;
}

SpawnPanelActions draw_spawn_panel(SandboxPanels& panels, const SpawnPanelView& view)
{
    SpawnPanelActions actions;
    if (!panels.spawn)
    {
        return actions;
    }
    first_place(panel_margin, 0.0F, spawn_width, spawn_height);
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
    // Category and faction filters, once the types' kinds and factions are known.
    const bool described = view.types.size() == view.names.size();
    std::vector<std::string> factions{"All factions"};
    if (described)
    {
        std::set<std::string> known;
        for (const auto& type : view.types)
        {
            for (const auto& faction : type.affiliations)
            {
                known.insert(faction);
            }
        }
        factions.insert(factions.end(), known.begin(), known.end());
    }
    panels.category = std::clamp(panels.category, 0, static_cast<int>(categories.size()) - 1);
    panels.faction = std::clamp(panels.faction, 0, static_cast<int>(factions.size()) - 1);
    ImGui::BeginDisabled(!described);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5F - ImGui::GetStyle().ItemSpacing.x * 0.5F);
    if (ImGui::BeginCombo("##category", std::string(categories[static_cast<std::size_t>(panels.category)]).c_str()))
    {
        for (std::size_t index = 0; index < categories.size(); ++index)
        {
            if (ImGui::Selectable(std::string(categories[index]).c_str(), panels.category == static_cast<int>(index)))
            {
                panels.category = static_cast<int>(index);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::BeginCombo("##faction", factions[static_cast<std::size_t>(panels.faction)].c_str()))
    {
        for (std::size_t index = 0; index < factions.size(); ++index)
        {
            if (ImGui::Selectable(factions[index].c_str(), panels.faction == static_cast<int>(index)))
            {
                panels.faction = static_cast<int>(index);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    const std::string_view filter(panels.filter.data());
    std::vector<std::size_t> shown;
    shown.reserve(view.names.size());
    for (std::size_t index = 0; index < view.names.size(); ++index)
    {
        if (!contains_folded(view.names[index], filter))
        {
            continue;
        }
        if (described && panels.category != 0 && spawn_category(view.types[index].kind) != categories[static_cast<std::size_t>(panels.category)])
        {
            continue;
        }
        if (described && panels.faction != 0 && std::ranges::find(view.types[index].affiliations, factions[static_cast<std::size_t>(panels.faction)]) == view.types[index].affiliations.end())
        {
            continue;
        }
        shown.push_back(index);
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

} // namespace eaw::engine_app
