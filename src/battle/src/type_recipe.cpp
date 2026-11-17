#include "battle/type_recipe.hpp"

#include "math/quaternion.hpp"
#include "math/scalar.hpp"
#include "math/transform.hpp"
#include "battle/battle_map_build.hpp"
#include "battle/object_types.hpp"

namespace eaw {

TypeRecipe build_type_recipe(const SpawnableBuild& build, const XmlDefinition& definition, const std::string& model_path, const Model& host_model, const std::span<const ModelAttachment> attachments)
{
    TypeRecipe recipe;
    recipe.scale = type_scale_factor(build.catalog, definition, build.warnings);
    recipe.type_name = definition.name;
    recipe.max_speed = type_max_speed(build.catalog, definition, build.warnings, build.speed_multiplier);
    recipe.follows_ground = build.land && type_follows_ground(build.catalog, definition);
    recipe.passable_types = recipe.follows_ground ? type_passable_types(build.catalog, definition, build.movement_classes) : 0U;
    recipe.combat = build.profile_of(definition, &host_model, attachments);
    recipe.hover_offset = type_float_field(build.catalog, definition, "Hover_Offset");
    recipe.layer_z_adjust = type_float_field(build.catalog, definition, "Layer_Z_Adjust");
    recipe.crush = type_crush(build.catalog, definition);
    recipe.survivors = type_survivors(build.catalog, definition);
    recipe.script = type_script_info(build.catalog, definition);
    recipe.bounds = drawn_model_bounds(host_model, attachments, true);
    recipe.soft_radius = type_soft_footprint_radius(build.catalog, definition);
    if (recipe.max_speed > 0.0F)
    {
        recipe.radius = recipe.soft_radius > 0.0F ? recipe.soft_radius : model_footprint_radius(recipe.bounds, scaled_model_transform(Matrix4::identity(), recipe.scale));
        recipe.capsule = recipe.soft_radius > 0.0F ? FootprintCapsule{.radius = recipe.soft_radius} : model_footprint_capsule(recipe.bounds, scaled_model_transform(Matrix4::identity(), recipe.scale));
        // Turn rates and acceleration take the mode's speed multiplier in space (type_movement).
        const auto motion_multiplier = build.land ? 1.0F : build.speed_multiplier;
        recipe.turn_rate = type_turn_rate(build.catalog, definition, motion_multiplier);
        recipe.min_speed = build.land ? 0.0F : type_min_flight_speed(build.catalog, definition, recipe.max_speed);
        recipe.movement = type_movement(build.catalog, definition, build.land, recipe.min_speed > 0.0F, motion_multiplier);
        apply_animation_locomotion(recipe.movement, build.catalog, definition, build.resources, model_path, recipe.max_speed);
        if (!build.land)
        {
            recipe.movement.turn_in_place_rate = build.combat != nullptr ? recipe.turn_rate / type_turn_in_place_slowdown(build.catalog, definition, *build.combat) : 0.0F;
        }
        recipe.aims_with_body = type_aims_with_body(build.catalog, definition);
        recipe.formation_order = type_formation_order(build.catalog, definition);
        recipe.formation_spacing = type_formation_spacing(build.catalog, definition);
        recipe.formation_solo = type_formation_solo(build.catalog, definition);
        recipe.formation_raggedness = type_formation_raggedness(build.catalog, definition);
    }
    else if (build.land)
    {
        // Structures and props block the ground on land; only mobile units take space as a radius.
        if ((recipe.obstacle_passability = type_obstacle_passability(build.catalog, definition)))
        {
            recipe.obstacle_shape = type_obstacle_shape(build.catalog, definition);
        }
    }
    else
    {
        // Space avoidance steers ships around immobile entities with a radius. Starbases give only
        // their soft radius (100 to 550).
        recipe.space_obstacle_radius = type_space_obstacle_radius(build.catalog, definition);
        if (recipe.space_obstacle_radius <= 0.0F)
        {
            recipe.space_obstacle_radius = recipe.soft_radius;
        }
    }
    return recipe;
}

TypeRecipe build_team_recipe(const SpawnableBuild& build, const XmlDefinition& container, const TeamArrangement arrangement)
{
    TypeRecipe recipe;
    recipe.type_name = container.name;
    recipe.max_speed = type_max_speed(build.catalog, container, build.warnings, build.speed_multiplier);
    recipe.follows_ground = build.land && type_follows_ground(build.catalog, container);
    recipe.passable_types = recipe.follows_ground ? type_passable_types(build.catalog, container, build.movement_classes) : 0U;
    recipe.script = type_script_info(build.catalog, container);
    if (recipe.max_speed > 0.0F)
    {
        const auto motion_multiplier = build.land ? 1.0F : build.speed_multiplier;
        recipe.turn_rate = type_turn_rate(build.catalog, container, motion_multiplier);
        recipe.movement = type_movement(build.catalog, container, build.land, false, motion_multiplier);
        recipe.formation_order = type_formation_order(build.catalog, container);
        recipe.formation_spacing = type_formation_spacing(build.catalog, container);
        recipe.formation_solo = type_formation_solo(build.catalog, container);
    }
    recipe.team = arrangement;
    return recipe;
}

PreparedSpawn make_spawn(const TypeRecipe& recipe, const SpawnInstance& instance)
{
    PreparedSpawn made;
    auto& entity = made.recipe;
    auto& spawn = made.spawn;
    entity.type_name = recipe.type_name;
    spawn.owner = instance.owner;
    spawn.position = instance.position;
    spawn.heading = instance.heading;
    spawn.visual = instance.visual;
    entity.follows_ground = recipe.follows_ground && instance.ground_valid;
    entity.passable_types = entity.follows_ground ? recipe.passable_types : 0U;
    entity.max_speed = recipe.max_speed;
    entity.combat = recipe.combat;
    entity.crush = recipe.crush;
    entity.team = recipe.team;
    if (recipe.max_speed > 0.0F)
    {
        // A placement's matrix sizes the model (a map may scale a placement); a type without a soft
        // radius takes its radius from the bounds through it.
        entity.radius = instance.placement && recipe.soft_radius <= 0.0F ? model_footprint_radius(recipe.bounds, *instance.placement) : recipe.radius;
        entity.capsule = instance.placement && recipe.soft_radius <= 0.0F ? model_footprint_capsule(recipe.bounds, *instance.placement) : recipe.capsule;
        entity.turn_rate = recipe.turn_rate;
        entity.min_speed = recipe.min_speed;
        entity.movement = recipe.movement;
        entity.aims_with_body = entity.follows_ground && recipe.aims_with_body;
        entity.formation_order = recipe.formation_order;
        entity.formation_spacing = recipe.formation_spacing;
        entity.formation_solo = recipe.formation_solo;
    }
    else if (recipe.obstacle_passability)
    {
        const auto transform = instance.placement ? *instance.placement : placement_transform(instance.position, instance.heading, recipe.scale);
        spawn.footprint =
            recipe.obstacle_shape ? place_obstacle(*recipe.obstacle_shape, transform, *recipe.obstacle_passability) : model_ground_footprint(recipe.bounds, transform, *recipe.obstacle_passability);
    }
    else
    {
        entity.radius = recipe.space_obstacle_radius;
    }
    return made;
}

Matrix4 placement_transform(const Vector3& position, const float heading, const float scale) noexcept
{
    // Models face -Y (model_forward_heading): an unturned model faces -pi/2, so it turns by the rest.
    return compose_transform({scale, scale, scale}, axis_angle({0.0F, 0.0F, 1.0F}, heading + half_pi), position);
}

} // namespace eaw
