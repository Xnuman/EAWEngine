#include "test_support.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>

// Runs every registered test, or only the modules named on the command line
// (`EAWEngineTests simulation engine`). Modules run in name order and tests in their registration
// order, so the sequence does not depend on static initialization order. A failed requirement
// ends the run.
int main(const int argc, char** argv)
{
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
            selected = selected || std::string_view(argv[index]) == test.module;
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
        std::cerr << "No tests matched the requested modules.\n";
        return 1;
    }
    std::cout << "All " << ran << " EAWEngine tests passed.\n";
    return 0;
}
