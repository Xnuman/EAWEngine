#include "platform/input_bindings.hpp"

#include "core/fatal_error.hpp"
#include "core/string_utils.hpp"

namespace eaw {

InputBindings::InputBindings(const std::span<const InputAction> actions) : actions_(actions)
{
    reset();
}

const std::array<std::optional<Key>, keys_per_action>& InputBindings::keys(const std::size_t action) const
{
    EAW_ASSERT_MSG(action < keys_.size(), "an input action outside the bindings' list");
    return keys_[action];
}

void InputBindings::set(const std::size_t action, const std::size_t slot, const std::optional<Key> key)
{
    EAW_ASSERT_MSG(action < keys_.size() && slot < keys_per_action, "an input action or key slot outside the bindings' list");
    keys_[action][slot] = key;
}

void InputBindings::reset()
{
    keys_.clear();
    for (const auto& action : actions_)
    {
        keys_.push_back(action.defaults);
    }
}

bool InputBindings::down(const std::size_t action, const InputState& input) const
{
    for (const auto& key : keys(action))
    {
        if (key && input.keys[static_cast<std::size_t>(*key)].down)
        {
            return true;
        }
    }
    return false;
}

bool InputBindings::pressed(const std::size_t action, const InputState& input) const
{
    for (const auto& key : keys(action))
    {
        if (key && input.keys[static_cast<std::size_t>(*key)].pressed)
        {
            return true;
        }
    }
    return false;
}

std::vector<std::string> InputBindings::read(const std::string_view text)
{
    std::vector<std::string> problems;
    std::size_t line_number = 0;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const auto end = std::min(text.find('\n', start), text.size());
        const auto line = ascii_trim(text.substr(start, end - start));
        start = end + 1;
        ++line_number;
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        const auto where = "line " + std::to_string(line_number) + ": ";
        const auto equals = line.find('=');
        if (equals == std::string_view::npos)
        {
            problems.push_back(where + "no '=' between an action and its keys");
            continue;
        }
        const auto name = ascii_trim(line.substr(0, equals));
        std::size_t action = 0;
        while (action < actions_.size() && !ascii_equal_ignoring_case(actions_[action].name, name))
        {
            ++action;
        }
        if (action == actions_.size())
        {
            problems.push_back(where + "unknown action '" + std::string(name) + "'");
            continue;
        }
        std::array<std::optional<Key>, keys_per_action> keys{};
        std::size_t slot = 0;
        bool readable = true;
        auto list = line.substr(equals + 1);
        while (readable)
        {
            const auto comma = list.find(',');
            const auto item = ascii_trim(list.substr(0, comma));
            if (!item.empty())
            {
                const auto key = key_from_name(item);
                if (!key)
                {
                    problems.push_back(where + "unknown key '" + std::string(item) + "' for '" + std::string(name) + "'");
                    readable = false;
                }
                else if (slot == keys_per_action)
                {
                    problems.push_back(where + "more than " + std::to_string(keys_per_action) + " keys for '" + std::string(name) + "'");
                    readable = false;
                }
                else
                {
                    keys[slot++] = key;
                }
            }
            if (comma == std::string_view::npos)
            {
                break;
            }
            list = list.substr(comma + 1);
        }
        // A line with a key it cannot read leaves the action as it was.
        if (readable)
        {
            keys_[action] = keys;
        }
    }
    return problems;
}

std::string InputBindings::write() const
{
    std::string text = "# EAWEngine input bindings: action = key, key (names as in the Controls panel).\n";
    for (std::size_t action = 0; action < actions_.size(); ++action)
    {
        text += std::string(actions_[action].name) + " =";
        bool first = true;
        for (const auto& key : keys_[action])
        {
            if (key)
            {
                text += std::string(first ? " " : ", ") + std::string(key_name(*key));
                first = false;
            }
        }
        text += '\n';
    }
    return text;
}

} // namespace eaw
