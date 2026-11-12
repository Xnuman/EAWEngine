#include "engine_app/lua_survey_command.hpp"

#include "core/string_utils.hpp"
#include "script/script_host.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace eaw::engine_app {
namespace {

struct ScriptResult
{
    std::string path;
    bool loaded{};
    bool exited{};
    std::size_t threads_at_end{};
    // Engine events the survey queued: story triggers, task force events, callbacks.
    std::size_t story_events{};
    std::size_t task_force_events{};
    std::size_t callback_events{};
    std::vector<ScriptError> errors;
    // Wall-clock time of the script's ticks: in all, and the slowest one.
    double tick_seconds{};
    double worst_tick_seconds{};
};

// Task force events of AI plans: the plan's <TaskForce>_<Event>, else the framework's
// Default_<Event> (PGEvents.lua defines one per event; that the engine falls back to it is an
// inference from the names). The list is every Default_ handler plus the plan-only
// Production_Failed.
constexpr std::array<std::string_view, 16> task_force_event_names{
    "Production_Failed",    "No_Units_Remaining", "Original_Target_Owner_Changed", "Original_Target_Destroyed", "Current_Target_Destroyed", "Unit_Move_Finished",     "Unit_Damaged",
    "Unit_Destroyed",       "Target_In_Range",    "Hardpoint_Target_In_Range",     "Unit_Diversion_Finished",   "Unit_Ability_Ready",       "Unit_Ability_Cancelled", "Unit_Ability_Finished",
    "Space_Conflict_Begin", "Space_Conflict_End",
};

// The names of an AI plan's task forces: the first string of each entry of its TaskForce table.
std::vector<std::string> task_force_names(const ScriptHost& script)
{
    std::vector<std::string> names;
    const auto task_forces = script.global("TaskForce");
    if (!task_forces || task_forces->kind != ScriptValue::Kind::table)
    {
        return names;
    }
    for (const auto& entry : task_forces->fields)
    {
        if (const auto* name = entry.value.element(1); name != nullptr && name->kind == ScriptValue::Kind::string)
        {
            names.push_back(name->text);
        }
    }
    return names;
}

// The events a story script reacts to: the keys of its StoryModeEvents table.
std::vector<std::string> story_event_names(const ScriptHost& script)
{
    std::vector<std::string> names;
    const auto events = script.global("StoryModeEvents");
    if (!events || events->kind != ScriptValue::Kind::table)
    {
        return names;
    }
    for (const auto& entry : events->fields)
    {
        if (entry.key.kind == ScriptValue::Kind::string)
        {
            names.push_back(entry.key.text);
        }
    }
    return names;
}

// An error message without its "file:line:" prefix and trace, so equal causes count together.
std::string error_kind(const std::string& message)
{
    auto text = message.substr(0, message.find('\n'));
    // "@DATA/...LUA:12: attempt to ..." or "DATA/...:12: ..." -> "attempt to ...".
    if (const auto colon = text.find(": "); colon != std::string::npos && text.find(".LUA:") != std::string::npos && text.find(".LUA:") < colon)
    {
        text.erase(0, colon + 2);
    }
    // Keep the shape, not the names: quoted names become '...'.
    std::string shape;
    bool quoted = false;
    for (const auto character : text)
    {
        if (character == '\'' || character == '`')
        {
            if (!quoted)
            {
                shape += "'...'";
            }
            quoted = !quoted;
            continue;
        }
        if (!quoted)
        {
            shape += character;
        }
    }
    return shape;
}

std::string tsv(std::string text)
{
    std::ranges::replace(text, '\t', ' ');
    std::ranges::replace(text, '\n', ' ');
    std::ranges::replace(text, '\r', ' ');
    return text;
}

} // namespace

