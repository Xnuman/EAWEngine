#include "dev_launcher_app.hpp"

#include "eaw/asset_environment.hpp"
#include "eaw/child_process.hpp"
#include "eaw/file_dialogs.hpp"
#include "eaw/game_installation.hpp"
#include "eaw/log.hpp"
#include "eaw/sandbox_scenario.hpp"
#include "eaw/virtual_file_system.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace eaw::dev_launcher {
namespace {

std::filesystem::path executable_directory()
{
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;)
    {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return std::filesystem::current_path();
        }
        if (length < buffer.size())
        {
            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::string lower(std::string text)
{
    std::ranges::transform(text, text.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return text;
}

bool matches(const std::string& item, const std::string& filter)
{
    return filter.empty() || lower(item).find(lower(filter)) != std::string::npos;
}

std::string clock_text()
{
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &now);
    std::array<char, 16> text{};
    std::strftime(text.data(), text.size(), "%H:%M:%S", &local);
    return text.data();
}

struct Run
{
    std::string started;
    std::string scenario;
    std::string command;
    ChildProcess process;
    std::optional<int> exit_code;
};

// One searchable list of virtual paths (maps or models) shown in a child window.
bool asset_list(const char* id, const std::vector<std::string>& items, std::string& filter, std::string& selected, const float height)
{
    bool changed = false;
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint((std::string("##filter-") + id).c_str(), "Filter", &filter);
    if (ImGui::BeginChild(id, ImVec2(0.0F, height), ImGuiChildFlags_Borders))
    {
        for (const auto& item : items)
        {
            if (!matches(item, filter))
            {
                continue;
            }
            const bool is_selected = lower(item) == lower(selected);
            if (ImGui::Selectable(item.c_str(), is_selected))
            {
                selected = item;
                changed = true;
            }
            if (is_selected && ImGui::IsWindowAppearing())
            {
                ImGui::SetScrollHereY();
            }
        }
    }
    ImGui::EndChild();
    return changed;
}

} // namespace

struct DevLauncherApp::State
{
    std::filesystem::path directory = executable_directory();
    std::filesystem::path presets_path = directory / "EAWDevLauncher.scenarios";
    std::filesystem::path engine_path = directory / "EAWEngine.exe";
    std::vector<SandboxScenario> scenarios;
    std::size_t selected{};
    bool dirty{};
    std::string status;
    bool status_error{};
    // Asset lists of the installation and mods they were listed for.
    std::vector<std::string> maps;
    std::vector<std::string> models;
    std::string listed_for;
    std::string map_filter;
    std::string model_filter;
    std::string new_mod;
    std::vector<Run> runs;

    void report(std::string message, const bool error = false)
    {
        status = std::move(message);
        status_error = error;
        if (error)
        {
            log_warning("dev-launcher", status);
        }
    }

    SandboxScenario& current()
    {
        return scenarios[selected];
    }

    void load_presets()
    {
        std::ifstream file(presets_path, std::ios::binary);
        if (!file)
        {
            scenarios = {SandboxScenario{}};
            report("No saved scenarios yet; presets are stored in " + presets_path.string());
            return;
        }
        std::ostringstream text;
        text << file.rdbuf();
        auto parsed = parse_scenarios(text.str());
        if (!parsed)
        {
            scenarios = {SandboxScenario{}};
            report(presets_path.string() + ": " + parsed.error().message, true);
            return;
        }
        scenarios = std::move(parsed->scenarios);
        if (scenarios.empty())
        {
            scenarios.emplace_back();
        }
        for (const auto& warning : parsed->warnings)
        {
            log_warning("dev-launcher", presets_path.string() + ": " + warning);
        }
        report("Loaded " + std::to_string(scenarios.size()) + " scenarios" + (parsed->warnings.empty() ? std::string{} : " (" + std::to_string(parsed->warnings.size()) + " warnings in the log)"));
    }

    void save_presets()
    {
        // Write a sibling file first, then replace, so a failed write keeps the old presets.
        const auto temporary = std::filesystem::path(presets_path).concat(".new");
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file << serialize_scenarios(scenarios);
            if (!file)
            {
                report("could not write " + temporary.string(), true);
                return;
            }
        }
        std::error_code error;
        std::filesystem::rename(temporary, presets_path, error);
        if (error)
        {
            report("could not replace " + presets_path.string() + ": " + error.message(), true);
            return;
        }
        dirty = false;
        report("Saved " + std::to_string(scenarios.size()) + " scenarios to " + presets_path.string());
    }

