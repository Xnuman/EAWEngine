#include "sandbox_session.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace eaw {

std::optional<eaw::SimVector3> SandboxSession::ground_target(const float x, const float y) const
{
    const auto stats = renderer.stats();
    const auto camera = tactical_camera->camera();
    const auto width = static_cast<float>(stats.width);
    const auto height = static_cast<float>(stats.height);
    auto point = eaw::intersect_terrain(camera, width, height, x, y, terrain_heights);
    if (!point)
    {
        point = eaw::intersect_ground_plane(camera, width, height, x, y, terrain_heights.liquid_level.value_or(0.0F));
    }
    return point ? std::optional(eaw::SimVector3{(*point)[0], (*point)[1], (*point)[2]}) : std::nullopt;
}

void SandboxSession::order_move(const eaw::SimVector3& target)
{
    // Selected mobile units, in id order, move as one formation centered on the target.
    std::vector<eaw::EntityId> movers;
    for (const auto& entity : world->snapshot().entities)
    {
        if (selection.contains(eaw::entity_key(entity.id)) && entity.max_speed > 0.0F)
        {
            movers.push_back(entity.id);
        }
    }
    if (movers.empty())
    {
        return;
    }
    std::ranges::sort(movers, {}, eaw::entity_key);
    const auto count = movers.size();
    if (const auto submitted = battle->submit({.order = eaw::BattleOrder::move, .entities = std::move(movers), .point = target}); !submitted)
    {
        eaw::log_warning("simulation", submitted.error());
        return;
    }
    ++orders_issued;
    std::cout << "Move " << count << " units to " << eaw::format_fixed(target.x, 0) << ", " << eaw::format_fixed(target.y, 0) << ", " << eaw::format_fixed(target.z, 0) << '\n';
}

void SandboxSession::order_attack(const eaw::EntityId victim)
{
    const auto& entities = world->snapshot().entities;
    const auto found = std::ranges::find(entities, victim, &eaw::EntitySnapshot::id);
    if (found == entities.end() || found->max_health <= 0.0F)
    {
        return;
    }
    // Selected units hostile to the victim, in id order; unarmed ones are ignored by the
    // simulation and keep their move.
    std::vector<eaw::EntityId> attackers;
    for (const auto& entity : entities)
    {
        if (selection.contains(eaw::entity_key(entity.id)) && world->hostility().hostile(entity.owner, found->owner))
        {
            attackers.push_back(entity.id);
        }
    }
    if (attackers.empty())
    {
        return;
    }
    std::ranges::sort(attackers, {}, eaw::entity_key);
    if (const auto submitted = battle->submit({.order = eaw::BattleOrder::attack, .entities = std::move(attackers), .target = victim}); !submitted)
    {
        eaw::log_warning("simulation", submitted.error());
        return;
    }
    ++orders_issued;
    std::cout << "Attack " << world->type_name(victim) << " (owner " << found->owner << ")\n";
}

void SandboxSession::update_camera(const eaw::InputState& input, const bool scene_mouse, const bool rts_mode)
{
    const auto key_down = [&](const eaw::Key key)
    {
        return input.keys[static_cast<std::size_t>(key)].down;
    };
    const auto camera_now = std::chrono::steady_clock::now();
    const auto camera_seconds = std::min(std::chrono::duration<float>(camera_now - camera_tick).count(), 0.25F);
    camera_tick = camera_now;
    eaw::CameraInput camera_input;
    camera_input.reset = input.keys[static_cast<std::size_t>(eaw::Key::space)].pressed;
    camera_input.zoom_steps = scene_mouse ? input.wheel_delta : 0.0F;
    if (scene_mouse && !rts_mode && input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::left)].down)
    {
        // Rotation follows the device (raw, unaccelerated counts); dragging that should track
        // the cursor, such as panning, uses cursor pixels.
        camera_input.yaw = static_cast<float>(input.raw_mouse_delta_x) * 0.01F;
        camera_input.pitch = static_cast<float>(-input.raw_mouse_delta_y) * 0.01F;
    }
    if (tactical_camera)
    {
        // Pan speed scales with the view distance; panning units are view distances.
        const auto speed = (key_down(eaw::Key::shift) ? 2.4F : 0.8F) * camera_seconds;
        camera_input.pan_right = static_cast<float>((key_down(eaw::Key::d) || key_down(eaw::Key::right) ? 1 : 0) - (key_down(eaw::Key::a) || key_down(eaw::Key::left) ? 1 : 0)) * speed;
        camera_input.pan_forward = static_cast<float>((key_down(eaw::Key::w) || key_down(eaw::Key::up) ? 1 : 0) - (key_down(eaw::Key::s) || key_down(eaw::Key::down) ? 1 : 0)) * speed;
        if (scene_mouse && !rts_mode && input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::right)].down)
        {
            camera_input.pan_right -= static_cast<float>(input.mouse_delta_x) * 0.0015F;
            camera_input.pan_forward += static_cast<float>(input.mouse_delta_y) * 0.0015F;
        }
        tactical_camera->apply(camera_input);
        renderer.set_camera(tactical_camera->camera());
    }
    else
    {
        orbit_camera->apply(camera_input);
        renderer.set_camera(orbit_camera->camera());
    }
}

