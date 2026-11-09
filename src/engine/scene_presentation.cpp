#include "scene_presentation.hpp"

#include "eaw/log.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

std::array<float, 16> pose_transform(const std::array<float, 16>& base, const float base_heading, const Vector3& position, const float heading, const float pitch, const float bank)
{
    // Row-vector convention: the new linear part is base * Rz(-base_heading) * Rx(bank) * Ry(pitch) *
    // Rz(heading). Rz(-base_heading) turns the model's forward axis onto +X, where bank rolls about
    // it and pitch lifts it toward +Z, before the heading turns it about +Z.
    using Matrix = std::array<std::array<float, 3>, 3>;
    const auto multiply = [](const Matrix& left, const Matrix& right)
    {
        Matrix product{};
        for (std::size_t row = 0; row < 3; ++row)
        {
            for (std::size_t column = 0; column < 3; ++column)
            {
                product[row][column] = left[row][0] * right[0][column] + left[row][1] * right[1][column] + left[row][2] * right[2][column];
            }
        }
        return product;
    };
    const auto about_z = [](const float angle) -> Matrix
    {
        const auto c = std::cos(angle);
        const auto s = std::sin(angle);
        return {{{c, s, 0.0F}, {-s, c, 0.0F}, {0.0F, 0.0F, 1.0F}}};
    };
    Matrix rotation = about_z(heading - base_heading);
    if (pitch != 0.0F || bank != 0.0F)
    {
        const auto cb = std::cos(bank);
        const auto sb = std::sin(bank);
        const auto cp = std::cos(pitch);
        const auto sp = std::sin(pitch);
        const Matrix roll{{{1.0F, 0.0F, 0.0F}, {0.0F, cb, -sb}, {0.0F, sb, cb}}};
        const Matrix lift{{{cp, 0.0F, sp}, {0.0F, 1.0F, 0.0F}, {-sp, 0.0F, cp}}};
        rotation = multiply(multiply(multiply(about_z(-base_heading), roll), lift), about_z(heading));
    }
    auto result = base;
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            result[row * 4U + column] = base[row * 4U] * rotation[0][column] + base[row * 4U + 1U] * rotation[1][column] + base[row * 4U + 2U] * rotation[2][column];
        }
    }
    result[12] = position.x;
    result[13] = position.y;
    result[14] = position.z;
    return result;
}

float flight_bank(const float yaw_rate) noexcept
{
    constexpr auto most = 3.14159265F / 3.0F;
    return std::isfinite(yaw_rate) ? std::clamp(yaw_rate * 0.6F, -most, most) : 0.0F;
}

ScenePresentation::ScenePresentation(const VisualScene& scene, const std::span<const MapEntitySpawn> entities, const std::span<const std::optional<EntityId>> ids,
                                     const std::span<const SpawnableType> spawnable)
{
    EAW_ASSERT_MSG(ids.size() == entities.size(), "scene presentation needs one id slot per map entity");
    for (const auto& mesh : scene.meshes)
    {
        mesh_names_.push_back(mesh.name);
        mesh_minimum_.push_back(mesh.bounds_minimum);
        mesh_maximum_.push_back(mesh.bounds_maximum);
    }
    for (const auto& instance : scene.instances)
    {
        if (instance.background)
        {
            backgrounds_.push_back(instance);
        }
    }
    for (std::size_t index = 0; index < entities.size(); ++index)
    {
        const auto& entity = entities[index];
        EAW_ASSERT_MSG(entity.instance < scene.instances.size() && !scene.instances[entity.instance].background,
                       "entity '" + entity.spawn.type_name + "' names scene instance " + std::to_string(entity.instance) + ", which is missing or a background");
        if (ids[index])
        {
            templates_.emplace(entity_key(*ids[index]), Template{scene.instances[entity.instance], entity.spawn.heading, entity.death});
        }
    }
    for (const auto& type : spawnable)
    {
        EAW_ASSERT_MSG(type.mesh < mesh_names_.size(), "spawnable type '" + type.type_name + "' names mesh " + std::to_string(type.mesh) + ", which the scene does not have");
        // The template's transform is the model's own frame at its Scale_Factor; pose_transform turns
        // and places it, keeping the scale.
        VisualInstance instance{type.mesh};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            instance.transform[axis * 5] = type.scale;
        }
        instance.team_color = type.team_color;
        spawn_templates_.push_back({instance, identity_model_heading, type.death});
    }
}

