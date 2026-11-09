#include "lua_survey_command.hpp"

#include "eaw/script_host.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace eaw {
namespace {

struct ScriptResult
{
    std::string path;
    bool loaded{};
    bool exited{};
    std::size_t threads_at_end{};
    std::vector<ScriptError> errors;
};

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
    for (const auto& asset : vfs.list("DATA/SCRIPTS"))
    {
        if (!ascii_upper(asset.virtual_path).ends_with(".LUA"))
        {
            continue;
        }
        ScriptResult result;
        result.path = asset.virtual_path;
        auto host = ScriptHost::create(vfs, recorder, host_options);
        if (!host)
        {
            result.errors.push_back({"host", host.error().message});
            results.push_back(std::move(result));
            continue;
        }
        auto& script = **host;
        result.loaded = script.load(asset.virtual_path).ok();
        // The engine calls Base_Definitions when a script is created (the framework's version calls
        // the script's Definitions); scripts without the framework get Definitions directly.
        if (result.loaded && script.has_function("Base_Definitions"))
        {
            (void)script.call("Base_Definitions");
        }
        else if (result.loaded && script.has_function("Definitions"))
        {
            (void)script.call("Definitions");
        }
        // The framework clears Object while a script is created, so the engine binds a game-object
        // script's object afterwards (an inference from PGBaseDefinitions.lua); here it is a stub.
        if (result.loaded && ascii_upper(result.path).find("/GAMEOBJECT/") != std::string::npos)
        {
            script.bind_stub("Object");
        }
        if (result.loaded && script.has_function("main"))
        {
            (void)script.start_thread("main");
        }
        for (std::uint64_t tick = 0; tick < ticks && script.live_threads() > 0 && !script.exited(); ++tick)
        {
            script.tick();
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
    std::map<std::string, std::size_t> error_kinds;
    for (const auto& result : results)
    {
        loaded += result.loaded ? 1U : 0U;
        clean += result.errors.empty() ? 1U : 0U;
        running += result.threads_at_end > 0 ? 1U : 0U;
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
        scripts << "script\tloaded\terrors\texited\tthreads_at_end\tfirst_error\n";
        for (const auto& result : results)
        {
            scripts << result.path << '\t' << (result.loaded ? "yes" : "no") << '\t' << result.errors.size() << '\t' << (result.exited ? "yes" : "no") << '\t' << result.threads_at_end << '\t'
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

} // namespace eaw