void SandboxSession::handle_pointer(const eaw::InputState& input, const bool scene_mouse, const bool rts_mode, const std::array<float, 2>& mouse)
{
    // A left click that does not drag selects what is under the cursor; dragging orbits or
    // rotates as before.
    const auto& left_button = input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::left)];
    const auto& right_button = input.mouse_buttons[static_cast<std::size_t>(eaw::MouseButton::right)];
    if (left_button.pressed)
    {
        // A press over the overlay never becomes a scene click.
        click_travel = scene_mouse ? 0 : std::numeric_limits<std::int32_t>::max();
        drag_start = scene_mouse ? std::optional(mouse) : std::nullopt;
        box_active = false;
    }
    if (left_button.down)
    {
        click_travel += std::abs(input.mouse_delta_x) + std::abs(input.mouse_delta_y);
        if (rts_mode && drag_start && click_travel > 4)
        {
            box_active = true;
            renderer.set_selection_rectangle(std::array<float, 4>{(*drag_start)[0], (*drag_start)[1], mouse[0], mouse[1]});
        }
    }
    if (left_button.released)
    {
        if (box_active && drag_start && world)
        {
            // Drag-selection picks mobile units only, like the original's box selection.
            const auto stats = renderer.stats();
            const auto picked = eaw::entities_in_rectangle(world->snapshot().entities, tactical_camera->camera(), static_cast<float>(stats.width), static_cast<float>(stats.height),
                                                           {(*drag_start)[0], (*drag_start)[1], mouse[0], mouse[1]}, true);
            selection.clear();
            for (const auto id : picked)
            {
                selection.insert(eaw::entity_key(id));
            }
            std::cout << "Box-selected " << selection.size() << " units\n";
        }
        else if (click_travel <= 3 && input.mouse_x >= 0 && input.mouse_y >= 0)
        {
            unbound_picks.push_back(renderer.request_pick(static_cast<std::uint32_t>(input.mouse_x), static_cast<std::uint32_t>(input.mouse_y)));
        }
        renderer.set_selection_rectangle(std::nullopt);
        drag_start.reset();
        box_active = false;
    }
    // Orders and spawns target the ground under a client pixel, found on the CPU in this frame
    // (terrain or liquid surface on land, the z = 0 plane in space), so they never wait for a
    // GPU readback. A right-click also requests a pick: if it lands on a hostile entity, an
    // attack order follows the move a frame or two later.
    // RTS mode: the right button has no drag action, so an order is issued on the press with no
    // movement threshold (a threshold dropped quick clicks made while the mouse was moving).
    if (right_button.pressed && rts_mode && scene_mouse && !selection.empty() && world)
    {
        if (const auto target = ground_target(mouse[0], mouse[1]))
        {
            order_move(*target);
        }
        if (input.mouse_x >= 0 && input.mouse_y >= 0)
        {
            const auto request = renderer.request_pick(static_cast<std::uint32_t>(input.mouse_x), static_cast<std::uint32_t>(input.mouse_y));
            unbound_picks.push_back(request);
            attack_picks.insert(request);
        }
    }
}

