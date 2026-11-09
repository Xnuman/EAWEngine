#include "dat_editor.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <iostream>

namespace eaw::dat_editor {
namespace {

bool take_flag(std::vector<std::string>& arguments, const std::string_view flag)
{
    const auto found = std::ranges::find(arguments, flag);
    if (found == arguments.end())
    {
        return false;
    }
    arguments.erase(found);
    return true;
}

Result<std::optional<std::string>> take_option(std::vector<std::string>& arguments, const std::string_view option)
{
    const auto found = std::ranges::find(arguments, option);
    if (found == arguments.end())
    {
        return std::optional<std::string>{};
    }
    if (found + 1 == arguments.end())
    {
        return make_error("missing value after " + std::string(option));
    }
    const auto value = *(found + 1);
    arguments.erase(found, found + 2);
    return std::optional<std::string>(value);
}

Result<std::size_t> parse_size(const std::string& value)
{
    std::size_t parsed = 0;
    const auto* const end = value.data() + value.size();
    const auto [position, error] = std::from_chars(value.data(), end, parsed);
    if (value.empty() || error != std::errc{} || position != end)
    {
        return make_error("invalid number: " + value);
    }
    return parsed;
}

} // namespace

void print_usage()
{
    std::cout << "EAWDatEditor - safe Petroglyph DAT localization workflow\n\n"
              << "Usage:\n"
              << "  EAWDatEditor info <file.dat>\n"
              << "  EAWDatEditor list <file.dat> [--filter text] [--limit count]\n"
              << "  EAWDatEditor get <file.dat> <key>\n"
              << "  EAWDatEditor set <input.dat> <output.dat> <key> <text> [--force]\n"
              << "  EAWDatEditor remove <input.dat> <output.dat> <key> [--force]\n"
              << "  EAWDatEditor validate <file.dat> [--reference original.dat]\n"
              << "  EAWDatEditor export <file.dat> <workspace.tsv> [--force]\n"
              << "  EAWDatEditor diff <base.dat> <edited.dat> <workspace.tsv> [--force]\n"
              << "  EAWDatEditor build <base.dat> <workspace.tsv> <output.dat>\n"
              << "                     [--allow-todo] [--force]\n"
              << "  EAWDatEditor scan-mod <mod-root> <base.dat> <workspace.tsv> [--force]\n"
              << "  EAWDatEditor add-unit <workspace.tsv> <unit-id> <name> <description>\n"
              << "                        [--force]\n\n"
              << "Workspace columns are Key, Text, Group, Status, Context, and Notes.\n"
              << "Status 'todo' blocks builds unless --allow-todo is supplied; 'deleted'\n"
              << "removes a key. Output files are never replaced without --force.\n";
}

Result<std::optional<Arguments>> parse_arguments(const int argc, char** argv)
{
    if (argc < 2)
    {
        return std::optional<Arguments>{};
    }
    Arguments parsed;
    parsed.command = argv[1];
    parsed.positional.assign(argv + 2, argv + argc);
    parsed.force = take_flag(parsed.positional, "--force");
    parsed.allow_todo = take_flag(parsed.positional, "--allow-todo");
    EAW_TRY_ASSIGN(parsed.filter, take_option(parsed.positional, "--filter"));
    EAW_TRY_ASSIGN(const auto limit, take_option(parsed.positional, "--limit"));
    EAW_TRY_ASSIGN(parsed.reference, take_option(parsed.positional, "--reference"));
    if (limit)
    {
        EAW_TRY_ASSIGN(parsed.limit, parse_size(*limit));
    }
    return std::optional<Arguments>(std::move(parsed));
}

int report_error(const Error& error)
{
    std::cerr << "DAT editor error: " << error.message << '\n';
    return 1;
}

std::string lower(std::string value)
{
    std::ranges::transform(value, value.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return value;
}

} // namespace eaw::dat_editor
