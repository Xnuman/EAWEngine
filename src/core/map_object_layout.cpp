#include "eaw/map_object_layout.hpp"

#include "eaw/virtual_file_system.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <numbers>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace eaw {
namespace {

// Bounds expansion of malformed mod data; stock squadrons list at most a handful of members.
constexpr std::size_t maximum_squadron_members = 64;

AloQuaternion axis_rotation(const float degrees, const int axis) noexcept
{
    const auto half = degrees * std::numbers::pi_v<float> / 360.0F;
    const auto s = std::sin(half);
    AloQuaternion result{0.0F, 0.0F, 0.0F, std::cos(half)};
    (axis == 0 ? result.x : axis == 1 ? result.y : result.z) = s;
    return result;
}

// Hamilton product: the result applies `right` first, then `left`.
AloQuaternion multiply(const AloQuaternion& left, const AloQuaternion& right) noexcept
{
    return {left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y, left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
            left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w, left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z};
}

std::string_view trim(const std::string_view value) noexcept
{
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
    {
        ++first;
    }
    auto last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
    {
        --last;
    }
    return value.substr(first, last - first);
}

std::vector<std::string_view> split_list(const std::string_view text)
{
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const auto end = text.find(',', start);
        const auto item = trim(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (!item.empty())
        {
            result.push_back(item);
        }
        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }
    return result;
}

// Parses "x,y,z" (commas and/or whitespace). Returns nullopt for malformed or non-finite values.
std::optional<AloVector3> parse_vector3(const std::string_view text)
{
    std::array<float, 3> values{};
    std::size_t count = 0;
    std::size_t position = 0;
    while (position < text.size() && count < values.size())
    {
        while (position < text.size() && (text[position] == ',' || std::isspace(static_cast<unsigned char>(text[position]))))
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
    if (count != values.size() || !trim(text.substr(position)).empty())
    {
        return std::nullopt;
    }
    return AloVector3{values[0], values[1], values[2]};
}

std::string location(const XmlDefinition& definition)
{
    return definition.source_path + ":" + std::to_string(definition.line) + " " + definition.type + " '" + definition.name + "'";
}

// Land teams and companies are arranged by runtime formation behavior that the data does not
// describe (no member offsets), so they are counted rather than guessed.
bool is_unexpanded_container(const XmlDefinition& type, const GameObjectCatalog& catalog)
{
    return type.type == "Container" || type.type == "GroundCompany" || type.type == "HeroCompany" || catalog.field(type, "Company_Units").field != nullptr;
}

// Space squadrons list their members in Squadron_Units and give each member's offset, in the
// squadron's local frame, in repeated Squadron_Offsets fields. When Squadron_Units repeats, the
// last occurrence is used (the ordinary override rule; unconfirmed against the original game).
template <typename AssignModel> bool expand_squadron(const MapObjectPlacement& squadron, const GameObjectCatalog& catalog, MapObjectLayout& layout, const AssignModel& assign_model)
{
    const auto units = catalog.field(*squadron.type, "Squadron_Units", &layout.diagnostics);
    if (units.field == nullptr)
    {
        return false;
    }
    const auto members = split_list(units.field->text);
    if (members.empty())
    {
        return false;
    }
    const auto offsets = catalog.fields(*squadron.type, "Squadron_Offsets", &layout.diagnostics);
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
        AloVector3 offset{};
        if (member < offsets.fields.size())
        {
            if (const auto parsed = parse_vector3(offsets.fields[member]->text))
            {
                offset = *parsed;
            }
            else
            {
                layout.diagnostics.push_back(location(*squadron.type) + ": malformed Squadron_Offsets '" + std::string(trim(offsets.fields[member]->text)) + "'");
            }
        }
        else
        {
            layout.diagnostics.push_back(location(*squadron.type) + ": no Squadron_Offsets entry for member " + std::to_string(member) + "; placed at the squadron origin");
        }
        auto local = alo_identity_matrix();
        local.values[12] = offset.x;
        local.values[13] = offset.y;
        local.values[14] = offset.z;
        placement.transform = alo_multiply(local, squadron.transform);
        if (!assign_model(placement))
        {
            ++layout.without_model;
        }
        ++layout.expanded_members;
        layout.placements.push_back(std::move(placement));
    }
    return true;
}

float finite_or(const float value, const float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

} // namespace

AloMatrix4 placed_object_transform(const TedPlacedObject& object) noexcept
{
    const auto rotation =
        multiply(axis_rotation(finite_or(object.rotation.z, 0.0F), 2), multiply(axis_rotation(finite_or(object.rotation.y, 0.0F), 1), axis_rotation(finite_or(object.rotation.x, 0.0F), 0)));
    // A zero or non-finite scale would collapse or poison the transform; stock maps use 1.0.
    auto scale = finite_or(object.scale, 1.0F);
    if (scale == 0.0F)
    {
        scale = 1.0F;
    }
    return alo_compose_transform({scale, scale, scale}, rotation, {object.position.x, object.position.y, object.position.z});
}

MapObjectLayout build_map_object_layout(const TedMap& map, const GameObjectCatalog& catalog, const VirtualFileSystem& vfs)
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