    // The installation (and mods) the lists describe, so a change can prompt a refresh.
    [[nodiscard]] std::string installation_key()
    {
        const auto& scenario = current();
        auto key = std::string(to_string(scenario.game)) + "|" + scenario.game_root;
        for (const auto& mod : scenario.mods)
        {
            key += "|" + mod;
        }
        return key;
    }

    void refresh_assets()
    {
        const auto& scenario = current();
        std::optional<std::filesystem::path> root;
        if (!scenario.game_root.empty())
        {
            auto path = tools::utf8_to_path(scenario.game_root);
            if (!path)
            {
                report(path.error().message, true);
                return;
            }
            root = *path;
        }
        const auto installations = InstallationLocator{}.discover(root);
        if (installations.empty())
        {
            report("Empire at War was not found; set the game root", true);
            return;
        }
        const auto profile = make_game_profile(installations.front(), scenario.game);
        if (!profile)
        {
            report(tools::path_to_utf8(installations.front()) + " is not an Empire at War installation", true);
            return;
        }
        std::vector<std::filesystem::path> mods;
        for (const auto& mod : scenario.mods)
        {
            if (auto path = tools::utf8_to_path(mod))
            {
                mods.push_back(*path);
            }
        }
        VirtualFileSystem vfs;
        const auto mount = mount_game_assets(vfs, *profile, mods);
        for (const auto& warning : mount.warnings)
        {
            log_warning("dev-launcher", warning);
        }
        const auto collect = [&](const std::string_view prefix, const std::string_view extension)
        {
            std::vector<std::string> paths;
            for (const auto& asset : vfs.list(prefix))
            {
                const auto& path = asset.virtual_path;
                if (path.size() >= extension.size() && lower(path.substr(path.size() - extension.size())) == extension)
                {
                    paths.push_back(path);
                }
            }
            std::ranges::sort(paths);
            return paths;
        };
        maps = collect("Data/Art/Maps", ".ted");
        models = collect("Data/Art/Models", ".alo");
        listed_for = installation_key();
        report("Listed " + std::to_string(maps.size()) + " maps and " + std::to_string(models.size()) + " models from " + tools::path_to_utf8(installations.front()) +
               (mount.warnings.empty() ? std::string{} : " (" + std::to_string(mount.warnings.size()) + " mount warnings in the log)"));
    }

    void launch()
    {
        auto arguments = sandbox_arguments(current());
        if (!arguments)
        {
            report("Cannot launch: " + arguments.error().message, true);
            return;
        }
        auto process = ChildProcess::start(engine_path, *arguments);
        if (!process)
        {
            report(process.error().message, true);
            return;
        }
        runs.push_back({clock_text(), current().name, build_command_line(engine_path, *arguments), std::move(process).value(), std::nullopt});
        report("Started " + current().name);
    }
};

DevLauncherApp::DevLauncherApp() : state_(std::make_unique<State>())
{
    state_->load_presets();
    state_->refresh_assets();
}

DevLauncherApp::~DevLauncherApp() = default;

