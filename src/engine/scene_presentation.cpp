#include "scene_presentation.hpp"

#include "eaw/log.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

std::array<float, 16> pose_transform(const std::array<float, 16>& base, const float base_heading, const SimVector3& position, const float heading, const float pitch, const float bank)
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

ScenePresentation::ScenePresentation(const VisualScene& scene, const std::span<const MapEntitySpawn> entities, SimulationWorld& world)
{
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
    for (const auto& entity : entities)
    {
        EAW_ASSERT_MSG(entity.instance < scene.instances.size() && !scene.instances[entity.instance].background,
                       "entity '" + entity.spawn.type_name + "' names scene instance " + std::to_string(entity.instance) + ", which is missing or a background");
        const auto id = world.spawn(entity.spawn);
        if (!id)
        {
            // Map data: one bad placement does not stop the rest of the map.
            log_warning("simulation", with_context("skipping map entity", id.error()));
            continue;
        }
        templates_.emplace(entity_key(*id), Template{scene.instances[entity.instance], entity.spawn.heading, entity.spawn.type_name});
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
        const auto found = templates_.find(entity_key(entity.id));
        if (found == templates_.end())
        {
            continue;
        }
        auto instance = found->second.instance;
        instance.transform = pose_transform(instance.transform, found->second.base_heading, entity.position, entity.heading, entity.pitch, flight_bank(entity.yaw_rate));
        instance.highlighted = selected.contains(entity_key(entity.id));
        instances_out.push_back(instance);
        entities_out.push_back(entity.id);
    }
}

const std::string& ScenePresentation::mesh_name(const std::uint32_t mesh) const
{
    EAW_ASSERT_MSG(mesh < mesh_names_.size(), "scene presentation mesh " + std::to_string(mesh) + " is out of range");
    return mesh_names_[mesh];
}

const std::array<float, 3>& ScenePresentation::mesh_minimum(const std::uint32_t mesh) const
{
    EAW_ASSERT_MSG(mesh < mesh_minimum_.size(), "scene presentation mesh " + std::to_string(mesh) + " is out of range");
    return mesh_minimum_[mesh];
}

const std::array<float, 3>& ScenePresentation::mesh_maximum(const std::uint32_t mesh) const
{
    EAW_ASSERT_MSG(mesh < mesh_maximum_.size(), "scene presentation mesh " + std::to_string(mesh) + " is out of range");
    return mesh_maximum_[mesh];
}

Result<EntityId> ScenePresentation::spawn(SimulationWorld& world, const SpawnableType& type, const SimVector3& position, const float heading, const std::uint32_t owner, const bool follows_ground)
{
    EAW_ASSERT_MSG(type.mesh < mesh_names_.size(), "spawnable type '" + type.type_name + "' names mesh " + std::to_string(type.mesh) + ", which the scene does not have");
    EntitySpawn spawn;
    spawn.type_name = type.type_name;
    spawn.owner = owner;
    spawn.position = position;
    spawn.heading = heading;
    spawn.max_speed = type.max_speed;
    spawn.visual = type.mesh;
    spawn.follows_ground = follows_ground;
    spawn.passable_types = follows_ground ? type.passable_types : 0U;
    spawn.combat = type.combat;
    spawn.radius = type.radius;
    spawn.turn_rate = type.turn_rate;
    spawn.min_speed = type.min_speed;
    spawn.formation_order = type.formation_order;
    spawn.formation_spacing = type.formation_spacing;
    EAW_TRY_ASSIGN(const auto id, world.spawn(spawn));
    // The template's transform is the model's own frame; pose_transform turns and places it.
    VisualInstance instance{type.mesh};
    instance.team_color = type.team_color;
    templates_.emplace(entity_key(id), Template{instance, identity_model_heading, type.type_name});
    return id;
}

std::string ScenePresentation::type_name(const EntityId id) const
{
    const auto found = templates_.find(entity_key(id));
    return found != templates_.end() ? found->second.type_name : std::string{};
}

} // namespace eaw
