#include "test_support.hpp"

#include "eaw/visual_lod.hpp"

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

const ModuleRegistrar registrar("render", {
                                              {"test_gpu_resource_lifetime", &test_gpu_resource_lifetime},
                                              {"test_lod_selection", &test_lod_selection},
                                              {"test_lod_tracking", &test_lod_tracking},
                                          });

} // namespace
