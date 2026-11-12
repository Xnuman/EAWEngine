#pragma once

// EAWDatEditor: the command line over eaw_formats' DAT tables and eaw_localization's workspaces. Commands
// are grouped by what they work on: dat_commands.cpp (one DAT file) and workspace_commands.cpp
// (TSV workspaces and mods); dat_editor_options.cpp parses the command line.

#include "core/status.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eaw::dat_editor {

struct Arguments
{
    std::string command;
    // Positional arguments after the command, options removed.
    std::vector<std::string> positional;
    bool force{};
    bool allow_todo{};
    std::optional<std::string> filter;
    std::optional<std::string> reference;
    std::size_t limit{200};
};

void print_usage();
// Parses argv; nullopt when there is no command (usage is printed by the caller).
[[nodiscard]] Result<std::optional<Arguments>> parse_arguments(int argc, char** argv);

// Prints "DAT editor error: ..." and returns exit code 1.
int report_error(const Error& error);
// An error carrying `usage` unless the command got exactly `count` positional arguments.
[[nodiscard]] Status require_count(const Arguments& arguments, std::size_t count, std::string_view usage);

// Commands return their exit code (0 success, 2 validation errors, 3 key not found); failures that
// are not a command's own outcome are errors.
[[nodiscard]] Result<int> info_command(const Arguments& arguments);
[[nodiscard]] Result<int> list_command(const Arguments& arguments);
[[nodiscard]] Result<int> get_command(const Arguments& arguments);
[[nodiscard]] Result<int> set_command(const Arguments& arguments);
[[nodiscard]] Result<int> remove_command(const Arguments& arguments);
[[nodiscard]] Result<int> validate_command(const Arguments& arguments);
[[nodiscard]] Result<int> export_command(const Arguments& arguments);
[[nodiscard]] Result<int> diff_command(const Arguments& arguments);
[[nodiscard]] Result<int> build_command(const Arguments& arguments);
[[nodiscard]] Result<int> scan_mod_command(const Arguments& arguments);
[[nodiscard]] Result<int> add_unit_command(const Arguments& arguments);

} // namespace eaw::dat_editor
