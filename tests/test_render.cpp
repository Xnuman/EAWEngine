#include "test_support.hpp"

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

const ModuleRegistrar registrar("render", {
                                              {"test_gpu_resource_lifetime", &test_gpu_resource_lifetime},
                                          });

} // namespace
