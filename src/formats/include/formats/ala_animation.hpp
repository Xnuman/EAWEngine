#pragma once

#include "formats/alo_model.hpp"
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
};

} // namespace eaw
