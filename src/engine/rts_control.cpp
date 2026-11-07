#include "rts_control.hpp"

#include "camera_math.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

std::vector<EntityId> entities_in_rectangle(const std::span<const EntitySnapshot> entities, const VisualCamera& camera, const float width, const float height, const std::array<float, 4>& rectangle,
                                            const bool mobile_only)
{
    const auto left = std::min(rectangle[0], rectangle[2]);
    const auto right = std::max(rectangle[0], rectangle[2]);
    const auto top = std::min(rectangle[1], rectangle[3]);
    const auto bottom = std::max(rectangle[1], rectangle[3]);
    std::vector<EntityId> result;
    for (const auto& entity : entities)
    {
        if (!entity.selectable || (mobile_only && !(entity.max_speed > 0.0F)))
        {
            continue;
        }
        const auto screen = project_to_screen(camera, width, height, {entity.position.x, entity.position.y, entity.position.z});
        if (screen && (*screen)[0] >= left && (*screen)[0] <= right && (*screen)[1] >= top && (*screen)[1] <= bottom)
        {
            result.push_back(entity.id);
        }
    }
    return result;
}

std::vector<SimVector3> grid_targets(const SimVector3& target, const std::size_t count, const float spacing)
{
    std::vector<SimVector3> result;
    if (count == 0)
    {
        return result;
    }
    const auto columns = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    const auto rows = (count + columns - 1U) / columns;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto column = static_cast<float>(index % columns) - static_cast<float>(columns - 1U) * 0.5F;
        const auto row = static_cast<float>(index / columns) - static_cast<float>(rows - 1U) * 0.5F;
        result.push_back({target.x + column * spacing, target.y + row * spacing, target.z});
    }
    return result;
}

} // namespace eaw
