#include "test_support.hpp"

#include "platform/input_bindings.hpp"
#include "platform/keys.hpp"

#include <array>
#include <string>

namespace {

using namespace eaw_test;

// Every key has a distinct name that reads back as the key, ignoring case; unknown names read as
// nothing.
void test_key_names()
{
    for (std::size_t index = 0; index < eaw::key_count; ++index)
    {
        const auto key = static_cast<eaw::Key>(index);
        const auto name = eaw::key_name(key);
        require(!name.empty() && eaw::key_from_name(name) == key, "every key's name must read back as the key (index " + std::to_string(index) + ")");
    }
    require(eaw::key_from_name("PAGE_UP") == eaw::Key::page_up && eaw::key_from_name("F12") == eaw::Key::f12 && eaw::key_from_name("7") == eaw::Key::digit7, "key names must read ignoring case");
    require(!eaw::key_from_name("hyper") && !eaw::key_from_name("") && eaw::key_name(eaw::Key::count).empty(), "unknown key names must read as nothing");
}

const std::array<eaw::InputAction, 3> actions{{
    {"pan_forward", "Pan the camera forward", {eaw::Key::w, eaw::Key::up}},
    {"pause", "Pause the world", {eaw::Key::p, std::nullopt}},
    {"spawn", "Spawn at the cursor", {eaw::Key::n, std::nullopt}},
}};

// Bindings start at their defaults, answer down and pressed from the input, read text over
// themselves (case, spaces, comments, and unbinding), report what they cannot read without changing
// those actions, and write text that reads back the same.
void test_input_bindings()
{
    eaw::InputBindings bindings(actions);
    eaw::InputState input;
    input.keys[static_cast<std::size_t>(eaw::Key::up)].down = true;
    input.keys[static_cast<std::size_t>(eaw::Key::p)].pressed = true;
    require(bindings.down(0, input) && !bindings.down(1, input) && bindings.pressed(1, input) && !bindings.pressed(2, input), "an action must answer from its default keys");

    const auto problems = bindings.read("# my keys\n  PAN_FORWARD =  i , page_up \nspawn =\npause = p, q, e\nfly = f\nnonsense\npause = hyper\n");
    require(problems.size() == 4, "every line it cannot read must be reported (" + std::to_string(problems.size()) + ")");
    require(problems[0].starts_with("line 4:") && problems[1].starts_with("line 5:") && problems[2].starts_with("line 6:") && problems[3].starts_with("line 7:"), "problems must name their lines");
    require(bindings.keys(0)[0] == eaw::Key::i && bindings.keys(0)[1] == eaw::Key::page_up, "an action's keys must read from its line");
    require(!bindings.keys(2)[0] && !bindings.keys(2)[1], "an action with no keys after '=' must be unbound");
    require(bindings.keys(1)[0] == eaw::Key::p && !bindings.keys(1)[1], "an action whose line cannot be read must keep its keys");

    const auto text = bindings.write();
    eaw::InputBindings copy(actions);
    require(copy.read(text).empty(), "written bindings must read back without problems");
    for (std::size_t action = 0; action < actions.size(); ++action)
    {
        require(copy.keys(action) == bindings.keys(action), "written bindings must read back the same");
    }
    bindings.reset();
    require(bindings.keys(0)[0] == eaw::Key::w && bindings.keys(2)[0] == eaw::Key::n, "reset must restore the defaults");
}

const ModuleRegistrar registrar("input", {
                                             {"test_key_names", &test_key_names},
                                             {"test_input_bindings", &test_input_bindings},
                                         });

} // namespace
