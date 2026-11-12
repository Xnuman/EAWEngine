#pragma once

#include "math/matrix.hpp"
#include "math/quaternion.hpp"
#include "math/vector.hpp"
#include "formats/alo_model.hpp"

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
    Quaternion rotation{Quaternion::identity()};
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

struct AlaAnimationData
{
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
};

// The error names the malformed chunk.
[[nodiscard]] Result<AlaAnimationData> read_ala(std::span<const std::byte> bytes);

} // namespace eaw