int run_lua_survey(const SandboxOptions& options, const VirtualFileSystem& vfs)
{
    const auto ticks = options.ticks.value_or(300U);
    const auto started = std::chrono::steady_clock::now();
    HostApiRecorder recorder;
    std::vector<ScriptResult> results;
    // Names any script defines are script names, not engine functions, wherever they are defined.
    ScriptHostOptions host_options;
    host_options.script_globals = std::make_shared<const std::set<std::string>>(script_defined_globals(vfs));
    host_options.recorder = &recorder;
    for (const auto& asset : vfs.list("DATA/SCRIPTS"))
    {
        if (!ascii_upper(asset.virtual_path).ends_with(".LUA"))
        {
            continue;
        }
        ScriptResult result;
        result.path = asset.virtual_path;
        auto host = ScriptHost::create(vfs, host_options);
        if (!host)
        {
            result.errors.push_back({"host", host.error().message});
            results.push_back(std::move(result));
            continue;
        }
        auto& script = **host;
        result.loaded = script.load(asset.virtual_path).ok();
        if (result.loaded)
        {
            (void)script.run_definitions();
        }
        // The framework clears Object, PlayerObject, and Target while a script is created, so the
        // engine binds them afterwards (an inference from PGBaseDefinitions.lua); here they are
        // stubs: Object for game-object scripts, PlayerObject for the per-player AI, free-store,
        // evaluator, and intervention scripts, Target for AI plans. An AI plan (a TaskForce table) also gets one
        // object per task force under its name, and each task force's <Name>_Thread runs, as the
        // plans' structure implies.
        const auto folder = ascii_upper(result.path);
        const auto in_folder = [&](const std::string_view name)
        {
            return folder.find(name) != std::string::npos;
        };
        if (result.loaded && in_folder("/GAMEOBJECT/"))
        {
            script.set_global("Object", ScriptValue::object("Object"));
        }
        if (result.loaded && (in_folder("/AI/") || in_folder("/FREESTORE/") || in_folder("/EVALUATORS/") || in_folder("/INTERVENTIONS/")))
        {
            script.set_global("PlayerObject", ScriptValue::object("PlayerObject"));
        }
        const auto task_forces = result.loaded ? task_force_names(script) : std::vector<std::string>{};
        if (!task_forces.empty())
        {
            script.set_global("Target", ScriptValue::object("Target"));
        }
        for (const auto& task_force : task_forces)
        {
            script.set_global(task_force, ScriptValue::object(task_force));
            if (script.has_function(task_force + "_Thread"))
            {
                (void)script.start_thread(task_force + "_Thread");
            }
        }
        if (result.loaded && script.has_function("main"))
        {
            (void)script.start_thread("main");
        }
        // Events, spread over the run so each meets running threads: story events one at a time
        // over the first three quarters (the engine fires them through Story_Event_Trigger, which
        // no script calls), every task force event at the half, and every callback the scripts
        // handed to engine functions by then at three quarters. Engine objects in their arguments
        // are stubs named Event_Unit, Event_Other, Callback_Object, and Callback_Other, since the
        // real argument lists are not known yet.
        const auto story_events = result.loaded && script.has_function("Story_Event_Trigger") ? story_event_names(script) : std::vector<std::string>{};
        const auto story_span = ticks * 3U / 4U;
        std::size_t next_story_event = 0;
        for (std::uint64_t tick = 0; tick < ticks && script.live_threads() > 0 && !script.exited(); ++tick)
        {
            while (next_story_event < story_events.size() && tick >= story_span * (next_story_event + 1) / (story_events.size() + 1))
            {
                result.story_events += script.queue_event("Story_Event_Trigger", {ScriptValue::from_string(story_events[next_story_event])}).ok() ? 1U : 0U;
                ++next_story_event;
            }
            if (tick == ticks / 2U)
            {
                for (const auto& task_force : task_forces)
                {
                    for (const auto event : task_force_event_names)
                    {
                        auto handler = task_force + "_" + std::string(event);
                        if (!script.has_function(handler))
                        {
                            handler = "Default_" + std::string(event);
                        }
                        if (script.has_function(handler))
                        {
                            const std::vector arguments{ScriptValue::object(task_force), ScriptValue::object("Event_Unit"), ScriptValue::object("Event_Other")};
                            result.task_force_events += script.queue_event(handler, arguments).ok() ? 1U : 0U;
                        }
                    }
                }
            }
            if (tick == story_span)
            {
                for (std::size_t callback = 0; callback < script.callbacks().size(); ++callback)
                {
                    const std::vector arguments{ScriptValue::object("Callback_Object"), ScriptValue::object("Callback_Other")};
                    result.callback_events += script.queue_callback(callback, arguments).ok() ? 1U : 0U;
                }
            }
            const auto tick_started = std::chrono::steady_clock::now();
            script.tick();
            const auto tick_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - tick_started).count();
            result.tick_seconds += tick_seconds;
            result.worst_tick_seconds = std::max(result.worst_tick_seconds, tick_seconds);
        }
        result.exited = script.exited();
        result.threads_at_end = script.live_threads();
        result.errors = script.errors();
        results.push_back(std::move(result));
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    std::size_t loaded = 0;
    std::size_t clean = 0;
    std::size_t running = 0;
    std::size_t story_events = 0;
    std::size_t task_force_events = 0;
    std::size_t callback_events = 0;
    std::map<std::string, std::size_t> error_kinds;
    for (const auto& result : results)
    {
        loaded += result.loaded ? 1U : 0U;
        clean += result.errors.empty() ? 1U : 0U;
        running += result.threads_at_end > 0 ? 1U : 0U;
        story_events += result.story_events;
        task_force_events += result.task_force_events;
        callback_events += result.callback_events;
        for (const auto& error : result.errors)
        {
            ++error_kinds[error_kind(error.message)];
        }
    }
    const auto& calls = recorder.calls();
    std::size_t functions = 0;
    std::size_t methods = 0;
    for (const auto& [name, record] : calls)
    {
        (name.find("().") != std::string::npos || name.ends_with("()()") ? methods : functions) += 1;
    }
    std::cout << "Lua survey: " << results.size() << " scripts in " << std::fixed << std::setprecision(1) << seconds << " s, " << ticks << " ticks each\n"
              << "Loaded: " << loaded << " | without errors: " << clean << " | threads still running at the end: " << running << '\n'
              << "Events queued: " << story_events << " story, " << task_force_events << " task force, " << callback_events << " callbacks\n"
              << "Host API: " << functions << " functions and " << methods << " methods called\n\n"
              << "Most used (by scripts calling them):\n";
    std::vector<std::pair<std::string, const HostCallRecord*>> ranked;
    for (const auto& [name, record] : calls)
    {
        ranked.emplace_back(name, &record);
    }
    std::ranges::sort(ranked,
                      [](const auto& left, const auto& right)
                      {
                          if (left.second->scripts.size() != right.second->scripts.size())
                          {
                              return left.second->scripts.size() > right.second->scripts.size();
                          }
                          if (left.second->calls != right.second->calls)
                          {
                              return left.second->calls > right.second->calls;
                          }
                          return left.first < right.first;
                      });
    for (std::size_t index = 0; index < ranked.size() && index < 40; ++index)
    {
        const auto& [name, record] = ranked[index];
        std::cout << "  " << std::setw(4) << record->scripts.size() << " scripts " << std::setw(8) << record->calls << " calls  " << name << (record->implemented ? "  [host]" : "") << '\n';
    }
    // Cost of running scripts: their ticks in all, and the slowest single ticks.
    double script_seconds = 0.0;
    std::vector<const ScriptResult*> slowest;
    for (const auto& result : results)
    {
        script_seconds += result.tick_seconds;
        slowest.push_back(&result);
    }
    std::ranges::sort(slowest,
                      [](const ScriptResult* left, const ScriptResult* right)
                      {
                          return left->worst_tick_seconds > right->worst_tick_seconds;
                      });
    const auto script_ticks = static_cast<double>(std::max<std::size_t>(results.size(), 1U) * ticks);
    std::cout << "\nScript ticks: " << std::setprecision(1) << script_seconds * 1000.0 << " ms in all, " << std::setprecision(2) << script_seconds * 1.0e6 / script_ticks
              << " us per script and tick; slowest single ticks:\n";
    for (std::size_t index = 0; index < slowest.size() && index < 5; ++index)
    {
        std::cout << "  " << std::setw(8) << slowest[index]->worst_tick_seconds * 1000.0 << " ms  " << slowest[index]->path << '\n';
    }
    std::cout << std::setprecision(1);
    std::vector<std::pair<std::size_t, std::string>> kinds;
    for (const auto& [kind, count] : error_kinds)
    {
        kinds.emplace_back(count, kind);
    }
    std::ranges::sort(kinds, std::greater<>());
    std::cout << "\nMost common errors:\n";
    for (std::size_t index = 0; index < kinds.size() && index < 15; ++index)
    {
        std::cout << "  " << std::setw(5) << kinds[index].first << "  " << kinds[index].second << '\n';
    }

    if (options.report)
    {
        std::error_code error;
        std::filesystem::create_directories(*options.report, error);
        std::ofstream api(*options.report / "api.tsv", std::ios::binary | std::ios::trunc);
        api << "name\tkind\thost\tscripts\tcalls\tsignatures\n";
        for (const auto& [name, record] : ranked)
        {
            std::vector<std::pair<std::uint64_t, std::string>> signatures;
            for (const auto& [signature, count] : record->signatures)
            {
                signatures.emplace_back(count, signature);
            }
            std::ranges::sort(signatures, std::greater<>());
            std::string listed;
            for (std::size_t index = 0; index < signatures.size() && index < 4; ++index)
            {
                listed += (index == 0 ? "" : " | ") + signatures[index].second + " x" + std::to_string(signatures[index].first);
            }
            const bool method = name.find("().") != std::string::npos || name.ends_with("()()");
            api << tsv(name) << '\t' << (method ? "method" : "function") << '\t' << (record->implemented ? "yes" : "stub") << '\t' << record->scripts.size() << '\t' << record->calls << '\t'
                << tsv(listed) << '\n';
        }
        std::ofstream scripts(*options.report / "scripts.tsv", std::ios::binary | std::ios::trunc);
        scripts << "script\tloaded\terrors\texited\tthreads_at_end\tevents\ttick_ms\tworst_tick_ms\tfirst_error\n";
        for (const auto& result : results)
        {
            scripts << result.path << '\t' << (result.loaded ? "yes" : "no") << '\t' << result.errors.size() << '\t' << (result.exited ? "yes" : "no") << '\t' << result.threads_at_end << '\t'
                    << result.story_events + result.task_force_events + result.callback_events << '\t' << result.tick_seconds * 1000.0 << '\t' << result.worst_tick_seconds * 1000.0 << '\t'
                    << (result.errors.empty() ? std::string{} : tsv(result.errors.front().context + ": " + result.errors.front().message.substr(0, 300))) << '\n';
        }
        if (!api || !scripts)
        {
            std::cerr << "error: could not write the reports to " << options.report->string() << '\n';
            return 1;
        }
        std::cout << "\nReports: " << (*options.report / "api.tsv").string() << ", " << (*options.report / "scripts.tsv").string() << '\n';
    }
    return 0;
}

} // namespace eaw::engine_app
