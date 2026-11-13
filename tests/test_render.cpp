#include "test_support.hpp"

#include "render/frame_graph.hpp"
#include "render/lod_selection.hpp"
#include "render/shadow_cascades.hpp"
#include "render/sky_visibility.hpp"
#include "render/wind_bend.hpp"

#include <cmath>

namespace {

using namespace eaw_test;

void test_gpu_resource_lifetime()
{
    struct TestTag
    {};
    eaw::HandlePool<TestTag, std::string> pool;
    require(!eaw::ResourceHandle<TestTag>{}.valid() && pool.get({}) == nullptr, "a default handle must not resolve");
    const auto first = pool.insert("first");
    const auto second = pool.insert("second");
    require(first.valid() && pool.size() == 2 && *pool.get(first) == "first" && *pool.get(second) == "second", "handle pool insert or lookup failed");
    require(pool.erase(first) == std::optional<std::string>("first") && pool.get(first) == nullptr && !pool.erase(first), "an erased handle must stop resolving");
    const auto reused = pool.insert("third");
    require(reused.index == first.index && reused.generation != first.generation && pool.get(first) == nullptr && *pool.get(reused) == "third", "a reused slot must not resolve the stale handle");
    require(pool.get({second.index, second.generation + 1U}) == nullptr && pool.get({99, 1}) == nullptr, "foreign or future handles must not resolve");

    eaw::DescriptorSlotAllocator slots(3, 3);
    const auto a = slots.allocate();
    const auto b = slots.allocate();
    const auto c = slots.allocate();
    require(a == 3U && b == 4U && c == 5U && !slots.allocate(), "descriptor slots must come from the configured range, lowest first");
    slots.release(*a, 10);
    slots.release(*c, 12);
    require(!slots.allocate() && slots.pending_count() == 2, "a released slot must not be reused before its fence completes");
    require(slots.reclaim(9).empty() && slots.reclaim(10) == std::vector<std::uint32_t>{3U} && slots.allocate() == 3U && slots.pending_count() == 1, "a slot returns once its release fence completes");
    // Violating these preconditions is a programming error (release asserts); the predicates report it.
    require(slots.can_release(*b, 12) && !slots.can_release(*b, 11), "out-of-order release fences must be rejected");
    require(!slots.can_release(*c, 13), "releasing a slot that is already pending must be rejected");
    require(!slots.can_release(99, 13), "releasing a slot outside the range must be rejected");

    eaw::FenceRetirementQueue<std::shared_ptr<int>> retired;
    auto tracked = std::make_shared<int>(7);
    const std::weak_ptr<int> observer = tracked;
    retired.push(std::move(tracked), 5);
    retired.push(std::make_shared<int>(8), 6);
    require(retired.retire(4) == 0 && !observer.expired(), "a retired object must outlive its fence");
    require(retired.retire(5) == 1 && observer.expired() && retired.size() == 1, "a retired object must be released once its fence completes");
    require(!retired.can_push(2) && retired.can_push(6) && retired.retire(6) == 1 && retired.size() == 0, "retirement fences must be non-decreasing");
}

// Detail-level selection: the three strategies, the LOD bias and mesh-detail scaling, hysteresis,
// and the policy names.
void test_lod_selection()
{
    eaw::VisualLodSettings settings;
    settings.mesh_detail = 0.5F; // thresholds as given
    settings.policy = eaw::VisualLodPolicy::distance;
    settings.distance_step = 100.0F;
    const auto level = [&](const float distance, const float radius, const std::optional<std::uint32_t> previous = std::nullopt, const float bias = 1.0F)
    {
        return eaw::visual_lod_level(eaw::visual_lod_position(settings, bias, distance, radius, 1.0F, 1000.0F), 3, previous, settings.hysteresis);
    };
    require(level(50.0F, 1.0F) == 0 && level(150.0F, 1.0F) == 1 && level(250.0F, 1.0F) == 2 && level(9999.0F, 1.0F) == 2, "distance must pick one level per step, up to the last");
    require(level(150.0F, 1.0F, std::nullopt, 2.0F) == 0, "a LOD bias of 2 must keep detail twice as far");
    settings.mesh_detail = 1.0F;
    require(level(140.0F, 1.0F) == 0, "full mesh detail must keep detail 1.5 times as far");
    settings.mesh_detail = 0.5F;
    // Hysteresis: at 105 (level 1 by distance) an instance drawn at level 0 stays there; at 120 it
    // changes; coming back, it stays coarse until below 85.
    require(level(105.0F, 1.0F, 0U) == 0 && level(120.0F, 1.0F, 0U) == 1 && level(90.0F, 1.0F, 1U) == 1 && level(80.0F, 1.0F, 1U) == 0, "changes must wait for the hysteresis band");

    settings.policy = eaw::VisualLodPolicy::screen_size;
    settings.full_detail_pixels = 100.0F;
    // A sphere of radius r at distance d with a 1 rad field over 1000 px spans about 2r / (d * tan 0.5) * 500 px.
    const auto distance_for = [](const float pixels, const float radius)
    {
        return 2.0F * radius * 500.0F / (pixels * std::tan(0.5F));
    };
    require(level(distance_for(150.0F, 10.0F), 10.0F) == 0 && level(distance_for(75.0F, 10.0F), 10.0F) == 0 && level(distance_for(40.0F, 10.0F), 10.0F) == 1 &&
                level(distance_for(10.0F, 10.0F), 10.0F) == 2,
            "screen size must drop one level per halving below the full-detail size");
    require(level(distance_for(40.0F, 10.0F), 20.0F) == 0, "a larger object at the same distance must keep more detail");

    settings.policy = eaw::VisualLodPolicy::hybrid;
    settings.near_distance = 100.0F;
    settings.far_distance = 1000.0F;
    require(level(90.0F, 0.01F) == 0 && level(2000.0F, 1000.0F) == 2 && level(distance_for(40.0F, 10.0F), 10.0F) == 1,
            "hybrid must keep full detail near, the least far away, and screen size between");
    settings.policy = eaw::VisualLodPolicy::off;
    require(level(99999.0F, 0.01F) == 0 && eaw::visual_lod_level(5.0F, 1, std::nullopt, 0.1F) == 0, "off and single-level meshes must draw full detail");
    require(eaw::visual_lod_policy("screen") == eaw::VisualLodPolicy::screen_size && !eaw::visual_lod_policy("bogus") && eaw::to_string(eaw::VisualLodPolicy::hybrid) == "hybrid",
            "policies must be named");
}

// Tracking by key: a level holds while the position stays in its band; a change cross-fades for
// fade_seconds, both levels reported until the fade ends; without a fade time the change is
// instant; a key not seen in a pass is forgotten, and a key showing another mesh starts afresh.
void test_lod_tracking()
{
    eaw::VisualLodSettings settings;
    settings.fade_seconds = 0.5F;
    eaw::VisualLodTracker tracker;
    const auto step = [&](const float position, const float now, const std::uint32_t mesh = 1)
    {
        tracker.begin_pass();
        const auto choice = tracker.update(7, mesh, 0, position, 3, now, settings);
        tracker.end_pass();
        return choice;
    };
    auto choice = step(0.2F, 0.0F);
    require(choice.level == 0 && !choice.from && !choice.changed, "a new key must start at its level without a fade");
    choice = step(1.05F, 0.1F);
    require(choice.level == 0 && !choice.from, "a position within the band must keep the level");
    choice = step(1.5F, 1.0F);
    require(choice.changed && choice.level == 1 && choice.from == 0U && choice.fade > 0.0F && choice.fade < 0.1F, "a change must start a cross-fade from the old level");
    choice = step(1.5F, 1.25F);
    require(!choice.changed && choice.level == 1 && choice.from == 0U && std::abs(choice.fade - 0.5F) < 1e-4F, "half the fade time must show half of each level");
    choice = step(1.5F, 1.6F);
    require(choice.level == 1 && !choice.from && choice.fade == 0.0F, "a finished fade must show the new level alone");
    settings.fade_seconds = 0.0F;
    choice = step(2.5F, 2.0F);
    require(choice.changed && choice.level == 2 && !choice.from, "without a fade time a change must be instant");
    choice = step(2.5F, 2.1F, 2);
    require(!choice.changed && choice.level == 2 && !choice.from, "a key showing another mesh must start afresh");
    tracker.begin_pass();
    tracker.end_pass();
    require(tracker.size() == 0, "a key not seen in a pass must be forgotten");
}

// The frame graph: passes run in order, each after the transitions its uses need (from the state
// after the last use, or the initial one), batched per pass; resources return to their final
// states after the last pass; an unchanged access needs no transition.
void test_frame_graph()
{
    eaw::FrameGraph graph;
    const auto back = graph.import_resource("back buffer", eaw::GraphAccess::present, eaw::GraphAccess::present);
    const auto scene = graph.import_resource("scene", eaw::GraphAccess::render_target, eaw::GraphAccess::render_target);
    const auto bloom = graph.import_resource("bloom", eaw::GraphAccess::render_target, eaw::GraphAccess::render_target);
    std::vector<std::string> log;
    const auto record = [&](const char* name)
    {
        return [&log, name]
        {
            log.emplace_back(name);
        };
    };
    graph.add_pass("draw", {{scene, eaw::GraphAccess::render_target}}, record("draw"));
    graph.add_pass("extract", {{scene, eaw::GraphAccess::shader_read}, {bloom, eaw::GraphAccess::render_target}}, record("extract"));
    graph.add_pass("composite", {{scene, eaw::GraphAccess::shader_read}, {bloom, eaw::GraphAccess::shader_read}, {back, eaw::GraphAccess::render_target}}, record("composite"));
    graph.add_pass("capture", {{back, eaw::GraphAccess::copy_source}}, record("capture"));
    const auto transition = [&](const eaw::GraphTransition& value)
    {
        return graph.resource_name(value.resource) + ": " + eaw::to_string(value.before) + " -> " + eaw::to_string(value.after);
    };
    graph.execute(
        [&](const std::span<const eaw::GraphTransition> batch)
        {
            std::string line;
            for (const auto& value : batch)
            {
                line += (line.empty() ? "" : ", ") + transition(value);
            }
            log.push_back(line);
        });
    const std::vector<std::string> expected{
        "draw",      "scene: render target -> shader read",       "extract", "bloom: render target -> shader read, back buffer: present -> render target",
        "composite", "back buffer: render target -> copy source", "capture", "back buffer: copy source -> present, scene: shader read -> render target, bloom: shader read -> render target"};
    require(log == expected, "the frame graph must place each pass's transitions before it and return resources to their final states");
    graph.clear();
    require(graph.pass_count() == 0, "a cleared graph must have no passes");
}

// Shadow cascades: slices from the camera's near plane to the scene's far edge (rounded up), each
// slice inside its map, the scene's depth inside every map, and a pan smaller than a texel moving
// the map by whole texels only; degenerate input gives none.
void test_shadow_cascades()
{
    eaw::VisualCamera camera;
    camera.eye = {0.0F, -1000.0F, 1500.0F};
    camera.target = {0.0F, 0.0F, 0.0F};
    camera.near_plane = 10.0F;
    camera.far_plane = 50000.0F;
    camera.vertical_fov_degrees = 50.0F;
    const eaw::Vector3 scene_minimum{-4000.0F, -4000.0F, -50.0F};
    const eaw::Vector3 scene_maximum{4000.0F, 4000.0F, 300.0F};
    const eaw::Vector3 sun{0.4F, -0.3F, 0.8F};
    const eaw::ShadowCascadeSettings settings;
    const auto cascades = eaw::compute_shadow_cascades(camera, 16.0F / 9.0F, sun, scene_minimum, scene_maximum, settings);
    require(cascades.count == 3 && cascades.cascades[0].split_distance < cascades.cascades[1].split_distance && cascades.cascades[1].split_distance < cascades.cascades[2].split_distance &&
                cascades.cascades[2].split_distance >= 4063.0F && cascades.cascades[2].split_distance <= 4063.0F * 1.4143F,
            "cascades must split the view from near to the scene's far edge, rounded up");

    const auto view = eaw::normalize(camera.target - camera.eye);
    const auto right = eaw::normalize(eaw::cross(view, camera.up));
    const auto up = eaw::cross(right, view);
    const float half_height = std::tan(25.0F * 0.017453292F);
    const float half_width = half_height * 16.0F / 9.0F;
    float slice_near = camera.near_plane;
    bool inside = true;
    for (std::uint32_t index = 0; index < cascades.count; ++index)
    {
        const auto& cascade = cascades.cascades[index];
        for (const float along : {slice_near, cascade.split_distance})
        {
            for (const float sx : {-1.0F, 1.0F})
            {
                for (const float sy : {-1.0F, 1.0F})
                {
                    const auto corner = camera.eye + view * along + right * (sx * half_width * along) + up * (sy * half_height * along);
                    const auto clip = eaw::transform_point(corner, cascade.view_projection);
                    inside = inside && std::fabs(clip.x) <= 1.001F && std::fabs(clip.y) <= 1.001F && clip.z >= 0.0F && clip.z <= 1.0F;
                }
            }
        }
        for (const auto& corner : {scene_minimum, scene_maximum, eaw::Vector3{scene_minimum.x, scene_maximum.y, scene_maximum.z}, eaw::Vector3{scene_maximum.x, scene_minimum.y, scene_minimum.z}})
        {
            const auto depth = eaw::transform_point(corner, cascade.view_projection).z;
            inside = inside && depth >= 0.0F && depth <= 1.0F;
        }
        slice_near = cascade.split_distance;
    }
    require(inside, "every slice must lie inside its cascade, and the scene's depth inside every cascade");

    auto panned = camera;
    panned.eye.x += 0.37F;
    panned.target.x += 0.37F;
    const auto moved = eaw::compute_shadow_cascades(panned, 16.0F / 9.0F, sun, scene_minimum, scene_maximum, settings);
    const eaw::Vector3 point{120.0F, 80.0F, 0.0F};
    const auto texels = [&](const eaw::ShadowCascade& cascade)
    {
        return eaw::transform_point(point, cascade.view_projection).x * static_cast<float>(settings.resolution) * 0.5F;
    };
    const float shift = texels(moved.cascades[0]) - texels(cascades.cascades[0]);
    require(moved.cascades[0].texel_size == cascades.cascades[0].texel_size && std::fabs(shift - std::round(shift)) < 0.02F, "a pan must move a cascade by whole texels only");

    require(eaw::compute_shadow_cascades(camera, 1.0F, {}, scene_minimum, scene_maximum, settings).count == 0 &&
                eaw::compute_shadow_cascades(camera, 1.0F, sun, {-10.0F, -5000.0F, 0.0F}, {10.0F, -4000.0F, 0.0F}, settings).count == 0,
            "a missing light or a scene behind the camera must give no cascades");
}

// Wind bend: no wind holds still; a strong wind leans downwind by up to the strong offset, swelling
// and easing with the gust, never upwind.
void test_wind_bend()
{
    require(eaw::wind_bend({{1.0F, 0.0F, 0.0F}, 0.0F}, 3.0F).offset == eaw::Vector3{}, "no wind must not bend");
    float least = 1.0e9F;
    float most = 0.0F;
    for (int step = 0; step < 40; ++step)
    {
        const auto bend = eaw::wind_bend({{0.0F, 2.0F, 0.0F}, eaw::wind_strong_speed}, static_cast<float>(step) * 0.1F);
        require(bend.offset.x == 0.0F && bend.offset.z == 0.0F && bend.offset.y >= 0.0F, "the bend must lean downwind (never upwind) along the normalized direction");
        least = std::min(least, bend.offset.y);
        most = std::max(most, bend.offset.y);
    }
    require(most <= eaw::wind_bend_strong_offset + 1.0e-4F && most > eaw::wind_bend_strong_offset * 0.95F && least < eaw::wind_bend_strong_offset * (1.0F - eaw::wind_gust_share) * 1.05F &&
                std::fabs(eaw::wind_bend({}, 0.0F).inverse_height_squared - 1.0F / (eaw::wind_bend_height * eaw::wind_bend_height)) < 1.0e-9F,
            "a strong wind must sway between the gust's low and the full offset");
}

// Sky visibility: open ground sees the whole sky; the floor of a pit sees less, its rim more than
// its floor; a peak sees it all.
void test_sky_visibility()
{
    constexpr std::uint32_t size = 41;
    std::vector<float> flat(size * size, 0.0F);
    const auto open = eaw::compute_sky_visibility(flat, size, size, 20.0F);
    require(open.size() == flat.size() && open[20 * size + 20] == 1.0F, "flat ground must see the whole sky");
    auto pit = flat;
    for (std::uint32_t y = 0; y < size; ++y)
    {
        for (std::uint32_t x = 0; x < size; ++x)
        {
            const auto distance = std::hypot(static_cast<float>(x) - 20.0F, static_cast<float>(y) - 20.0F);
            pit[y * size + x] = distance < 10.0F ? -200.0F : 0.0F;
        }
    }
    const auto in_pit = eaw::compute_sky_visibility(pit, size, size, 20.0F);
    require(in_pit[20 * size + 20] < 0.8F && in_pit[20 * size + 20] > 0.0F && in_pit[20 * size + 35] == 1.0F, "a pit's floor must see less sky than the ground around it");
    auto peak = flat;
    peak[20 * size + 20] = 300.0F;
    require(eaw::compute_sky_visibility(peak, size, size, 20.0F)[20 * size + 20] == 1.0F && eaw::compute_sky_visibility(flat, 1, 1, 20.0F).empty(),
            "a peak must see the whole sky, and a degenerate grid nothing");
}

const ModuleRegistrar registrar("render", {
                                              {"test_frame_graph", &test_frame_graph},
                                              {"test_gpu_resource_lifetime", &test_gpu_resource_lifetime},
                                              {"test_lod_selection", &test_lod_selection},
                                              {"test_lod_tracking", &test_lod_tracking},
                                              {"test_shadow_cascades", &test_shadow_cascades},
                                              {"test_wind_bend", &test_wind_bend},
                                              {"test_sky_visibility", &test_sky_visibility},
                                          });

} // namespace
