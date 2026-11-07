#include "eaw/map_object_layout.hpp"

#include "eaw/virtual_file_system.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

namespace eaw {
namespace {

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
        else if (const auto model = catalog.model(*placement.type, layout.context, &layout.diagnostics))
        {
            placement.model_reference = model->value;
            auto [cached, inserted] = resolved_models.try_emplace(model->value);
            if (inserted)
            {
                cached->second = resolve_model_path(vfs, model->value).value_or(std::string{});
            }
            placement.model_path = cached->second;
            layout.missing_model_files += placement.model_path.empty() ? 1U : 0U;
        }
        else
        {
            ++layout.without_model;
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
