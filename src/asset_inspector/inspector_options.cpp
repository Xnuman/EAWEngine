// Command line of EAWAssetInspector.

#include "inspector.hpp"

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
              << "  EAWAssetInspector localization-refs <xml-path> [options]\n\n"
              << "Options:\n"
              << "  --game-root <path>      Installation root (auto-detected by default)\n"
              << "  --game <eaw|foc>        Game profile (default: foc)\n"
              << "  --schema-root <path>    Optional eaw-schema checkout for XML validation\n"
              << "  --mod <path>            Mod root; repeatable, first has priority\n"
              << "  --prefix <path>         Summary/list prefix (default: Data)\n"
              << "  --extension <.xml>      Filter list by extension\n"
              << "  --limit <count>         Maximum list/sample rows\n";
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
              options.command == "particle-audit" || options.command == "xml-audit") &&
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
            std::ranges::transform(options.extension, options.extension.begin(),
                                   [](const unsigned char character)
                                   {
                                       return static_cast<char>(std::tolower(character));
                                   });
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