void DevLauncherApp::render(HWND window)
{
    auto& state = *state_;
    for (auto& run : state.runs)
    {
        if (!run.exit_code)
        {
            run.exit_code = run.process.exit_code();
        }
    }

    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("EAWDevLauncher", nullptr, flags);

    // Scenario presets.
    ImGui::BeginChild("presets", ImVec2(260.0F, 0.0F), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    ImGui::TextUnformatted("Scenarios");
    ImGui::Separator();
    for (std::size_t index = 0; index < state.scenarios.size(); ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::Selectable(state.scenarios[index].name.c_str(), index == state.selected))
        {
            state.selected = index;
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::Button("New"))
    {
        state.scenarios.emplace_back();
        state.selected = state.scenarios.size() - 1U;
        state.dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate"))
    {
        auto copy = state.current();
        copy.name += " copy";
        state.scenarios.push_back(std::move(copy));
        state.selected = state.scenarios.size() - 1U;
        state.dirty = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state.scenarios.size() < 2);
    if (ImGui::Button("Delete"))
    {
        state.scenarios.erase(state.scenarios.begin() + static_cast<std::ptrdiff_t>(state.selected));
        state.selected = std::min(state.selected, state.scenarios.size() - 1U);
        state.dirty = true;
    }
    ImGui::EndDisabled();
    if (ImGui::Button(state.dirty ? "Save presets *" : "Save presets"))
    {
        state.save_presets();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
    {
        state.load_presets();
        state.selected = 0;
        state.dirty = false;
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // Editor for the selected scenario.
    ImGui::BeginChild("editor", ImVec2(0.0F, 0.0F));
    auto& scenario = state.current();
    const auto edited = [&](const bool changed)
    {
        state.dirty = state.dirty || changed;
    };
    ImGui::SetNextItemWidth(320.0F);
    edited(ImGui::InputText("Name", &scenario.name));

    if (ImGui::CollapsingHeader("Installation and mods", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int game = scenario.game == GameVariant::empire_at_war ? 0 : 1;
        ImGui::SetNextItemWidth(220.0F);
        if (ImGui::Combo("Game", &game, "Empire at War\0Forces of Corruption\0"))
        {
            scenario.game = game == 0 ? GameVariant::empire_at_war : GameVariant::forces_of_corruption;
            edited(true);
        }
        ImGui::SetNextItemWidth(-160.0F);
        edited(ImGui::InputTextWithHint("##game-root", "Game root (empty: discover)", &scenario.game_root));
        ImGui::SameLine();
        if (ImGui::Button("Browse...##root"))
        {
            if (const auto picked = tools::pick_folder(window, L"Empire at War installation"); picked && *picked)
            {
                scenario.game_root = tools::path_to_utf8(**picked);
                edited(true);
            }
        }
        ImGui::TextDisabled("Mods, highest priority first:");
        for (std::size_t index = 0; index < scenario.mods.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(index));
            ImGui::BulletText("%s", scenario.mods[index].c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Up") && index > 0)
            {
                std::swap(scenario.mods[index], scenario.mods[index - 1U]);
                edited(true);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
            {
                scenario.mods.erase(scenario.mods.begin() + static_cast<std::ptrdiff_t>(index));
                edited(true);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::Button("Add mod folder..."))
        {
            if (const auto picked = tools::pick_folder(window, L"Mod folder"); picked && *picked)
            {
                scenario.mods.push_back(tools::path_to_utf8(**picked));
                edited(true);
            }
        }
        ImGui::SameLine();
        const bool stale = state.listed_for != state.installation_key();
        if (ImGui::Button(stale ? "Refresh asset lists *" : "Refresh asset lists"))
        {
            state.refresh_assets();
        }
    }

    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int scene = static_cast<int>(scenario.scene);
        edited(ImGui::RadioButton("Map", &scene, 0));
        ImGui::SameLine();
        edited(ImGui::RadioButton("Model", &scene, 1));
        ImGui::SameLine();
        edited(ImGui::RadioButton("Validation triangle", &scene, 2));
        scenario.scene = static_cast<SandboxScene>(scene);
        if (scenario.scene == SandboxScene::map)
        {
            ImGui::Text("Map: %s", scenario.map.empty() ? "(none)" : scenario.map.c_str());
            edited(asset_list("maps", state.maps, state.map_filter, scenario.map, 220.0F));
            ImGui::TextUnformatted("Debug spawns (TYPE, count, owner slot; -1 = default 1):");
            for (std::size_t index = 0; index < scenario.spawns.size(); ++index)
            {
                auto& spawn = scenario.spawns[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::SetNextItemWidth(260.0F);
                edited(ImGui::InputTextWithHint("##type", "XML type, e.g. T2B_Tank", &spawn.type));
                ImGui::SameLine();
                int count = static_cast<int>(spawn.count);
                ImGui::SetNextItemWidth(90.0F);
                if (ImGui::InputInt("##count", &count))
                {
                    spawn.count = static_cast<std::uint32_t>(std::clamp(count, 0, 1000));
                    edited(true);
                }
                ImGui::SameLine();
                int owner = spawn.owner ? static_cast<int>(*spawn.owner) : -1;
                ImGui::SetNextItemWidth(90.0F);
                if (ImGui::InputInt("##owner", &owner))
                {
                    owner = std::clamp(owner, -1, 63);
                    spawn.owner = owner < 0 ? std::nullopt : std::optional<std::uint32_t>(static_cast<std::uint32_t>(owner));
                    edited(true);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    scenario.spawns.erase(scenario.spawns.begin() + static_cast<std::ptrdiff_t>(index));
                    edited(true);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::Button("Add spawn"))
            {
                scenario.spawns.push_back({"", 1, std::nullopt});
                edited(true);
            }
        }
        else if (scenario.scene == SandboxScene::model)
        {
            ImGui::Text("Model: %s", scenario.model.empty() ? "(none)" : scenario.model.c_str());
            edited(asset_list("models", state.models, state.model_filter, scenario.model, 260.0F));
            ImGui::SetNextItemWidth(-1.0F);
            edited(ImGui::InputTextWithHint("##animation", "Animation (empty: the model's idle, Left/Right steps)", &scenario.animation));
        }
    }

    if (ImGui::CollapsingHeader("Engine options", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::BeginTable("options", 3))
        {
            const auto option = [&](const char* label, bool& value)
            {
                ImGui::TableNextColumn();
                edited(ImGui::Checkbox(label, &value));
            };
            option("WARP adapter", scenario.warp);
            option("GPU validation", scenario.gpu_validation);
            option("V-sync", scenario.vsync);
            option("Hot reload", scenario.hot_reload);
            option("Show bounds", scenario.show_bounds);
            option("Show passability", scenario.show_passability);
            option("Auto move", scenario.auto_move);
            option("No overlay", scenario.no_overlay);
            option("No raw input", scenario.no_raw_input);
            ImGui::EndTable();
        }
        const auto number = [&](const char* label, std::uint32_t& value)
        {
            int edited_value = static_cast<int>(value);
            ImGui::SetNextItemWidth(140.0F);
            if (ImGui::InputInt(label, &edited_value))
            {
                value = static_cast<std::uint32_t>(std::max(edited_value, 0));
                edited(true);
            }
        };
        number("Frame limit (0 = none)", scenario.frame_limit);
        number("Width (0 = default)", scenario.width);
        number("Height (0 = default)", scenario.height);
        bool budget = scenario.upload_budget.has_value();
        if (ImGui::Checkbox("Upload budget (MiB per frame)", &budget))
        {
            scenario.upload_budget = budget ? std::optional<std::uint32_t>(64U) : std::nullopt;
            edited(true);
        }
        if (scenario.upload_budget)
        {
            ImGui::SameLine();
            number("##budget", *scenario.upload_budget);
        }
        ImGui::SetNextItemWidth(-1.0F);
        edited(ImGui::InputTextWithHint("##extra", "Extra arguments, e.g. --pick 640 360", &scenario.extra_arguments));
    }

    ImGui::Separator();
    const auto arguments = sandbox_arguments(scenario);
    auto command = arguments ? build_command_line(state.engine_path, *arguments) : "(" + arguments.error().message + ")";
    ImGui::BeginDisabled(!arguments);
    if (ImGui::Button("Launch", ImVec2(140.0F, 0.0F)))
    {
        state.launch();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Copy command line") && arguments)
    {
        ImGui::SetClipboardText(command.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Open logs folder"))
    {
        const auto logs = state.directory / "logs";
        ShellExecuteW(nullptr, L"open", logs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::InputTextMultiline("##command", &command, ImVec2(-1.0F, ImGui::GetTextLineHeight() * 3.5F), ImGuiInputTextFlags_ReadOnly);
    if (!state.status.empty())
    {
        ImGui::TextColored(state.status_error ? ImVec4(1.0F, 0.45F, 0.4F, 1.0F) : ImVec4(0.6F, 0.8F, 0.6F, 1.0F), "%s", state.status.c_str());
    }

    if (ImGui::CollapsingHeader("Runs", ImGuiTreeNodeFlags_DefaultOpen) && ImGui::BeginTable("runs", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
    {
        ImGui::TableSetupColumn("Started", ImGuiTableColumnFlags_WidthFixed, 80.0F);
        ImGui::TableSetupColumn("Scenario", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 140.0F);
        ImGui::TableHeadersRow();
        for (auto run = state.runs.rbegin(); run != state.runs.rend(); ++run)
        {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(run->started.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(run->scenario.c_str());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", run->command.c_str());
            }
            ImGui::TableNextColumn();
            if (!run->exit_code)
            {
                ImGui::TextUnformatted("running");
            }
            else
            {
                ImGui::TextColored(*run->exit_code == 0 ? ImVec4(0.6F, 0.8F, 0.6F, 1.0F) : ImVec4(1.0F, 0.45F, 0.4F, 1.0F), "exit %d", *run->exit_code);
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace eaw::dev_launcher
