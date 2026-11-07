#include "scene_presentation.hpp"

#include "eaw/log.hpp"

#include <cmath>

namespace eaw {

std::array<float, 16> pose_transform(const std::array<float, 16>& base, const float base_heading, const SimVector3& position, const float heading)
{
    const auto delta = heading - base_heading;
    const auto cosine = std::cos(delta);
    const auto sine = std::sin(delta);
    auto result = base;
    // Row-vector convention: the new linear part is base * Rz(delta), Rz rows (c, s, 0), (-s, c, 0).
    for (std::size_t row = 0; row < 3; ++row)
    {
        const auto x = base[row * 4U];
        const auto y = base[row * 4U + 1U];
        result[row * 4U] = x * cosine - y * sine;
        result[row * 4U + 1U] = x * sine + y * cosine;
    }
    result[12] = position.x;
    result[13] = position.y;
    result[14] = position.z;
    return result;
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
        instance.transform = pose_transform(instance.transform, found->second.base_heading, entity.position, entity.heading);
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
    EAW_TRY_ASSIGN(const auto id, world.spawn(spawn));
    // The template's transform is the model's own frame; pose_transform turns and places it.
    VisualInstance instance{type.mesh};
    instance.team_color = type.team_color;
    templates_.emplace(entity_key(id), Template{instance, 0.0F, type.type_name});
    return id;
}

std::string ScenePresentation::type_name(const EntityId id) const
{
    const auto found = templates_.find(entity_key(id));
    return found != templates_.end() ? found->second.type_name : std::string{};
}

} // namespace eaw
