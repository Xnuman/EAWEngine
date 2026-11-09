#pragma once

#include "eaw/alo_model.hpp"
#include "math/matrix.hpp"
#include "math/quaternion.hpp"
#include "math/vector.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace eaw {

struct AlaBoneFrame
{
    Vector3 translation;
    Vector3 scale{1.0F, 1.0F, 1.0F};
    Quaternion rotation;
    bool visible{true};
};

struct AlaPoseBone
{
    Matrix4 transform;
    bool visible{true};
};

// A bone of a local pose: its transform relative to its parent, as parts that blend, and its own
// visibility (before the parent's is applied).
struct AlaLocalBone
{
    Vector3 translation;
    Vector3 scale{1.0F, 1.0F, 1.0F};
    Quaternion rotation;
    bool visible{true};
};

struct AlaBoneTrack
{
    std::string name;
    std::uint32_t bone_index{};
    bool translation{};
    bool rotation{};
    bool scale{};
    bool visibility{};
    bool stepped{};
    std::vector<AlaBoneFrame> frames;
};

class AlaAnimation final
{
public:
    // The error names the malformed chunk.
    [[nodiscard]] static Result<AlaAnimation> parse(std::span<const std::byte> bytes);

    std::uint32_t version{1};
    std::uint32_t stored_frame_count{};
    float frames_per_second{};
    std::uint32_t rotation_block_words{};
    std::uint32_t translation_block_words{};
    std::uint32_t scale_block_words{};
    std::size_t rotation_data_bytes{};
    std::size_t translation_data_bytes{};
    std::size_t scale_data_bytes{};
    std::vector<AlaBoneTrack> tracks;
    std::vector<std::string> warnings;

    [[nodiscard]] std::uint32_t playable_frame_count() const noexcept;
    [[nodiscard]] float duration_seconds() const noexcept;
    [[nodiscard]] std::vector<AlaPoseBone> sample_pose(const AloModel& model, float seconds) const;
    // The pose at `seconds` (looping) relative to each bone's parent, for blending; bones without
    // a track keep their bind transform. ala_compose_pose turns it into sample_pose's form.
    [[nodiscard]] std::vector<AlaLocalBone> sample_local_pose(const AloModel& model, float seconds) const;
};

// A copy of `animation` whose tracks point at `model`'s bones by name (case-insensitive) instead of
// at the indices of the skeleton it was authored for; tracks naming no bone of the model are
// dropped. Clips shared between models (XML Land_Model_Anim_Override_Name) need it.
[[nodiscard]] AlaAnimation ala_retarget_by_bone_name(const AlaAnimation& animation, const AloModel& model);

// A model's bind pose as a local pose.
[[nodiscard]] std::vector<AlaLocalBone> ala_bind_local_pose(const AloModel& model);
// Moves `pose` toward `target` by `weight` (0 keeps `pose`, 1 takes `target`): rotations by
// normalized linear interpolation along the shorter arc, translations and scales linearly, and
// each bone's visibility from whichever side weighs more. Poses of different sizes blend over
// their common bones. Blending several clips one after another with weights w / (sum so far)
// gives their weighted average.
void ala_blend_pose(std::vector<AlaLocalBone>& pose, std::span<const AlaLocalBone> target, float weight);
// Model-space transforms and inherited visibility of a local pose (bones beyond the pose keep
// their bind transform).
[[nodiscard]] std::vector<AlaPoseBone> ala_compose_pose(const AloModel& model, std::span<const AlaLocalBone> pose);

} // namespace eaw
