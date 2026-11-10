#include "dev_launcher_app.hpp"

#include "vfs/asset_environment.hpp"
#include "eaw/child_process.hpp"
#include "eaw/file_dialogs.hpp"
#include "vfs/game_installation.hpp"
#include "core/log.hpp"
#include "eaw/sandbox_scenario.hpp"
#include "assets/spawn_catalog.hpp"
#include "vfs/virtual_file_system.hpp"
#include "core/string_utils.hpp"
#include "core/file_io.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
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

bool matches(const std::string& item, const std::string& filter)
{
    return filter.empty() || ascii_lower(item).find(ascii_lower(filter)) != std::string::npos;
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
            const bool is_selected = ascii_lower(item) == ascii_lower(selected);
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
    std::filesystem::path engine_path = directory / EAW_ENGINE_FILE_NAME;
    std::vector<SandboxScenario> scenarios;
    std::size_t selected{};
    bool dirty{};
    std::string status;
    bool status_error{};
    // Asset lists of the installation and mods they were listed for.
    std::vector<std::string> maps;
    std::vector<std::string> models;
    std::string listed_for;
    // The installation key last seen and when it changed: the lists refresh by themselves once a
    // change has rested for list_refresh_delay (typing a path refreshes once, not per key).
    std::string seen_key;
    std::chrono::steady_clock::time_point key_changed;
    // The key last refreshed for automatically: a failing refresh (no game found) is tried once.
    std::string refreshed_key;
    std::string map_filter;
    std::string model_filter;
    std::string new_mod;
    std::vector<Run> runs;
    // Spawnable XML types of the listed layers, built on a worker from its own mounted file system.
    std::vector<SpawnTypeInfo> spawn_types;
    std::future<std::vector<SpawnTypeInfo>> spawn_types_pending;
    bool units_only{true};
    bool match_map{true};

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
        std::error_code missing;
        if (!std::filesystem::exists(presets_path, missing))
        {
            scenarios = {SandboxScenario{}};
            report("No saved scenarios yet; presets are stored in " + presets_path.string());
            return;
        }
        const auto text = read_text_file(presets_path);
        if (!text)
        {
            scenarios = {SandboxScenario{}};
            report(text.error().message, true);
            return;
        }
        auto parsed = parse_scenarios(*text);
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
        // A failed write keeps the old presets (file_io writes a sibling file and renames it).
        if (const auto written = write_text_file(presets_path, serialize_scenarios(scenarios), WriteMode::replace); !written)
        {
            report(written.error().message, true);
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
            if (const auto valid = check_installation_root(*path); !valid)
            {
                report(valid.error().message, true);
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
            else
            {
                log_warning("dev-launcher", "skipped mod " + mod + ": " + path.error().message);
            }
        }
        auto shared_vfs = std::make_shared<VirtualFileSystem>();
        auto& vfs = *shared_vfs;
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
                if (path.size() >= extension.size() && ascii_lower(path.substr(path.size() - extension.size())) == extension)
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
        // Reading every XML file takes a few seconds; a refresh during a build waits for it first.
        if (spawn_types_pending.valid())
        {
            spawn_types_pending.wait();
        }
        spawn_types.clear();
        spawn_types_pending = std::async(std::launch::async,
                                         [shared_vfs]
                                         {
                                             return list_spawn_types(*shared_vfs);
                                         });
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

DevLauncherApp::~DevLauncherApp()
{
    if (state_->spawn_types_pending.valid())
    {
        state_->spawn_types_pending.wait();
    }
}

void DevLauncherApp::render(HWND window)
{
    auto& state = *state_;
    // A changed game, installation, or mod list refreshes the asset and spawn lists on its own.
    constexpr auto list_refresh_delay = std::chrono::seconds(1);
    if (const auto key = state.installation_key(); key != state.seen_key)
    {
        state.seen_key = key;
        state.key_changed = std::chrono::steady_clock::now();
    }
    else if (key != state.listed_for && key != state.refreshed_key && std::chrono::steady_clock::now() - state.key_changed >= list_refresh_delay)
    {
        state.refreshed_key = key;
        state.refresh_assets();
    }
    if (state.spawn_types_pending.valid() && state.spawn_types_pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        state.spawn_types = state.spawn_types_pending.get();
        const auto units = std::ranges::count_if(state.spawn_types, &SpawnTypeInfo::unit);
        const auto summary = std::to_string(state.spawn_types.size()) + " spawnable types (" + std::to_string(units) + " units and structures)";
        log_info("dev-launcher", "listed " + summary);
        state.report("Listed " + summary);
    }
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
        ImGui::SetNextItemWidth(-250.0F);
        edited(ImGui::InputTextWithHint("Game root##game-root", "empty: found through Steam", &scenario.game_root));
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
            ImGui::TextUnformatted("Debug spawns (type, count, owner slot; -1 = default 1):");
            if (state.spawn_types_pending.valid())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(reading spawnable types...)");
            }
            // Land or space by the stock naming convention; only a hint for the type list.
            const bool space_map = ascii_lower(scenario.map).find("_space") != std::string::npos;
            for (std::size_t index = 0; index < scenario.spawns.size(); ++index)
            {
                auto& spawn = scenario.spawns[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::SetNextItemWidth(260.0F);
                edited(ImGui::InputTextWithHint("##type", "XML type, e.g. T2B_Tank", &spawn.type));
                ImGui::SameLine();
                if (ImGui::Button("Pick..."))
                {
                    ImGui::OpenPopup("spawn-types");
                }
                if (ImGui::BeginPopup("spawn-types"))
                {
                    ImGui::Checkbox("Units and structures only", &state.units_only);
                    ImGui::SameLine();
                    ImGui::Checkbox(space_map ? "With a space model" : "With a land model", &state.match_map);
                    ImGui::TextDisabled("Filtered by the text in the type field");
                    std::vector<const SpawnTypeInfo*> shown;
                    for (const auto& type : state.spawn_types)
                    {
                        if ((!state.units_only || type.unit) && (!state.match_map || (space_map ? type.space_model : type.land_model)) && matches(type.name, spawn.type))
                        {
                            shown.push_back(&type);
                        }
                    }
                    ImGui::Text("%zu of %zu types", shown.size(), state.spawn_types.size());
                    if (ImGui::BeginChild("type-list", ImVec2(520.0F, 360.0F), ImGuiChildFlags_Borders))
                    {
                        ImGuiListClipper clipper;
                        clipper.Begin(static_cast<int>(shown.size()));
                        while (clipper.Step())
                        {
                            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                            {
                                const auto& type = *shown[static_cast<std::size_t>(row)];
                                ImGui::PushID(row);
                                if (ImGui::Selectable(type.name.c_str()))
                                {
                                    spawn.type = type.name;
                                    edited(true);
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::SameLine(330.0F);
                                ImGui::TextDisabled("%s%s", type.kind.c_str(), type.land_model && type.space_model ? " (land, space)" : type.land_model ? " (land)" : " (space)");
                                ImGui::PopID();
                            }
                        }
                    }
                    ImGui::EndChild();
                    ImGui::EndPopup();
                }
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
            option("Statistics panel", scenario.overlay);
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