void SandboxSession::handle_command_keys(const eaw::InputState& input, const bool scene_mouse, const std::array<float, 2>& mouse)
{
    if (input.keys[static_cast<std::size_t>(eaw::Key::o)].pressed && !spawn_names.empty())
    {
        do
        {
            spawn_owner = (spawn_owner + 1U) % 8U;
        } while (spawn_owner == eaw::neutral_map_owner);
        std::cout << "Spawn owner: " << spawn_owner << '\n';
    }
    if (input.keys[static_cast<std::size_t>(eaw::Key::tab)].pressed && !spawn_names.empty())
    {
        // Tab next, Shift+Tab previous, with Ctrl ten at a time.
        const auto size = spawn_names.size();
        const auto step = (input.keys[static_cast<std::size_t>(eaw::Key::control)].down ? 10U : 1U) % size;
        spawn_choice = input.keys[static_cast<std::size_t>(eaw::Key::shift)].down ? (spawn_choice + size - step) % size : (spawn_choice + step) % size;
        const bool loaded = std::ranges::any_of(spawnable,
                                                [&](const eaw::SpawnableType& type)
                                                {
                                                    return fold_name(type.type_name) == fold_name(spawn_names[spawn_choice]);
                                                });
        std::cout << "Spawn type: " << spawn_names[spawn_choice] << (loaded ? "" : " (loads on first spawn)") << " [" << spawn_choice + 1U << "/" << size << "]\n";
    }
    if (input.keys[static_cast<std::size_t>(eaw::Key::n)].pressed && spawn_choice < spawn_names.size() && world && presentation && tactical_camera && scene_mouse)
    {
        if (const auto target = ground_target(mouse[0], mouse[1]))
        {
            spawn_named(spawn_names[spawn_choice], *target, spawn_count);
        }
    }
    if (input.keys[static_cast<std::size_t>(eaw::Key::c)].pressed && tactical_camera)
    {
        free_camera = !free_camera;
        std::cout << (free_camera ? "Free debug camera (drag rotates, right-drag pans)\n" : "RTS camera (drag selects, right-click moves)\n");
    }
}

void SandboxSession::process_picks()
{
    while (const auto pick = renderer.take_pick())
    {
        // Pick ids are positions in the instance list of the frame that drew the pick.
        const auto recorded = pick_entities.find(pick->request);
        const auto& entities_then = recorded != pick_entities.end() ? recorded->second : frame_entities;
        const auto entity = pick->instance && *pick->instance < entities_then.size() ? entities_then[*pick->instance] : eaw::EntityId{};
        const bool attack = attack_picks.erase(pick->request) != 0;
        std::erase(unbound_picks, pick->request);
        if (recorded != pick_entities.end())
        {
            pick_entities.erase(recorded);
        }
        if (attack)
        {
            if (entity.valid() && world && world->alive(entity))
            {
                order_attack(entity);
            }
            continue;
        }
        selection.clear();
        if (entity.valid())
        {
            selection.insert(eaw::entity_key(entity));
            std::cout << "Selected " << (world != nullptr ? world->type_name(entity) : std::string_view{}) << '\n';
        }
        else
        {
            std::cout << "Selected: " << (pick->terrain ? "terrain" : "nothing") << '\n';
        }
    }
}

void SandboxSession::handle_view_keys(const eaw::InputState& input)
{
    if (input.keys[static_cast<std::size_t>(eaw::Key::p)].pressed)
    {
        if (tactical_camera && world)
        {
            toggle_world_pause();
        }
        else if (visual_animation)
        {
            animation_playing = !animation_playing;
        }
    }
    if (input.keys[static_cast<std::size_t>(eaw::Key::b)].pressed)
    {
        show_all_bounds = !show_all_bounds;
        std::cout << "Bounds overlay " << (show_all_bounds ? "on" : "off") << '\n';
    }
    if (input.keys[static_cast<std::size_t>(eaw::Key::g)].pressed)
    {
        show_passability = !show_passability;
        std::cout << "Passability overlay " << (show_passability ? "on" : "off") << '\n';
    }
    const int animation_step = (input.keys[static_cast<std::size_t>(eaw::Key::right)].pressed ? 1 : 0) - (input.keys[static_cast<std::size_t>(eaw::Key::left)].pressed ? 1 : 0);
    if (visual_model && !tactical_camera && animation_step != 0 && !animation_paths.empty())
    {
        // Choice 0 is the bind pose; reading here is safe because builds only read too, and
        // remounts happen on this thread between builds.
        const auto choices = animation_paths.size() + 1U;
        animation_choice = (animation_choice + (animation_step > 0 ? 1U : choices - 1U)) % choices;
        animation_time = 0.0F;
        visual_animation.reset();
        if (animation_choice == 0)
        {
            std::cout << "Animation: bind pose\n";
            animation_status = "bind pose";
            renderer.set_bone_transforms(bind_palette);
        }
        else
        {
            const auto& path = animation_paths[animation_choice - 1U];
            auto animation = read_animation(vfs, path);
            if (animation)
            {
                visual_animation = std::move(animation).value();
                animation_status = path;
                std::cout << "Animation " << animation_choice << "/" << animation_paths.size() << ": " << path << " | " << visual_animation->playable_frame_count() << " frames, "
                          << visual_animation->duration_seconds() << " seconds\n";
            }
            else
            {
                std::cerr << "animation warning: " << path << ": " << animation.error().message << "; showing the bind pose\n";
                renderer.set_bone_transforms(bind_palette);
            }
        }
    }
}

} // namespace eaw
