// `extract`: copies the winning file of every virtual path under a prefix into an output folder
// the user names, at its virtual path, so archived data (scripts, XML) can be read with ordinary
// tools. The installation and mods are only read: an output folder inside one of them is refused.
// Archive entry names are untrusted (a mod's MEG may hold any name): one asset_output_path refuses
// is skipped with a diagnostic instead of written.

#include "asset_inspector/inspector.hpp"
#include "core/file_io.hpp"
#include "vfs/asset_path.hpp"

namespace eaw::inspector {
namespace {

// Whether `path` is `folder` or lies inside it (both made absolute and normalized first).
bool is_within(const std::filesystem::path& path, const std::filesystem::path& folder)
{
    std::error_code error;
    const auto absolute_path = std::filesystem::weakly_canonical(path, error);
    const auto absolute_folder = std::filesystem::weakly_canonical(folder, error);
    if (error)
    {
        // Undecidable: treat as inside, so nothing is written there.
        return true;
    }
    const auto relative = absolute_path.lexically_relative(absolute_folder);
    return !relative.empty() && *relative.begin() != "..";
}

} // namespace

eaw::Status extract_assets(const eaw::VirtualFileSystem& vfs, const Options& options, const std::vector<std::filesystem::path>& read_only_roots)
{
    if (!options.output)
    {
        return eaw::make_error("extract requires --output <folder>");
    }
    for (const auto& root : read_only_roots)
    {
        if (is_within(*options.output, root))
        {
            return eaw::make_error("the output folder " + options.output->string() + " is inside " + root.string() + ", which is only read");
        }
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
    const auto mode = options.overwrite ? eaw::WriteMode::replace : eaw::WriteMode::create_only;
    std::size_t written = 0;
    std::size_t failed = 0;
    for (const auto& asset : assets)
    {
        const auto report = [&](const eaw::Error& error)
        {
            std::cerr << "warning: " << asset.virtual_path << " [" << asset.mount_name << "]: " << error.message << '\n';
            ++failed;
        };
        const auto relative = eaw::asset_output_path(asset.virtual_path);
        if (!relative)
        {
            report(relative.error());
            continue;
        }
        const auto bytes = vfs.read(asset.virtual_path);
        if (!bytes)
        {
            report(bytes.error());
            continue;
        }
        if (const auto status = eaw::write_file(*options.output / *relative, *bytes, mode); !status)
        {
            report(status.error());
            continue;
        }
        ++written;
    }
    std::cout << "Extracted " << written << " of " << assets.size() << " files under " << options.prefix << " to " << options.output->string() << '\n';
    if (failed > 0)
    {
        return eaw::make_error(std::to_string(failed) + " files could not be extracted" + (options.overwrite ? "" : " (existing files are kept; --overwrite replaces them)"));
    }
    return {};
}

} // namespace eaw::inspector
