#include "presentation/scene_presentation.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

Matrix4 pose_transform(const Matrix4& base, const float base_heading, const Vector3& position, const float heading, const float pitch, const float bank)
{
    // Row-vector convention: the new linear part is base * Rz(-base_heading) * Rx(bank) * Ry(pitch) *
    // Rz(heading). Rz(-base_heading) turns the model's forward axis onto +X, where bank rolls about
    // it and pitch lifts it toward +Z, before the heading turns it about +Z.
    auto rotation = rotation_z(heading - base_heading);
    if (pitch != 0.0F || bank != 0.0F)
    {
        // Bank and pitch turn the opposite way to rotation_x and rotation_y; written out so the
        // sines are those of the given angles.
        const auto cb = std::cos(bank);
        const auto sb = std::sin(bank);
        const auto cp = std::cos(pitch);
        const auto sp = std::sin(pitch);
        const Matrix3 roll{{1.0F, 0.0F, 0.0F, 0.0F, cb, -sb, 0.0F, sb, cb}};
        const Matrix3 lift{{cp, 0.0F, sp, 0.0F, 1.0F, 0.0F, -sp, 0.0F, cp}};
        rotation = rotation_z(-base_heading) * roll * lift * rotation_z(heading);
    }
    auto result = base;
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            result(row, column) = base(row, 0) * rotation(0, column) + base(row, 1) * rotation(1, column) + base(row, 2) * rotation(2, column);
        }
    }
    set_translation(result, position);
    return result;
}

float flight_bank(const float yaw_rate, const float limit) noexcept
{
    return std::isfinite(yaw_rate) ? std::clamp(yaw_rate * 0.6F, -limit, limit) : 0.0F;
}

ScenePresentation::ScenePresentation(const VisualScene& scene, const std::span<const MapEntity> entities, const std::span<const MapEntityLook> looks,
                                     const std::span<const std::optional<EntityId>> ids, const std::span<const SpawnableLook> spawnable, const std::span<const Decoration> decorations)
{
    EAW_ASSERT_MSG(ids.size() == entities.size() && looks.size() == entities.size(), "scene presentation needs one id slot and one look per map entity");
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
        const auto& look = looks[index];
        // Hidden entities (markers, undrawable models) stay entities but are never drawn: without a
        // template they are skipped.
        if (look.hidden)
        {
            continue;
        }
        EAW_ASSERT_MSG(look.instance < scene.instances.size() && !scene.instances[look.instance].background,
                       "entity '" + entity.recipe.type_name + "' names scene instance " + std::to_string(look.instance) + ", which is missing or a background");
        if (ids[index])
        {
            templates_.emplace(entity_key(*ids[index]),
                               Template{scene.instances[look.instance], entity.spawn.heading, look.death, look.bank_limit, look.animation, look.damage, look.shield_mesh, look.hardpoints});
        }
    }
    add_spawnable(spawnable);
    for (std::size_t index = 0; index < decorations.size(); ++index)
    {
        const auto& decoration = decorations[index];
        if (decoration.look.hidden)
        {
            continue;
        }
        EAW_ASSERT_MSG(decoration.look.instance < scene.instances.size() && !scene.instances[decoration.look.instance].background, "a decoration names a scene instance it does not have");
        const EntityId id{decoration_id_base + static_cast<std::uint32_t>(index), 1};
        templates_.emplace(entity_key(id), Template{scene.instances[decoration.look.instance], decoration.heading, decoration.look.death, decoration.look.bank_limit, decoration.look.animation,
                                                    decoration.look.damage, decoration.look.shield_mesh, decoration.look.hardpoints});
        EntitySnapshot snapshot;
        snapshot.id = id;
        snapshot.position = decoration.position;
        snapshot.heading = decoration.heading;
        snapshot.owner = decoration.owner;
        decoration_snapshots_.push_back(snapshot);
    }
    // Scenery: every other instance of the world, so nothing the scene builds goes undrawn because
    // no entity claims it. Hidden looks claim theirs too (markers stay invisible).
    std::vector<bool> claimed(scene.instances.size(), false);
    for (const auto& look : looks)
    {
        if (look.instance < claimed.size())
        {
            claimed[look.instance] = true;
        }
    }
    for (const auto& decoration : decorations)
    {
        if (decoration.look.instance < claimed.size())
        {
            claimed[decoration.look.instance] = true;
        }
    }
    for (std::size_t index = 0; index < scene.instances.size(); ++index)
    {
        if (!claimed[index] && !scene.instances[index].background)
        {
            scenery_.push_back(scene.instances[index]);
        }
    }
}

