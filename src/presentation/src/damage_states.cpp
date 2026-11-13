#include "presentation/damage_states.hpp"

#include "assets/type_fields.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {
namespace {

// Alternates beyond this are taken for typing errors (stock uses 0-3, the models up to 12).
constexpr float maximum_alternate = 255.0F;

} // namespace

std::vector<DamageStep> type_damage_states(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::vector<std::string>* warnings)
{
    const TypeFields type(catalog, definition, warnings);
    const auto thresholds = type.list("Land_Damage_Thresholds");
    const auto alternates = type.list("Land_Damage_Alternates");
    std::vector<DamageStep> steps;
    for (std::size_t index = 0; index < thresholds.size() && index < alternates.size(); ++index)
    {
        const auto threshold = parse_xml_number(thresholds[index]);
        const auto alternate = parse_xml_number(alternates[index]);
        if (!threshold || !std::isfinite(*threshold))
        {
            type.warn("Land_Damage_Thresholds", thresholds[index], "a health fraction");
            break;
        }
        if (!alternate || *alternate < 0.0F || *alternate > maximum_alternate || *alternate != std::floor(*alternate))
        {
            type.warn("Land_Damage_Alternates", alternates[index], "an alternate number");
            break;
        }
        steps.push_back({*threshold, static_cast<std::uint32_t>(*alternate)});
    }
    return steps;
}

std::optional<std::size_t> damage_step(const std::span<const DamageStep> steps, const float health_fraction) noexcept
{
    std::optional<std::size_t> step;
    for (std::size_t index = 0; index < steps.size(); ++index)
    {
        if (health_fraction <= steps[index].threshold)
        {
            step = index;
        }
    }
    return step;
}

std::uint32_t damage_alternate(const std::span<const DamageStep> steps, const float health_fraction) noexcept
{
    const auto step = damage_step(steps, health_fraction);
    return step ? steps[*step].alternate : 0U;
}

} // namespace eaw
