// `lua-audit`: Lua sources and bytecode across the layered VFS.

#include "core/string_utils.hpp"
#include "asset_inspector/inspector.hpp"

namespace eaw::inspector {

void audit_lua(const eaw::VirtualFileSystem& vfs, const std::string_view prefix, const std::size_t limit)
{
    struct ScriptRecord
    {
        std::string path;
        eaw::LuaChunkData info;
    };

    std::vector<ScriptRecord> scripts;
    std::vector<std::pair<std::string, std::string>> failures;
    std::map<std::string, std::size_t> kinds;
    std::map<std::string, std::size_t> groups;
    std::map<std::string, std::size_t> dependencies;
    std::map<std::string, std::set<std::string>> dependency_users;
    std::map<std::string, std::size_t> definitions;
    std::map<std::string, std::size_t> calls;
    std::set<std::string> definition_keys;
    std::set<std::string> module_keys;
    std::size_t total_lines = 0;
    std::size_t total_prototypes = 0;
    std::size_t total_instructions = 0;
    std::size_t warning_count = 0;

    constexpr std::string_view scripts_prefix = "DATA/SCRIPTS/";
    for (const auto& asset : vfs.list("DATA/SCRIPTS"))
    {
        if (extension_of(asset.virtual_path) != ".lua")
        {
            continue;
        }
        auto relative = asset.virtual_path;
        if (relative.starts_with(scripts_prefix))
        {
            relative.erase(0, scripts_prefix.size());
        }
        auto module = ascii_lower(std::filesystem::path(relative).replace_extension().generic_string());
        module_keys.insert(module);
        auto dotted = module;
        std::ranges::replace(dotted, '/', '.');
        module_keys.insert(std::move(dotted));
        module_keys.insert(ascii_lower(std::filesystem::path(relative).stem().string()));
    }

    const auto assets = vfs.list(prefix);
    for (const auto& asset : assets)
    {
        if (extension_of(asset.virtual_path) != ".lua")
        {
            continue;
        }
        auto relative = asset.virtual_path;
        if (relative.starts_with(scripts_prefix))
        {
            relative.erase(0, scripts_prefix.size());
        }
        const auto separator = relative.find('/');
        ++groups[separator == std::string::npos ? "<root>" : relative.substr(0, separator)];

        auto decoded = decode_asset<eaw::LuaChunkData>(vfs, asset.virtual_path);
        if (!decoded)
        {
            failures.emplace_back(asset.virtual_path, decoded.error().message);
            continue;
        }
        auto info = std::move(decoded).value();
        ++kinds[std::string(eaw::to_string(info.kind))];
        total_lines += info.lines;
        total_prototypes += info.prototypes;
        total_instructions += info.instructions;
        warning_count += info.warnings.size();
        for (const auto& dependency : info.dependencies)
        {
            ++dependencies[dependency];
            dependency_users[dependency].insert(asset.virtual_path);
        }
        for (const auto& definition : info.defined_functions)
        {
            ++definitions[definition];
            const auto folded = ascii_lower(definition);
            definition_keys.insert(folded);
            const auto leaf = folded.find_last_of(".:");
            definition_keys.insert(leaf == std::string::npos ? folded : folded.substr(leaf + 1U));
        }
        for (const auto& call : info.called_functions)
        {
            ++calls[call];
        }
        scripts.push_back({asset.virtual_path, std::move(info)});
    }

    const auto dependency_resolved = [&](std::string dependency)
    {
        dependency = ascii_lower(std::move(dependency));
        std::ranges::replace(dependency, '\\', '/');
        if (dependency.starts_with("data/scripts/"))
        {
            dependency.erase(0, 13);
        }
        if (dependency.ends_with(".lua"))
        {
            dependency.resize(dependency.size() - 4U);
        }
        if (module_keys.contains(dependency))
        {
            return true;
        }
        auto path_form = dependency;
        std::ranges::replace(path_form, '.', '/');
        if (module_keys.contains(path_form))
        {
            return true;
        }
        return module_keys.contains(ascii_lower(std::filesystem::path(dependency).stem().string()));
    };

    std::map<std::string, std::set<std::string>> unresolved_dependencies;
    for (const auto& [dependency, users] : dependency_users)
    {
        if (!dependency_resolved(dependency))
        {
            unresolved_dependencies[dependency] = users;
        }
    }

    const auto standard_library = [](const std::string_view call)
    {
        const auto separator = call.find_first_of(".:");
        const auto root = ascii_lower(std::string(call.substr(0, separator)));
        static const std::set<std::string> standard{"assert",     "collectgarbage", "coroutine",    "dofile", "error", "gcinfo",   "getfenv",  "getmetatable", "ipairs", "loadfile",
                                                    "loadstring", "math",           "next",         "pairs",  "pcall", "print",    "rawequal", "rawget",       "rawset", "require",
                                                    "select",     "setfenv",        "setmetatable", "string", "table", "tonumber", "tostring", "type",         "unpack", "xpcall"};
        return standard.contains(root);
    };
    std::map<std::string, std::size_t> host_api_calls;
    for (const auto& [call, count] : calls)
    {
        const auto folded = ascii_lower(call);
        const auto leaf = folded.find_last_of(".:");
        const auto leaf_name = leaf == std::string::npos ? folded : folded.substr(leaf + 1U);
        if (!standard_library(call) && !definition_keys.contains(folded) && !definition_keys.contains(leaf_name))
        {
            host_api_calls[call] = count;
        }
    }

    const auto sorted = [](const std::map<std::string, std::size_t>& values)
    {
        std::vector<std::pair<std::string, std::size_t>> result(values.begin(), values.end());
        std::ranges::sort(result,
                          [](const auto& left, const auto& right)
                          {
                              return left.second != right.second ? left.second > right.second : left.first < right.first;
                          });
        return result;
    };

    std::cout << "Lua compatibility audit\n"
              << "Scripts: " << scripts.size() + failures.size() << '\n'
              << "Inspected: " << scripts.size() << '\n'
              << "Failures: " << failures.size() << '\n'
              << "Warnings: " << warning_count << '\n'
              << "Source lines: " << total_lines << '\n'
              << "Bytecode prototypes: " << total_prototypes << '\n'
              << "Bytecode instructions: " << total_instructions << '\n'
              << "Unique dependencies: " << dependencies.size() << '\n'
              << "Unresolved dependencies: " << unresolved_dependencies.size() << '\n'
              << "Defined function names: " << definitions.size() << '\n'
              << "Called function names: " << calls.size() << '\n'
              << "Candidate host API calls: " << host_api_calls.size() << "\n\nFormats:\n";
    for (const auto& [kind, count] : kinds)
    {
        std::cout << "  " << kind << ": " << count << '\n';
    }
    std::cout << "Script groups:\n";
    for (const auto& [group, count] : groups)
    {
        std::cout << "  " << group << ": " << count << '\n';
    }
    std::cout << "Most-used dependencies:\n";
    const auto sorted_dependencies = sorted(dependencies);
    for (std::size_t index = 0; index < std::min(limit, sorted_dependencies.size()); ++index)
    {
        std::cout << "  " << sorted_dependencies[index].second << "  " << sorted_dependencies[index].first << '\n';
    }
    std::cout << "Candidate host API calls (scripts using each):\n";
    const auto sorted_calls = sorted(host_api_calls);
    for (std::size_t index = 0; index < std::min(limit, sorted_calls.size()); ++index)
    {
        std::cout << "  " << sorted_calls[index].second << "  " << sorted_calls[index].first << '\n';
    }
    std::cout << "Most-common defined functions:\n";
    const auto sorted_definitions = sorted(definitions);
    for (std::size_t index = 0; index < std::min(limit, sorted_definitions.size()); ++index)
    {
        std::cout << "  " << sorted_definitions[index].second << "  " << sorted_definitions[index].first << '\n';
    }
    std::cout << "Unresolved dependencies:\n";
    std::size_t shown = 0;
    for (const auto& [dependency, users] : unresolved_dependencies)
    {
        if (shown++ >= limit)
        {
            break;
        }
        std::cout << "  " << dependency << "  (" << users.size() << " script(s))";
        if (!users.empty())
        {
            std::cout << "  e.g. " << *users.begin();
        }
        std::cout << '\n';
    }
    std::cout << "Inspection failures:\n";
    for (std::size_t index = 0; index < std::min(limit, failures.size()); ++index)
    {
        std::cout << "  " << failures[index].first << ": " << failures[index].second << '\n';
    }
}

} // namespace eaw::inspector