void ScenePresentation::build(const std::span<const EntitySnapshot> snapshot, const std::unordered_set<std::uint64_t>& selected, std::vector<VisualInstance>& instances_out,
                              std::vector<EntityId>& entities_out, std::vector<AnimationProfile>* animations_out, const double seconds) const
{
    if (animations_out != nullptr)
    {
        animations_out->clear();
    }
    instances_out.clear();
    entities_out.clear();
    instances_out.reserve(backgrounds_.size() + scenery_.size() + snapshot.size());
    entities_out.reserve(backgrounds_.size() + scenery_.size() + snapshot.size());
    for (const auto& background : backgrounds_)
    {
        instances_out.push_back(background);
        entities_out.emplace_back();
        if (animations_out != nullptr)
        {
            animations_out->emplace_back();
        }
    }
    for (const auto& scenery : scenery_)
    {
        instances_out.push_back(scenery);
        entities_out.emplace_back();
        if (animations_out != nullptr)
        {
            animations_out->emplace_back();
        }
    }
    for (const auto& entity : snapshot)
    {
        const auto* found = template_of(entity.id, entity.visual);
        if (found == nullptr)
        {
            continue;
        }
        auto instance = found->instance;
        instance.transform = pose_transform(instance.transform, found->base_heading, entity.position, entity.heading, entity.pitch, flight_bank(entity.yaw_rate, found->bank_limit) + entity.roll);
        instance.highlighted = selected.contains(entity_key(entity.id));
        instance.key = entity_key(entity.id);
        // The land model's damage state by health (decorations, with no health, stay intact).
        if (!found->damage.empty() && entity.max_health > 0.0F)
        {
            instance.state = damage_alternate(found->damage, entity.health / entity.max_health);
        }
        instances_out.push_back(instance);
        entities_out.push_back(entity.id);
        if (animations_out != nullptr)
        {
            animations_out->push_back(found->animation);
        }
        // The shield flash: the model's shield mesh around it, grown by the step's scale (about the
        // model's origin), drawn in its rest pose without an entity.
        // Only a mesh presentation knows (a stale index would draw an unrelated model).
        const auto hit = found->shield_mesh && *found->shield_mesh < mesh_names_.size() ? shield_hits_.find(instance.key) : shield_hits_.end();
        if (hit == shield_hits_.end() || seconds < hit->second)
        {
            continue;
        }
        const auto step = static_cast<std::size_t>((seconds - hit->second) / shield_flash_step);
        if (step >= shield_flash_scales.size())
        {
            continue;
        }
        auto flash = instance;
        flash.mesh = *found->shield_mesh;
        flash.key = 0;
        flash.state = 0;
        flash.highlighted = false;
        flash.silhouette = false;
        for (std::size_t row = 0; row < 3; ++row)
        {
            for (std::size_t column = 0; column < 3; ++column)
            {
                flash.transform(row, column) *= shield_flash_scales[step];
            }
        }
        instances_out.push_back(flash);
        entities_out.emplace_back();
        if (animations_out != nullptr)
        {
            animations_out->emplace_back();
        }
    }
}

void ScenePresentation::flash_shields(const std::span<const ImpactSnapshot> impacts, const double seconds)
{
    const auto last = shield_flash_step * static_cast<double>(shield_flash_scales.size());
    std::erase_if(shield_hits_,
                  [&](const auto& entry)
                  {
                      return seconds - entry.second >= last;
                  });
    for (const auto& impact : impacts)
    {
        if (impact.kind == ImpactKind::shield && impact.target.valid())
        {
            shield_hits_[entity_key(impact.target)] = seconds;
        }
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

void ScenePresentation::add_spawnable(const std::span<const SpawnableLook> types)
{
    for (const auto& type : types)
    {
        if (type.hidden)
        {
            spawn_templates_.emplace_back();
            continue;
        }
        EAW_ASSERT_MSG(type.mesh < mesh_names_.size(), "a spawnable type names mesh " + std::to_string(type.mesh) + ", which presentation does not know");
        // The template's transform is the model's own frame at its Scale_Factor; pose_transform turns
        // and places it, keeping the scale.
        VisualInstance instance{type.mesh};
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            instance.transform(axis, axis) = type.scale;
        }
        instance.team_color = type.team_color;
        instance.lod_bias = type.lod_bias;
        instance.receives_shadows = type.receives_shadows;
        instance.silhouette = type.silhouette;
        spawn_templates_.push_back(Template{instance, identity_model_heading, type.death, type.bank_limit, type.animation, type.damage, type.shield_mesh, type.hardpoints});
    }
}

const ScenePresentation::Template* ScenePresentation::template_of(const EntityId id, const std::uint32_t visual) const
{
    if (const auto placed = templates_.find(entity_key(id)); placed != templates_.end())
    {
        return &placed->second;
    }
    if (visual >= spawnable_visual_base && visual - spawnable_visual_base < spawn_templates_.size() && spawn_templates_[visual - spawnable_visual_base])
    {
        return &*spawn_templates_[visual - spawnable_visual_base];
    }
    return nullptr;
}

std::optional<ScenePresentation::HardpointBones> ScenePresentation::hardpoint_bones(const EntitySnapshot& entity, const std::uint32_t hardpoint) const
{
    const auto* found = template_of(entity.id, entity.visual);
    if (found == nullptr || hardpoint >= found->hardpoints.size())
    {
        return std::nullopt;
    }
    return HardpointBones{found->instance.mesh, found->hardpoints[hardpoint],
                          pose_transform(found->instance.transform, found->base_heading, entity.position, entity.heading, entity.pitch, entity.roll)};
}

std::optional<ScenePresentation::Corpse> ScenePresentation::corpse_instance(const DeathSnapshot& death)
{
    const auto* found = template_of(death.id, death.visual);
    const auto placed = templates_.find(entity_key(death.id));
    if (found == nullptr)
    {
        return std::nullopt;
    }
    auto instance = found->instance;
    instance.transform = pose_transform(instance.transform, found->base_heading, death.position, death.heading, death.pitch);
    instance.highlighted = false;
    // A wreck shows its model's last damage state (rubble), the one for no health left.
    if (!found->damage.empty())
    {
        instance.state = damage_alternate(found->damage, 0.0F);
    }
    const auto style = found->death;
    if (placed != templates_.end())
    {
        templates_.erase(placed);
    }
    if (style.removed)
    {
        return std::nullopt;
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
