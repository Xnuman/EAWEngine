#pragma once

// A frame as a list of passes that declare the resources they use and how. The graph runs the
// passes in the order added and, before each, the transitions its uses need, from each resource's
// state after its last use (or its initial state); after the last pass it returns every resource
// to its final state. Access kinds are neutral: a backend maps them to its own resource states and
// records the transitions. No graphics API, so the plans are testable without a device.
//
// Engine choices: passes run in declared order (no reordering or culling), and resources are the
// backend's own, persistent ones (no transient allocation or memory aliasing yet).

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace eaw {

enum class GraphAccess : std::uint8_t
{
    // A buffer between frames (buffers return to it after every submission).
    common,
    render_target,
    depth_write,
    // The depth buffer bound read-only for testing and sampled at once (water reads the scene's
    // depth while testing against it).
    depth_read,
    shader_read,
    // Read by a compute shader.
    compute_read,
    // Written by a compute shader.
    unordered_access,
    copy_source,
    copy_dest,
    present,
};

[[nodiscard]] const char* to_string(GraphAccess access) noexcept;

struct GraphTransition
{
    std::uint32_t resource{};
    GraphAccess before{};
    GraphAccess after{};
};

class FrameGraph final
{
public:
    // A resource the frame uses: in `initial` when the frame starts, returned to `final` at its end.
    // Ids are given in order from 0, so a backend can keep its resources in a parallel list.
    std::uint32_t import_resource(std::string name, GraphAccess initial, GraphAccess final);

    struct Use
    {
        std::uint32_t resource{};
        GraphAccess access{};
    };
    // A pass that uses each listed resource in one access (a resource listed twice in one pass is a
    // programming error); `record` records its commands.
    void add_pass(std::string name, std::vector<Use> uses, std::function<void()> record);

    // Runs the passes, calling `barriers` with each non-empty batch of transitions: before a pass,
    // those its uses need; after the last pass, those back to the final states.
    void execute(const std::function<void(std::span<const GraphTransition>)>& barriers);

    // Forgets the passes and resources (keeping their storage), for the next frame.
    void clear() noexcept;

    [[nodiscard]] std::size_t pass_count() const noexcept
    {
        return passes_.size();
    }
    [[nodiscard]] const std::string& resource_name(std::uint32_t resource) const;

private:
    struct Resource
    {
        std::string name;
        GraphAccess initial{};
        GraphAccess final{};
    };
    struct Pass
    {
        std::string name;
        std::vector<Use> uses;
        std::function<void()> record;
    };
    std::vector<Resource> resources_;
    std::vector<Pass> passes_;
    std::vector<GraphAccess> current_;
    std::vector<GraphTransition> batch_;
};

} // namespace eaw
