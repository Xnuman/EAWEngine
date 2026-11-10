#pragma once

// Sampling, blending, and composing skeleton poses from Alamo animation clips. This is animation
// runtime, not file data: it stays here, transitionally, until the game asset layer gives models a
// format-independent skeleton (AGENTS.md, "Reorganization in progress", step 5).

#include "formats/ala_animation.hpp"
#include "formats/alo_model.hpp"
#include "math/matrix.hpp"
#include "math/quaternion.hpp"
#include "math/vector.hpp"

#include <span>
#include <vector>

namespace eaw {

struct AlaPoseBone
{
    Matrix4 transform{Matrix4::identity()};
    bool visible{true};
};

// A bone of a local pose: its transform relative to its parent, as parts that blend, and its own
// visibility (before the parent's is applied).
struct AlaLocalBone
{
    Vector3 translation;
    Vector3 scale{1.0F, 1.0F, 1.0F};
    Quaternion rotation{Quaternion::identity()};
    bool visible{true};
};

// The model-space pose of `model` at `seconds` (looping).
[[nodiscard]] std::vector<AlaPoseBone> ala_sample_pose(const AlaAnimationData& animation, const AloModelData& model, float seconds);
// The pose at `seconds` (looping) relative to each bone's parent, for blending; bones without a
// track keep their bind transform. ala_compose_pose turns it into ala_sample_pose's form.
[[nodiscard]] std::vector<AlaLocalBone> ala_sample_local_pose(const AlaAnimationData& animation, const AloModelData& model, float seconds);

// A copy of `animation` whose tracks point at `model`'s bones by name (case-insensitive) instead of
// at the indices of the skeleton it was authored for; tracks naming no bone of the model are
// dropped. Clips shared between models (XML Land_Model_Anim_Override_Name) need it.
[[nodiscard]] AlaAnimationData ala_retarget_by_bone_name(const AlaAnimationData& animation, const AloModelData& model);

// A model's bind pose as a local pose.
[[nodiscard]] std::vector<AlaLocalBone> ala_bind_local_pose(const AloModelData& model);
// Moves `pose` toward `target` by `weight` (0 keeps `pose`, 1 takes `target`): rotations by
// normalized linear interpolation along the shorter arc, translations and scales linearly, and
// each bone's visibility from whichever side weighs more. Poses of different sizes blend over
// their common bones. Blending several clips one after another with weights w / (sum so far)
// gives their weighted average.
void ala_blend_pose(std::vector<AlaLocalBone>& pose, std::span<const AlaLocalBone> target, float weight);
// Model-space transforms and inherited visibility of a local pose (bones beyond the pose keep
// their bind transform).
[[nodiscard]] std::vector<AlaPoseBone> ala_compose_pose(const AloModelData& model, std::span<const AlaLocalBone> pose);

} // namespace eaw
