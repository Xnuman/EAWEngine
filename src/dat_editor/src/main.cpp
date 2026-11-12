// EAWDatEditor entry point: parses the command line and dispatches to a command (dat_editor.hpp).

#include "dat_editor/dat_editor.hpp"

#include <array>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using Command = eaw::Result<int> (*)(const eaw::dat_editor::Arguments&);

constexpr std::array<std::pair<std::string_view, Command>, 11> commands{{
    {"info", &eaw::dat_editor::info_command},
    {"list", &eaw::dat_editor::list_command},
    {"get", &eaw::dat_editor::get_command},
    {"set", &eaw::dat_editor::set_command},
    {"remove", &eaw::dat_editor::remove_command},
    {"validate", &eaw::dat_editor::validate_command},
    {"export", &eaw::dat_editor::export_command},
    {"diff", &eaw::dat_editor::diff_command},
    {"build", &eaw::dat_editor::build_command},
    {"scan-mod", &eaw::dat_editor::scan_mod_command},
    {"add-unit", &eaw::dat_editor::add_unit_command},
}};

// Runs one command and returns its exit code; failures are returned for main to report.
eaw::Result<int> run(const int argc, char** argv)
{
    if (argc >= 2)
    {
        const std::string_view command = argv[1];
        if (command == "--help" || command == "-h" || command == "help")
        {
            eaw::dat_editor::print_usage();
            return 0;
        }
    }
    EAW_TRY_ASSIGN(const auto arguments, eaw::dat_editor::parse_arguments(argc, argv));
    if (!arguments)
    {
        eaw::dat_editor::print_usage();
        return 1;
    }
    for (const auto& [name, handler] : commands)
    {
        if (arguments->command == name)
        {
            return handler(*arguments);
        }
    }
    return eaw::make_error("unknown command: " + arguments->command);
}

} // namespace

int main(const int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    const auto result = run(argc, argv);
    return result ? *result : eaw::dat_editor::report_error(result.error());
}
