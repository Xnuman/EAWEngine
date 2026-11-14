// What XML object types say about how they are drawn: LOD bias, banking, animation, markers, and
// deaths (the gameplay fields are in object_types.cpp).

#include "math/scalar.hpp"
#include "core/string_utils.hpp"
#include "assets/legacy_units.hpp"
#include "assets/type_fields.hpp"
#include "battle/object_types.hpp"
#include "scene/scene_builder_internal.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <span>
#include <string>
#include <string_view>

namespace eaw::scene_build {

float type_lod_bias(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto bias = type_float_field(catalog, definition, "LOD_Bias");
    return bias > 0.0F ? bias : 1.0F;
}

float type_bank_limit(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    if (catalog.field(definition, "Bank_Turn_Angle").field == nullptr)
    {
        return default_bank_limit;
    }
    const auto degrees = std::clamp(type_float_field(catalog, definition, "Bank_Turn_Angle"), 0.0F, 90.0F);
    return degrees * pi / 180.0F;
}

AnimationProfile type_animation_profile(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    const auto rate = [&](const std::string_view field)
    {
        const auto value = type_float_field(catalog, definition, field);
        return value > 0.0F && value <= 100.0F ? value : 1.0F;
    };
    AnimationProfile profile;
    // Reference speeds as fractions of Max_Speed; without a top speed they cannot be placed.
    if (const auto top = type_float_field(catalog, definition, "Max_Speed"); top > 0.0F && std::isfinite(top))
    {
        const auto reference = [&](const std::string_view field)
        {
            const auto value = type_float_field(catalog, definition, field);
            return value > 0.0F && std::isfinite(value) ? value / top : 0.0F;
        };
        const auto move = reference("Movement_Animation_Speed");
        profile.move_reference = move > 0.0F ? move : 1.0F;
        profile.walk_reference = reference("Walk_Animation_Speed");
    }
    profile.idle_rate = rate("Idle_Anim_00_Rate_Mod");
    profile.idle_when_moving = TypeFields(catalog, definition).flag("Allow_Idle_When_Moving", false);
    profile.loop_first_idle = TypeFields(catalog, definition).flag("Loop_Idle_Anim_00", false);
    const auto flag = [&](const std::string_view field)
    {
        return TypeFields(catalog, definition).flag(field, false);
    };
    profile.pre_turn = flag("Has_Pre_Turn_Anim");
    if (const auto rotation = type_float_field(catalog, definition, "Rotation_Animation_Speed"); rotation > 0.0F && std::isfinite(rotation))
    {
        profile.turn_reference = legacy_units::degrees_per_frame(rotation);
    }
    profile.animation_led = flag("Locomotor_Has_Animation_Priority");
    for (const auto* field : {"Stopped_Rate_Of_Turn", "Max_Rate_Of_Turn"})
    {
        if (const auto value = type_float_field(catalog, definition, field); value > 0.0F && std::isfinite(value) && profile.in_place_turn_rate <= 0.0F)
        {
            profile.in_place_turn_rate = legacy_units::degrees_per_frame(value);
        }
    }
    profile.walk_transition = flag("Walk_Transition");
    const auto bone = [&](const std::string_view field)
    {
        const auto* node = catalog.field(definition, field).field;
        const auto name = node != nullptr ? ascii_trim(node->text) : std::string_view{};
        return name.empty() ? 0U : game_object_name_crc(name);
    };
    profile.turret_bone = bone("Turret_Bone_Name");
    profile.barrel_bone = bone("Barrel_Bone_Name");
    if (profile.barrel_bone == profile.turret_bone)
    {
        // Spore plants name one bone as both: it only turns.
        profile.barrel_bone = 0U;
    }
    const auto speed = type_float_field(catalog, definition, "Turret_Rotate_Speed");
    profile.turret_rate = legacy_units::degrees_per_frame(speed > 0.0F ? speed : default_turret_rotate_speed);
    const auto half_arc = [&](const std::string_view field, const float fallback)
    {
        const auto degrees = type_float_field(catalog, definition, field);
        return degrees > 0.0F && degrees < 360.0F ? degrees * 0.5F * pi / 180.0F : fallback;
    };
    profile.turret_pitch_limit = half_arc("Turret_Elevate_Extent_Degrees", profile.turret_pitch_limit);
    return profile;
}

bool type_is_marker(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    return TypeFields(catalog, definition).flag("Is_Marker", false);
}

std::string type_animation_source(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    for (const auto* field : {"Land_Model_Anim_Override_Name", "Space_Model_Anim_Override_Name"})
    {
        if (const auto* value = catalog.field(definition, field).field)
        {
            const auto name = ascii_trim(value->text);
            if (!name.empty())
            {
                return std::string(name);
            }
        }
    }
    return {};
}

void note_animation_source(SceneBuilder& builder, const std::string& model_path, const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    if (auto source = type_animation_source(catalog, definition); !source.empty())
    {
        builder.animation_sources.try_emplace(ascii_lower(model_path), std::move(source));
    }
}

bool type_receives_shadows(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    return !TypeFields(catalog, definition).flag("In_Background", false);
}

bool type_silhouette(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    return TypeFields(catalog, definition).flag("Occlusion_Silhouette_Enabled", false);
}

void add_track_layers(SceneBuilder& builder, const GameObjectCatalog& catalog, const SurfaceEffectCatalog& surfaces, const DynamicTrackCatalog& tracks, const XmlDefinition& type,
                      TrackLayerTable& table)
{
    const auto key = ascii_lower(type.name);
    if (table.contains(key))
    {
        return;
    }
    std::vector<TrackLayer> layers;
    for (const auto name : TypeFields(catalog, type).list("SurfaceFX_Name"))
    {
        const auto* settings = surfaces.find(name);
        if (settings == nullptr || settings->track.empty())
        {
            continue;
        }
        const auto* track = tracks.find(settings->track);
        if (track == nullptr)
        {
            builder.scene.warnings.push_back(type.name + ": dynamic track " + settings->track + " is not defined; it lays no trail");
            continue;
        }
        const auto texture = particle_library_texture(builder, track->texture, "dynamic tracks");
        if (settings->track_left_bone.empty() && settings->track_right_bone.empty())
        {
            layers.push_back({*track, {}, texture});
            continue;
        }
        for (const auto* bone : {&settings->track_left_bone, &settings->track_right_bone})
        {
            if (!bone->empty())
            {
                layers.push_back({*track, *bone, texture});
            }
        }
    }
    table.emplace(key, std::move(layers));
}

DeathStyle type_death_style(const GameObjectCatalog& catalog, const XmlDefinition& definition)
{
    DeathStyle style;
    style.removed = TypeFields(catalog, definition).flag("Remove_Upon_Death", false);
    std::string_view chosen;
    for (const auto* entry : catalog.fields(definition, "Death_Clone").fields)
    {
        // The crush clone's clip, for deaths by crushing.
        if (const auto [crush_damage, crush_clone] = death_clone_entry(entry->text); ascii_equal_ignoring_case(crush_damage, "Damage_Crush") && !crush_clone.empty())
        {
            if (const auto found = catalog.find(crush_clone); !found.empty())
            {
                const auto* kind = catalog.field(*found.front(), "Specific_Death_Anim_Type").field;
                DeathClip clip;
                clip.role = kind != nullptr && ascii_equal_ignoring_case(ascii_trim(kind->text), "Crushed") ? AnimationRole::crushed : AnimationRole::die;
                const auto index = type_float_field(catalog, *found.front(), "Specific_Death_Anim_Index");
                clip.variant = index >= 0.0F && index < 100.0F ? static_cast<std::uint32_t>(index) : 0U;
                style.crushed = clip;
            }
        }
        const auto [damage, clone] = death_clone_entry(entry->text);
        if (clone.empty())
        {
            continue;
        }
        if (chosen.empty() || ascii_lower(std::string(damage)) == "damage_normal")
        {
            chosen = clone;
        }
    }
    // The wreck is the clone's; a type without one is its own wreck (AT-AA, the Dark Troopers),
    // and the same tags on it say how long it stays and what plays when it goes.
    const auto clones = chosen.empty() ? std::span<const XmlDefinition* const>{} : catalog.find(chosen);
    const auto& wreck = clones.empty() ? definition : *clones.front();
    if (!clones.empty() && catalog.field(wreck, "Specific_Death_Anim_Index").field != nullptr)
    {
        const auto index = type_float_field(catalog, wreck, "Specific_Death_Anim_Index");
        if (index >= 0.0F && index < 100.0F)
        {
            style.die_variant = static_cast<std::uint32_t>(index);
        }
    }
    if (const auto seconds = type_float_field(catalog, wreck, "Death_Persistence_Duration"); seconds > 0.0F && seconds < 3600.0F)
    {
        style.persistence_seconds = seconds;
    }
    if (const auto* end = catalog.field(wreck, "Death_Explosions_End").field)
    {
        if (const auto names = split_list(end->text); !names.empty())
        {
            style.end_effect = game_object_name_crc(names.front());
        }
    }
    return style;
}

} // namespace eaw::scene_build
