#include "assets/map_object_layout.hpp"

#include "vfs/virtual_file_system.hpp"
#include "math/scalar.hpp"
#include "math/transform.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace eaw {
namespace {

// Bounds expansion of malformed mod data; stock squadrons list at most a handful of members.
constexpr std::size_t maximum_squadron_members = 64;
// Distance between squadron members that have no Squadron_Offsets entry, lined up astern (an engine
// choice; stock offsets are 20-60 units apart).
constexpr float squadron_fallback_spacing = 40.0F;
constexpr std::size_t maximum_company_members = 64;

Quaternion axis_rotation(const float degrees, const int axis) noexcept
{
    const auto half = degrees * pi / 360.0F;
    const auto s = std::sin(half);
    Quaternion result{0.0F, 0.0F, 0.0F, std::cos(half)};
    (axis == 0 ? result.x : axis == 1 ? result.y : result.z) = s;
    return result;
}

// Parses "x,y,z" (commas and/or whitespace). Returns nullopt for malformed or non-finite values.
std::optional<Vector3> parse_vector3(const std::string_view text)
{
    std::array<float, 3> values{};
    std::size_t count = 0;
    std::size_t position = 0;
    while (position < text.size() && count < values.size())
    {
        while (position < text.size() && (text[position] == ',' || ascii_is_space(text[position])))
        {
            ++position;
        }
        if (position >= text.size())
        {
            break;
        }
        float value{};
        const auto [end, error] = std::from_chars(text.data() + position, text.data() + text.size(), value);
        if (error != std::errc{} || !std::isfinite(value))
        {
            return std::nullopt;
        }
        values[count++] = value;
        position = static_cast<std::size_t>(end - text.data());
    }
    if (count != values.size() || !ascii_trim(text.substr(position)).empty())
    {
        return std::nullopt;
    }
    return Vector3{values[0], values[1], values[2]};
}

std::string location(const XmlDefinition& definition)
{
    return definition.source_path + ":" + std::to_string(definition.line) + " " + definition.type + " '" + definition.name + "'";
}

// Containers without a member list are counted, not guessed.
bool is_unexpanded_container(const XmlDefinition& type, const GameObjectCatalog& catalog)
{
    return type.type == "Container" || type.type == "GroundCompany" || type.type == "HeroCompany" || catalog.field(type, "Company_Units").field != nullptr;
}

// The definition a member name refers to: one with a model for the context when there is one.
const XmlDefinition* member_type(const GameObjectCatalog& catalog, const std::string_view name, const GameModelContext context)
{
    const auto candidates = catalog.find(name);
    for (const auto* candidate : candidates)
    {
        if (catalog.model(*candidate, context))
        {
            return candidate;
        }
    }
    return candidates.empty() ? nullptr : candidates.front();
}

// Land companies list their members in Company_Units (GroundCompany, HeroCompany, SlaveCompany in
// the stock data; the field never repeats there). The data gives no offsets, so every member takes
// the company's transform and is flagged for the consumer to arrange.
template <typename AssignModel> bool expand_company(const MapObjectPlacement& company, const GameObjectCatalog& catalog, MapObjectLayout& layout, const AssignModel& assign_model)
{
    const auto units = catalog.field(*company.type, "Company_Units", &layout.diagnostics);
    if (units.field == nullptr)
    {
        return false;
    }
    const auto members = split_type_names(units.field->text);
    if (members.empty())
    {
        return false;
    }
    if (members.size() > maximum_company_members)
    {
        layout.diagnostics.push_back(location(*company.type) + ": Company_Units lists " + std::to_string(members.size()) + " members; only the first " + std::to_string(maximum_company_members) +
                                     " are placed");
    }
    const auto count = std::min(members.size(), maximum_company_members);
    for (std::size_t member = 0; member < count; ++member)
    {
        MapObjectPlacement placement;
        placement.object_index = company.object_index;
        placement.container = company.type;
        placement.member = static_cast<std::uint32_t>(member);
        placement.needs_formation = true;
        placement.transform = company.transform;
        placement.type = member_type(catalog, members[member], layout.context);
        if (placement.type == nullptr)
        {
            layout.diagnostics.push_back(location(*company.type) + ": company member '" + std::string(members[member]) + "' is not defined");
            ++layout.untyped;
            continue;
        }
        if (!assign_model(placement))
        {
            ++layout.without_model;
        }
        ++layout.company_members;
        layout.placements.push_back(std::move(placement));
    }
    return true;
}

// Space squadrons list their members in Squadron_Units and give each member's offset, in the
// squadron's local frame, in repeated Squadron_Offsets fields. A repeated Squadron_Units continues
// the list: stock Rebel_X-Wing_Squadron writes "X-Wing, X-Wing" twice and "X-Wing" once beside five
// Squadron_Offsets, one per fighter.
template <typename AssignModel> bool expand_squadron(const MapObjectPlacement& squadron, const GameObjectCatalog& catalog, MapObjectLayout& layout, const AssignModel& assign_model)
{
    const auto units = catalog.fields(*squadron.type, "Squadron_Units", &layout.diagnostics);
    std::vector<std::string_view> members;
    for (const auto* field : units.fields)
    {
        const auto names = split_type_names(field->text);
        members.insert(members.end(), names.begin(), names.end());
    }
    if (members.empty())
    {
        return false;
    }
    auto offsets = catalog.fields(*squadron.type, "Squadron_Offsets", &layout.diagnostics);
    if (offsets.fields.empty())
    {
        // Darth_Vader_TIE_Fighter_Squadron keeps its offsets on its team container.
        if (const auto* team = catalog.field(*squadron.type, "Create_Team_Type").field)
        {
            for (const auto* candidate : catalog.find(ascii_trim(team->text)))
            {
                if (auto from_team = catalog.fields(*candidate, "Squadron_Offsets", &layout.diagnostics); !from_team.fields.empty())
                {
                    offsets = std::move(from_team);
                    break;
                }
            }
        }
    }
    if (members.size() > maximum_squadron_members)
    {
        layout.diagnostics.push_back(location(*squadron.type) + ": Squadron_Units lists " + std::to_string(members.size()) + " members; only the first " + std::to_string(maximum_squadron_members) +
                                     " are placed");
    }
    const auto count = std::min(members.size(), maximum_squadron_members);
    for (std::size_t member = 0; member < count; ++member)
    {
        MapObjectPlacement placement;
        placement.object_index = squadron.object_index;
        placement.container = squadron.type;
        placement.member = static_cast<std::uint32_t>(member);
        const auto candidates = catalog.find(members[member]);
        for (const auto* candidate : candidates)
        {
            if (catalog.model(*candidate, layout.context))
            {
                placement.type = candidate;
                break;
            }
        }
        if (placement.type == nullptr && !candidates.empty())
        {
            placement.type = candidates.front();
        }
        if (placement.type == nullptr)
        {
            layout.diagnostics.push_back(location(*squadron.type) + ": squadron member '" + std::string(members[member]) + "' is not defined");
            ++layout.untyped;
            continue;
        }
        Vector3 offset{};
        if (member < offsets.fields.size())
        {
            if (const auto parsed = parse_vector3(offsets.fields[member]->text))
            {
                offset = *parsed;
            }
            else
            {
                layout.diagnostics.push_back(location(*squadron.type) + ": malformed Squadron_Offsets '" + std::string(ascii_trim(offsets.fields[member]->text)) + "'");
            }
        }
        else
        {
            // Engine choice: members without an offset line up astern instead of stacking.
            offset = {-squadron_fallback_spacing * static_cast<float>(member + 1U - offsets.fields.size()), 0.0F, 0.0F};
            layout.diagnostics.push_back(location(*squadron.type) + ": no Squadron_Offsets entry for member " + std::to_string(member) + "; placed astern");
        }
        // Squadron_Offsets are in the squadron's facing frame (+X ahead: stock V formations point
        // there), while its placement first turns the model a quarter (placed_model_facing): the
        // offset takes the inverse quarter turn, (x, y) to (y, -x), into the model's frame.
        static_assert(placed_model_facing == 90.0F, "the offset turn below assumes a quarter turn");
        auto local = Matrix4::identity();
        set_translation(local, {offset.y, -offset.x, offset.z});
        placement.transform = local * squadron.transform;
        if (!assign_model(placement))
        {
            ++layout.without_model;
        }
        ++layout.expanded_members;
        layout.placements.push_back(std::move(placement));
    }
    return true;
}

} // namespace

