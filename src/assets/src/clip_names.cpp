#include "assets/clip_names.hpp"

#include "core/string_utils.hpp"

#include <array>
#include <charconv>
#include <system_error>
#include <utility>

namespace eaw {

namespace {

// Stopping clips carry their variant in the name, not the number.
constexpr std::array<std::string_view, 4> stop_names{"move_endone", "move_endtwo", "move_endthree", "move_endfour"};
constexpr std::array<std::pair<std::string_view, AnimationRole>, 14> role_names{{
    {"idle", AnimationRole::idle},
    {"move", AnimationRole::move},
    {"walkmove", AnimationRole::walk_move},
    {"attack", AnimationRole::attack},
    {"attackidle", AnimationRole::attack_idle},
    {"die", AnimationRole::die},
    {"crushed", AnimationRole::crushed},
    {"turnl", AnimationRole::turn_left},
    {"turnr", AnimationRole::turn_right},
    {"turnl_begin", AnimationRole::turn_left_begin},
    {"turnl_end", AnimationRole::turn_left_end},
    {"turnr_begin", AnimationRole::turn_right_begin},
    {"turnr_end", AnimationRole::turn_right_end},
    {"movestart", AnimationRole::move_start},
}};

} // namespace

std::optional<ClipName> parse_clip_name(const std::string_view file_stem, const std::string_view model_stem)
{
    const auto file = ascii_lower(std::string(file_stem));
    const auto prefix = ascii_lower(std::string(model_stem)) + "_";
    if (!file.starts_with(prefix))
    {
        return std::nullopt;
    }
    const auto rest = std::string_view(file).substr(prefix.size());
    const auto underscore = rest.rfind('_');
    if (underscore == std::string_view::npos || underscore + 1U >= rest.size())
    {
        return std::nullopt;
    }
    const auto digits = rest.substr(underscore + 1U);
    std::uint32_t variant{};
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), variant);
    if (error != std::errc{} || end != digits.data() + digits.size())
    {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < stop_names.size(); ++index)
    {
        if (rest.substr(0, underscore) == stop_names[index])
        {
            return ClipName{AnimationRole::move_end, static_cast<std::uint32_t>(index)};
        }
    }
    const auto role = rest.substr(0, underscore);
    for (const auto& [name, value] : role_names)
    {
        if (role == name)
        {
            return ClipName{value, variant};
        }
    }
    return std::nullopt;
}

std::string clip_file_stem(const std::string_view model_stem, const AnimationRole role, const std::uint32_t variant)
{
    std::string_view name;
    std::uint32_t number = variant;
    if (role == AnimationRole::move_end)
    {
        if (variant >= stop_names.size())
        {
            return {};
        }
        name = stop_names[variant];
        number = 0;
    }
    else
    {
        for (const auto& [text, value] : role_names)
        {
            if (value == role)
            {
                name = text;
            }
        }
    }
    const auto digits = std::to_string(number);
    return ascii_upper(std::string(model_stem) + "_" + std::string(name) + "_" + (digits.size() < 2 ? "0" : "") + digits);
}

} // namespace eaw