void ScenePresentation::build(const std::span<const EntitySnapshot> snapshot, const std::unordered_set<std::uint64_t>& selected, std::vector<VisualInstance>& instances_out,
                              std::vector<EntityId>& entities_out) const
{
    instances_out.clear();
    entities_out.clear();
    instances_out.reserve(backgrounds_.size() + snapshot.size());
    entities_out.reserve(backgrounds_.size() + snapshot.size());
    for (const auto& background : backgrounds_)
    {
        instances_out.push_back(background);
        entities_out.emplace_back();
    }
    for (const auto& entity : snapshot)
    {
        const Template* found = nullptr;
        if (const auto placed = templates_.find(entity_key(entity.id)); placed != templates_.end())
        {
            found = &placed->second;
        }
        else if (entity.visual >= spawnable_visual_base && entity.visual - spawnable_visual_base < spawn_templates_.size())
        {
            found = &spawn_templates_[entity.visual - spawnable_visual_base];
        }
        if (found == nullptr)
        {
            continue;
        }
        auto instance = found->instance;
        instance.transform = pose_transform(instance.transform, found->base_heading, entity.position, entity.heading, entity.pitch, flight_bank(entity.yaw_rate) + entity.roll);
        instance.highlighted = selected.contains(entity_key(entity.id));
        instances_out.push_back(instance);
        entities_out.push_back(entity.id);
    }
}

void ScenePresentation::add_meshes(const std::span<const VisualMesh> meshes)
{
    for (const auto& mesh : meshes)
    {
        mesh_names_.push_back(mesh.name);
        mesh_minimum_.push_back(mesh.bounds_minimum);
        mesh_maximum_.push_back(mesh.bounds_maximum);
    }
}

void ScenePresentation::add_spawnable(const std::span<const SpawnableType> types)
{
    for (const auto& type : types)
    {
        EAW_ASSERT_MSG(type.mesh < mesh_names_.size(), "spawnable type '" + type.type_name + "' names mesh " + std::to_string(type.mesh) + ", which presentation does not know");
        VisualInstance instance{type.mesh};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            instance.transform[axis * 5] = type.scale;
        }
        instance.team_color = type.team_color;
        spawn_templates_.push_back({instance, identity_model_heading, type.death});
    }
}

std::optional<ScenePresentation::Corpse> ScenePresentation::corpse_instance(const DeathSnapshot& death)
{
    const Template* found = nullptr;
    const auto placed = templates_.find(entity_key(death.id));
    if (placed != templates_.end())
    {
        found = &placed->second;
    }
    else if (death.visual >= spawnable_visual_base && death.visual - spawnable_visual_base < spawn_templates_.size())
    {
        found = &spawn_templates_[death.visual - spawnable_visual_base];
    }
    if (found == nullptr)
    {
        return std::nullopt;
    }
    auto instance = found->instance;
    instance.transform = pose_transform(instance.transform, found->base_heading, death.position, death.heading, death.pitch);
    instance.highlighted = false;
    const auto style = found->death;
    if (placed != templates_.end())
    {
        templates_.erase(placed);
    }
    return Corpse{instance, style};
}

const std::string& ScenePresentation::mesh_name(const std::uint32_t mesh) const
{
    EAW_ASSERT_MSG(mesh < mesh_names_.size(), "scene presentation mesh " + std::to_string(mesh) + " is out of range");
    return mesh_names_[mesh];
}

const Vector3& ScenePresentation::mesh_minimum(const std::uint32_t mesh) const
{
    EAW_ASSERT_MSG(mesh < mesh_minimum_.size(), "scene presentation mesh " + std::to_string(mesh) + " is out of range");
    return mesh_minimum_[mesh];
}

const Vector3& ScenePresentation::mesh_maximum(const std::uint32_t mesh) const
{
    EAW_ASSERT_MSG(mesh < mesh_maximum_.size(), "scene presentation mesh " + std::to_string(mesh) + " is out of range");
    return mesh_maximum_[mesh];
}

} // namespace eaw
