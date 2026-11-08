#include "inspector.hpp"

namespace eaw::inspector {
namespace {

// Runs the selected command and returns its exit code; failures are returned for main to report.
eaw::Result<int> run(const Options& options)
{
    const eaw::InstallationLocator locator;
    const auto installations = locator.discover(options.game_root);
    if (installations.empty())
    {
        return eaw::make_error("no valid Empire at War installation was found");
    }
    const auto profile = eaw::make_game_profile(installations.front(), options.variant);
    if (!profile)
    {
        return eaw::make_error("the selected game profile is unavailable");
    }

    eaw::VirtualFileSystem vfs;
    const auto report = eaw::mount_game_assets(vfs, *profile, options.mods);
    for (const auto& warning : report.warnings)
    {
        std::cerr << "warning: " << warning << '\n';
    }

    if (options.command == "inspect")
    {
        const auto status = inspect_asset(vfs, options.operand, options.limit);
        return status ? 0 : report_error(status.error());
    }
    if (options.command == "localization-refs")
    {
        const auto read = vfs.read(options.operand);
        if (!read)
        {
            return report_error(read.error());
        }
        const auto& bytes = *read;
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const auto references = localization_references(text);
        for (const auto& reference : references)
        {
            std::cout << reference << '\n';
        }
        std::cerr << references.size() << " unique localization references\n";
        return 0;
    }
    if (options.command == "texture-audit")
    {
        audit_texture_references(vfs, options.prefix, options.limit);
        return 0;
    }
    if (options.command == "lua-audit")
    {
        audit_lua(vfs, options.prefix, options.limit);
        return 0;
    }
    if (options.command == "map-objects")
    {
        audit_map_objects(vfs, options.prefix, options.limit);
        return 0;
    }
    if (options.command == "map-audit")
    {
        audit_maps(vfs, options.prefix, options.limit);
        return 0;
    }
    if (options.command == "xml-audit")
    {
        const auto status = audit_xml(vfs, options.prefix, options.limit, options.schema_root, options.variant == eaw::GameVariant::forces_of_corruption);
        return status ? 0 : report_error(status.error());
    }
    if (options.command != "summary" && options.command != "list")
    {
        return eaw::make_error("unknown command: " + options.command);
    }

    auto assets = vfs.list(options.prefix);
    if (!options.extension.empty())
    {
        std::erase_if(assets,
                      [&](const auto& asset)
                      {
                          return extension_of(asset.virtual_path) != options.extension;
                      });
    }
    if (options.command == "list")
    {
        for (std::size_t index = 0; index < std::min(options.limit, assets.size()); ++index)
        {
            const auto& asset = assets[index];
            std::cout << std::setw(12) << asset.size << "  " << asset.virtual_path << "  [" << asset.mount_name << "]\n";
        }
        std::cout.flush();
        std::cerr << "showing " << std::min(options.limit, assets.size()) << " of " << assets.size() << " assets\n";
        return 0;
    }

    std::map<std::string, std::pair<std::size_t, std::uint64_t>> by_extension;
    std::map<std::string, std::size_t> by_mount;
    std::uint64_t total_bytes = 0;
    for (const auto& asset : assets)
    {
        auto& group = by_extension[extension_of(asset.virtual_path)];
        ++group.first;
        group.second += asset.size;
        ++by_mount[asset.mount_name];
        total_bytes += asset.size;
    }
    std::cout << "Game: " << eaw::to_string(profile->variant) << '\n'
              << "Installation: " << profile->root << '\n'
              << "Prefix: " << options.prefix << '\n'
              << "Resolved assets: " << assets.size() << '\n'
              << "Logical bytes: " << total_bytes << "\n\nBy extension:\n";
    for (const auto& [extension, values] : by_extension)
    {
        std::cout << "  " << std::setw(8) << extension << "  " << std::setw(7) << values.first << " files  " << values.second << " bytes\n";
    }
    std::cout << "\nWinning source mounts:\n";
    for (const auto& [mount, count] : by_mount)
    {
        std::cout << "  " << std::setw(7) << count << "  " << mount << '\n';
    }
    return 0;
}

} // namespace
} // namespace eaw::inspector

int main(const int argc, char** argv)
{
    using namespace eaw::inspector;
    const auto options = parse_options(argc, argv);
    if (!options)
    {
        return report_error(options.error());
    }
    const auto result = run(*options);
    return result ? *result : report_error(result.error());
}
