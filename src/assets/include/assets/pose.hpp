#pragma once

// Sampling, blending, and composing skeleton poses from animation clips.

#include "assets/animation.hpp"
#include "assets/skeleton.hpp"
#include "math/matrix.hpp"
#include "math/quaternion.hpp"
#include "math/vector.hpp"

#include <span>
#include <vector>

namespace eaw {

// A bone's model-space transform and visibility (its own and its ancestors').
struct PoseBone
{
    Matrix4 transform{Matrix4::identity()};
    bool visible{true};
};

// A bone of a local pose: its transform relative to its parent, as parts that blend, and its own
// visibility (before the parent's is applied).
struct LocalBone
{
    Vector3 translation;
    Vector3 scale{1.0F, 1.0F, 1.0F};
    Quaternion rotation{Quaternion::identity()};
    bool visible{true};
};

// The model-space pose of `skeleton` at `seconds` (looping).
[[nodiscard]] std::vector<PoseBone> sample_pose(const Animation& animation, const Skeleton& skeleton, float seconds);
// The pose at `seconds` (looping) relative to each bone's parent, for blending; bones without a
// track keep their bind transform. compose_pose turns it into sample_pose's form.
[[nodiscard]] std::vector<LocalBone> sample_local_pose(const Animation& animation, const Skeleton& skeleton, float seconds);

// A copy of `animation` whose tracks point at `skeleton`'s bones by name (case-insensitive)
// instead of at the indices of the skeleton it was authored for; tracks naming no bone of the
// skeleton are dropped. Clips shared between models (XML Land_Model_Anim_Override_Name) need it.
[[nodiscard]] Animation retarget_by_bone_name(const Animation& animation, const Skeleton& skeleton);

// A skeleton's bind pose as a local pose.
[[nodiscard]] std::vector<LocalBone> bind_local_pose(const Skeleton& skeleton);
// Moves `pose` toward `target` by `weight` (0 keeps `pose`, 1 takes `target`): rotations by
// normalized linear interpolation along the shorter arc, translations and scales linearly, and
// each bone's visibility from whichever side weighs more. Poses of different sizes blend over
// their common bones. Blending several clips one after another with weights w / (sum so far)
// gives their weighted average.
void blend_pose(std::vector<LocalBone>& pose, std::span<const LocalBone> target, float weight);
// Model-space transforms and inherited visibility of a local pose (bones beyond the pose keep
// their bind transform).
[[nodiscard]] std::vector<PoseBone> compose_pose(const Skeleton& skeleton, std::span<const LocalBone> pose);

} // namespace eaw
