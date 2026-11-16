#include "render/frame_graph.hpp"

#include "core/fatal_error.hpp"

#include <utility>

namespace eaw {

const char* to_string(const GraphAccess access) noexcept
{
    switch (access)
    {
    case GraphAccess::common:
        return "common";
    case GraphAccess::render_target:
        return "render target";
    case GraphAccess::depth_write:
        return "depth write";
    case GraphAccess::depth_read:
        return "depth read";
    case GraphAccess::shader_read:
        return "shader read";
    case GraphAccess::all_shader_read:
        return "all shader read";
    case GraphAccess::compute_read:
        return "compute read";
    case GraphAccess::unordered_access:
        return "unordered access";
    case GraphAccess::copy_source:
        return "copy source";
    case GraphAccess::copy_dest:
        return "copy destination";
    case GraphAccess::present:
        return "present";
    }
    return "unknown";
}

std::uint32_t FrameGraph::import_resource(std::string name, const GraphAccess initial, const GraphAccess final)
{
    resources_.push_back({std::move(name), initial, final});
    return static_cast<std::uint32_t>(resources_.size() - 1U);
}

void FrameGraph::add_pass(std::string name, std::vector<Use> uses, std::function<void()> record)
{
    for (std::size_t index = 0; index < uses.size(); ++index)
    {
        EAW_ASSERT_MSG(uses[index].resource < resources_.size(), "frame graph pass '" + name + "' uses a resource the graph does not have");
        for (std::size_t other = index + 1U; other < uses.size(); ++other)
        {
            EAW_ASSERT_MSG(uses[other].resource != uses[index].resource, "frame graph pass '" + name + "' lists '" + resources_[uses[index].resource].name + "' twice");
        }
    }
    passes_.push_back({std::move(name), std::move(uses), std::move(record)});
}

void FrameGraph::execute(const std::function<void(std::span<const GraphTransition>)>& barriers)
{
    current_.clear();
    for (const auto& resource : resources_)
    {
        current_.push_back(resource.initial);
    }
    const auto flush = [&]
    {
        if (!batch_.empty())
        {
            barriers(batch_);
            batch_.clear();
        }
    };
    const auto move_to = [&](const std::uint32_t resource, const GraphAccess access)
    {
        if (current_[resource] != access)
        {
            batch_.push_back({resource, current_[resource], access});
            current_[resource] = access;
        }
    };
    for (const auto& pass : passes_)
    {
        for (const auto& use : pass.uses)
        {
            move_to(use.resource, use.access);
        }
        flush();
        if (pass.record)
        {
            pass.record();
        }
    }
    for (std::uint32_t resource = 0; resource < resources_.size(); ++resource)
    {
        move_to(resource, resources_[resource].final);
    }
    flush();
}

void FrameGraph::clear() noexcept
{
    resources_.clear();
    passes_.clear();
}

const std::string& FrameGraph::resource_name(const std::uint32_t resource) const
{
    EAW_ASSERT_MSG(resource < resources_.size(), "frame graph resource out of range");
    return resources_[resource].name;
}

} // namespace eaw
