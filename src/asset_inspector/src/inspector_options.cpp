// Command line of EAWAssetInspector.

#include "core/string_utils.hpp"
#include "asset_inspector/inspector.hpp"

namespace eaw::inspector {
namespace {

eaw::Result<std::size_t> parse_size(const std::string& value)
{
    std::size_t parsed = 0;
    const auto* const end = value.data() + value.size();
    const auto [position, error] = std::from_chars(value.data(), end, parsed);
    if (value.empty() || error != std::errc{} || position != end)
    {
        return eaw::make_error("invalid number: " + value);
    }
    return parsed;
}

} // namespace

void usage()
{
    std::cout << "EAWAssetInspector - layered Empire at War asset browser\n\n"
              << "Usage:\n"
              << "  EAWAssetInspector summary [options]\n"
              << "  EAWAssetInspector list [prefix] [options]\n"
              << "  EAWAssetInspector inspect <virtual-path> [options]\n"
              << "  EAWAssetInspector map-audit [prefix] [options]\n"
              << "  EAWAssetInspector map-objects [prefix] [options]\n"
              << "  EAWAssetInspector texture-audit [prefix] [options]\n"
              << "  EAWAssetInspector lua-audit [prefix] [options]\n"
              << "  EAWAssetInspector particle-audit [prefix] [options]\n"
              << "  EAWAssetInspector xml-audit [prefix] [options]\n"
              << "  EAWAssetInspector localization-refs <xml-path> [options]\n"
              << "  EAWAssetInspector extract [prefix] --output <folder> [options]\n"
              << "  EAWAssetInspector spawn-types [options]\n"
              << "  EAWAssetInspector sound-audit [options]\n\n"
              << "Options:\n"
              << "  --game-root <path>      Installation root (auto-detected by default)\n"
              << "  --game <eaw|foc>        Game profile (default: foc)\n"
              << "  --schema-root <path>    Optional eaw-schema checkout for XML validation\n"
              << "  --mod <path>            Mod root; repeatable, first has priority\n"
              << "  --language <name>       Language of text and speech archives (default: English)\n"
              << "  --prefix <path>         Summary/list prefix (default: Data)\n"
              << "  --extension <.xml>      Filter list by extension\n"
              << "  --limit <count>         Maximum list/sample rows (not used by extract)\n"
              << "  --output <folder>       extract: folder to write the files to (outside the game and mods)\n"
              << "  --overwrite             extract: replace files that already exist there\n";
}

eaw::Result<Options> parse_options(const int argc, char** argv)
{
    Options options;
    int index = 1;
    if (index < argc && argv[index][0] != '-')
    {
        options.command = argv[index++];
    }
    if ((options.command == "inspect" || options.command == "localization-refs") && index < argc && argv[index][0] != '-')
    {
        options.operand = argv[index++];
    }
    else if ((options.command == "list" || options.command == "map-audit" || options.command == "map-objects" || options.command == "texture-audit" || options.command == "lua-audit" ||
              options.command == "particle-audit" || options.command == "xml-audit" || options.command == "extract") &&
             index < argc && argv[index][0] != '-')
    {
        options.prefix = argv[index++];
    }

    for (; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h")
        {
            usage();
            std::exit(0);
        }
        if (argument == "--overwrite")
        {
            options.overwrite = true;
            continue;
        }
        if (index + 1 >= argc)
        {
            return eaw::make_error("missing value after " + argument);
        }
        const std::string value = argv[++index];
        if (argument == "--game-root")
        {
            options.game_root = value;
        }
        else if (argument == "--game")
        {
            const auto variant = eaw::parse_game_variant(value);
            if (!variant)
            {
                return eaw::make_error("unknown game variant: " + value);
            }
            options.variant = *variant;
        }
        else if (argument == "--schema-root")
        {
            options.schema_root = value;
        }
        else if (argument == "--mod")
        {
            options.mods.emplace_back(value);
        }
        else if (argument == "--prefix")
        {
            options.prefix = value;
        }
        else if (argument == "--extension")
        {
            options.extension = value;
            if (!options.extension.empty() && options.extension.front() != '.')
            {
                options.extension.insert(options.extension.begin(), '.');
            }
            options.extension = ascii_lower(options.extension);
        }
        else if (argument == "--language")
        {
            options.language = value;
        }
        else if (argument == "--output")
        {
            options.output = value;
        }
        else if (argument == "--limit")
        {
            EAW_TRY_ASSIGN(options.limit, parse_size(value));
        }
        else
        {
            return eaw::make_error("unknown option: " + argument);
        }
    }
    if ((options.command == "inspect" || options.command == "localization-refs") && options.operand.empty())
    {
        return eaw::make_error(options.command + " requires a virtual asset path");
    }
    return options;
}

} // namespace eaw::inspector
