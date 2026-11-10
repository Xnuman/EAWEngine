#pragma once

// A model's skeleton as the engine uses it, whatever file it came from: bones in parent-before-child
// order with their bind transforms relative to their parent and their visibility.

#include "math/matrix.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

struct SkeletonBone
{
    std::string name;
    // Index of the parent bone, or -1 for a root. A parent always comes before its children.
    std::int32_t parent{-1};
    // Bind transform relative to the parent (row-vector convention).
    Matrix4 local_bind{Matrix4::identity()};
    bool visible{true};
};

struct Skeleton
{
    std::vector<SkeletonBone> bones;
};

// Each bone's model-space bind transform.
[[nodiscard]] std::vector<Matrix4> skeleton_bind_pose(const Skeleton& skeleton);
// The first bone of that name (ASCII case-insensitive, surrounding whitespace ignored).
[[nodiscard]] std::optional<std::uint32_t> find_skeleton_bone(const Skeleton& skeleton, std::string_view name);

} // namespace eaw
