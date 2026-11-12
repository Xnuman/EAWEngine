#include "test_support.hpp"

#include <crtdbg.h>

#include <algorithm>
#include <iostream>
#include <string_view>

// Runs every registered test, or only the modules or tests named on the command line
// (`EAWEngineTests simulation engine`, `EAWEngineTests test_fighter_flight`). Modules run in name order and tests in their registration
// order, so the sequence does not depend on static initialization order. A failed requirement
// ends the run.
int main(const int argc, char** argv)
{
    // Debug-CRT assertions (checked indexing) report on stderr instead of a dialog box that would
    // block unattended runs; the failing run then aborts.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    auto tests = eaw_test::registry();
    std::ranges::stable_sort(tests,
                             [](const eaw_test::TestCase& left, const eaw_test::TestCase& right)
                             {
                                 return std::string_view(left.module) < std::string_view(right.module);
                             });
    std::size_t ran = 0;
    for (const auto& test : tests)
    {
        bool selected = argc < 2;
        for (int index = 1; index < argc; ++index)
        {
            selected = selected || std::string_view(argv[index]) == test.module || std::string_view(argv[index]) == test.name;
        }
        if (!selected)
        {
            continue;
        }
        test.run();
        ++ran;
    }
    if (ran == 0)
    {
        std::cerr << "No tests matched the requested modules or tests.\n";
        return 1;
    }
    std::cout << "All " << ran << " EAWEngine tests passed.\n";
    return 0;
}