std::vector<std::string_view> split_type_names(const std::string_view text)
{
    std::vector<std::string_view> result;
    std::size_t start = 0;
    const auto separator = [](const char character)
    {
        return character == ',' || ascii_is_space(character);
    };
    while (start < text.size())
    {
        while (start < text.size() && separator(text[start]))
        {
            ++start;
        }
        auto end = start;
        while (end < text.size() && !separator(text[end]))
        {
            ++end;
        }
        if (end > start)
        {
            result.push_back(text.substr(start, end - start));
        }
        start = end;
    }
    return result;
}

Matrix4 placed_object_transform(const TedPlacedObject& object) noexcept
{
    const auto rotation = axis_rotation(finite_or(object.rotation.z, 0.0F), 2) *
                          (axis_rotation(finite_or(object.rotation.y, 0.0F), 1) * (axis_rotation(finite_or(object.rotation.x, 0.0F), 0) * axis_rotation(placed_model_facing, 2)));
    // A zero or non-finite scale would collapse or poison the transform; stock maps use 1.0.
    auto scale = finite_or(object.scale, 1.0F);
    if (scale == 0.0F)
    {
        scale = 1.0F;
    }
    return compose_transform({scale, scale, scale}, rotation, {object.position.x, object.position.y, object.position.z});
}

MapObjectLayout build_map_object_layout(const TedMapData& map, const GameObjectCatalog& catalog, const AssetReader& vfs)
{
    MapObjectLayout layout;
    layout.context = map.type == TedMapType::land ? GameModelContext::land : GameModelContext::space;
    std::unordered_map<std::string, std::string> resolved_models;
    const auto assign_model = [&](MapObjectPlacement& placement)
    {
        const auto model = catalog.model(*placement.type, layout.context, &layout.diagnostics);
        if (!model)
        {
            return false;
        }
        placement.model_reference = model->value;
        auto [cached, inserted] = resolved_models.try_emplace(model->value);
        if (inserted)
        {
            cached->second = resolve_model_path(vfs, model->value).value_or(std::string{});
        }
        placement.model_path = cached->second;
        layout.missing_model_files += placement.model_path.empty() ? 1U : 0U;
        return true;
    };

    for (std::size_t index = 0; index < map.placed_objects.size(); ++index)
    {
        const auto& object = map.placed_objects[index];
        if (!object.has_transform)
        {
            continue;
        }
        MapObjectPlacement placement;
        placement.object_index = index;
        placement.transform = placed_object_transform(object);
        placement.type = catalog.resolve(object.crc, layout.context).definition;
        if (placement.type == nullptr)
        {
            ++layout.untyped;
        }
        // A land company is its members even when it names a model of its own (stock companies
        // such as Rebel_Infantry_Squad carry a galactic-style Land_Model_Name).
        else if (layout.context == GameModelContext::land && expand_company(placement, catalog, layout, assign_model))
        {
            continue;
        }
        else if (!assign_model(placement))
        {
            if (expand_squadron(placement, catalog, layout, assign_model))
            {
                continue;
            }
            if (is_unexpanded_container(*placement.type, catalog))
            {
                ++layout.unexpanded_containers;
            }
            else
            {
                ++layout.without_model;
            }
        }
        layout.placements.push_back(std::move(placement));
    }
    // The same variant problem is reported once per type, not once per placed instance.
    std::ranges::sort(layout.diagnostics);
    const auto [first, last] = std::ranges::unique(layout.diagnostics);
    layout.diagnostics.erase(first, last);
    return layout;
}

} // namespace eaw
