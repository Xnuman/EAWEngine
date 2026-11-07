#include "eaw/ala_animation.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace eaw {
namespace {

struct Chunk {
    std::uint32_t type{};
    std::size_t payload{};
    std::size_t end{};
};

struct MiniChunk {
    std::uint8_t type{};
    std::size_t payload{};
    std::size_t end{};
};

std::uint32_t read_u32(const std::span<const std::byte> bytes,
                       const std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        throw std::runtime_error("ALA ended while reading a 32-bit value");
    }
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<unsigned char>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

std::uint16_t read_u16(const std::span<const std::byte> bytes,
                       const std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("ALA ended while reading a 16-bit value");
    }
    return static_cast<std::uint16_t>(
        std::to_integer<unsigned char>(bytes[offset]) |
        (std::to_integer<unsigned char>(bytes[offset + 1]) << 8U));
}

std::int16_t read_i16(const std::span<const std::byte> bytes,
                      const std::size_t offset) {
    return static_cast<std::int16_t>(read_u16(bytes, offset));
}

float read_float(const std::span<const std::byte> bytes,
                 const std::size_t offset) {
    return std::bit_cast<float>(read_u32(bytes, offset));
}

std::string read_string(const std::span<const std::byte> bytes,
                        const std::size_t begin,
                        const std::size_t end) {
    if (begin > end || end > bytes.size()) {
        throw std::runtime_error("ALA string range is invalid");
    }
    std::string value;
    for (auto offset = begin; offset < end; ++offset) {
        const auto character = std::to_integer<unsigned char>(bytes[offset]);
        if (character == 0) {
            break;
        }
        value.push_back(static_cast<char>(character));
    }
    return value;
}

std::vector<Chunk> read_chunks(const std::span<const std::byte> bytes,
                               const std::size_t begin,
                               const std::size_t end) {
    if (begin > end || end > bytes.size()) {
        throw std::runtime_error("ALA chunk range is invalid");
    }
    std::vector<Chunk> chunks;
    auto cursor = begin;
    while (cursor < end) {
        if (end - cursor < 8) {
            throw std::runtime_error("ALA contains a truncated chunk header");
        }
        const auto type = read_u32(bytes, cursor);
        const auto size = static_cast<std::size_t>(read_u32(bytes, cursor + 4) &
                                                   0x7fffffffU);
        const auto payload = cursor + 8;
        if (size > end - payload) {
            std::ostringstream message;
            message << "ALA chunk 0x" << std::hex << type << " extends past its parent";
            throw std::runtime_error(message.str());
        }
        chunks.push_back({type, payload, payload + size});
        cursor = payload + size;
    }
    return chunks;
}

std::vector<MiniChunk> read_minis(const std::span<const std::byte> bytes,
                                  const Chunk& chunk) {
    std::vector<MiniChunk> chunks;
    auto cursor = chunk.payload;
    while (cursor < chunk.end) {
        if (chunk.end - cursor < 2) {
            throw std::runtime_error("ALA contains a truncated mini-chunk header");
        }
        const auto type = std::to_integer<std::uint8_t>(bytes[cursor]);
        const auto size = std::to_integer<std::uint8_t>(bytes[cursor + 1]);
        const auto payload = cursor + 2;
        if (size > chunk.end - payload) {
            throw std::runtime_error("ALA mini-chunk extends past its parent");
        }
        chunks.push_back({type, payload, payload + size});
        cursor = payload + size;
    }
    return chunks;
}

const Chunk* find_chunk(const std::vector<Chunk>& chunks,
                        const std::uint32_t type) {
    for (const auto& chunk : chunks) {
        if (chunk.type == type) {
            return &chunk;
        }
    }
    return nullptr;
}

const MiniChunk* find_mini(const std::vector<MiniChunk>& chunks,
                           const std::uint8_t type) {
    for (const auto& chunk : chunks) {
        if (chunk.type == type) {
            return &chunk;
        }
    }
    return nullptr;
}

std::uint32_t mini_u32(const std::span<const std::byte> bytes,
                       const MiniChunk* chunk,
                       const std::uint32_t fallback = 0) {
    return chunk != nullptr && chunk->end - chunk->payload >= 4
               ? read_u32(bytes, chunk->payload)
               : fallback;
}

bool active_index(const std::span<const std::byte> bytes,
                  const MiniChunk* chunk) {
    if (chunk == nullptr) {
        return false;
    }
    if (chunk->end - chunk->payload >= 4) {
        return read_u32(bytes, chunk->payload) != 0xffffffffU;
    }
    return chunk->end - chunk->payload >= 2 &&
           read_u16(bytes, chunk->payload) != 0xffffU;
}

std::int32_t track_index(const std::span<const std::byte> bytes,
                         const MiniChunk* chunk) {
    if (!active_index(bytes, chunk)) {
        return -1;
    }
    return chunk->end - chunk->payload >= 4
               ? static_cast<std::int32_t>(read_u32(bytes, chunk->payload))
               : static_cast<std::int32_t>(read_u16(bytes, chunk->payload));
}

AloVector3 mini_vector3(const std::span<const std::byte> bytes,
                        const MiniChunk* chunk,
                        const AloVector3 fallback = {}) {
    return chunk != nullptr && chunk->end - chunk->payload >= 12
               ? AloVector3{read_float(bytes, chunk->payload),
                            read_float(bytes, chunk->payload + 4),
                            read_float(bytes, chunk->payload + 8)}
               : fallback;
}

AloQuaternion packed_quaternion(const std::span<const std::byte> bytes,
                                const std::size_t offset) {
    constexpr auto divisor = static_cast<float>(std::numeric_limits<std::int16_t>::max());
    return {read_i16(bytes, offset) / divisor,
            read_i16(bytes, offset + 2) / divisor,
            read_i16(bytes, offset + 4) / divisor,
            read_i16(bytes, offset + 6) / divisor};
}

struct TrackDecodeInfo {
    AlaBoneTrack track;
    AloVector3 translation_offset;
    AloVector3 translation_scale;
    AloVector3 scale_offset{1.0F, 1.0F, 1.0F};
    AloVector3 scale_scale;
    AloQuaternion default_rotation;
    std::int32_t translation_index{-1};
    std::int32_t scale_index{-1};
    std::int32_t rotation_index{-1};
    Chunk version_one_translation;
    Chunk version_one_scale;
    Chunk version_one_rotation;
    Chunk visibility;
    bool has_version_one_translation{};
    bool has_version_one_scale{};
    bool has_version_one_rotation{};
    bool has_visibility{};
};

TrackDecodeInfo parse_track(const std::span<const std::byte> bytes,
                            const Chunk& track_chunk,
                            const std::uint32_t version) {
    TrackDecodeInfo information_result;
    auto& track = information_result.track;
    const auto children = read_chunks(bytes, track_chunk.payload, track_chunk.end);
    const auto* information = find_chunk(children, 0x1003);
    if (information == nullptr) {
        throw std::runtime_error("ALA bone track is missing chunk 0x1003");
    }
    const auto minis = read_minis(bytes, *information);
    if (const auto* name = find_mini(minis, 4)) {
        track.name = read_string(bytes, name->payload, name->end);
    }
    track.bone_index = mini_u32(bytes, find_mini(minis, 5));
    information_result.translation_offset =
        mini_vector3(bytes, find_mini(minis, 6));
    information_result.translation_scale =
        mini_vector3(bytes, find_mini(minis, 7));
    information_result.scale_offset =
        mini_vector3(bytes, find_mini(minis, 8), {1.0F, 1.0F, 1.0F});
    information_result.scale_scale = mini_vector3(bytes, find_mini(minis, 9));
    if (version == 2) {
        information_result.translation_index =
            track_index(bytes, find_mini(minis, 14));
        information_result.scale_index = track_index(bytes, find_mini(minis, 15));
        information_result.rotation_index = track_index(bytes, find_mini(minis, 16));
        track.translation = information_result.translation_index >= 0;
        track.scale = information_result.scale_index >= 0;
        track.rotation = information_result.rotation_index >= 0;
        if (const auto* rotation = find_mini(minis, 17);
            rotation != nullptr && rotation->end - rotation->payload >= 8) {
            information_result.default_rotation =
                packed_quaternion(bytes, rotation->payload);
        }
    } else {
        if (const auto* translation = find_chunk(children, 0x1004)) {
            information_result.version_one_translation = *translation;
            information_result.has_version_one_translation = true;
            track.translation = true;
        }
        if (const auto* scale = find_chunk(children, 0x1005)) {
            information_result.version_one_scale = *scale;
            information_result.has_version_one_scale = true;
            track.scale = true;
        }
        if (const auto* rotation = find_chunk(children, 0x1006)) {
            information_result.version_one_rotation = *rotation;
            information_result.has_version_one_rotation = true;
            if (rotation->end - rotation->payload == 8) {
                information_result.default_rotation =
                    packed_quaternion(bytes, rotation->payload);
            } else {
                track.rotation = true;
            }
        }
    }
    if (const auto* visibility = find_chunk(children, 0x1007)) {
        information_result.visibility = *visibility;
        information_result.has_visibility = true;
        track.visibility = true;
    }
    track.stepped = find_chunk(children, 0x1008) != nullptr;
    return information_result;
}

AloVector3 unpack_vector(const std::span<const std::byte> bytes,
                         const std::size_t offset,
                         const AloVector3& base,
                         const AloVector3& scale) {
    return {base.x + static_cast<float>(read_u16(bytes, offset)) * scale.x,
            base.y + static_cast<float>(read_u16(bytes, offset + 2)) * scale.y,
            base.z + static_cast<float>(read_u16(bytes, offset + 4)) * scale.z};
}

bool range_contains(const Chunk* chunk,
                    const std::size_t offset,
                    const std::size_t size) {
    return chunk != nullptr && offset >= chunk->payload && offset <= chunk->end &&
           size <= chunk->end - offset;
}

void decode_track_frames(const std::span<const std::byte> bytes,
                         TrackDecodeInfo& information,
                         const AlaAnimation& animation,
                         const Chunk* translations,
                         const Chunk* rotations,
                         const Chunk* scales,
                         std::vector<std::string>& warnings) {
    auto& track = information.track;
    track.frames.reserve(animation.stored_frame_count);
    bool warned_translation = false;
    bool warned_rotation = false;
    bool warned_scale = false;
    for (std::uint32_t frame_index = 0; frame_index < animation.stored_frame_count;
         ++frame_index) {
        AlaBoneFrame frame;
        frame.translation = information.translation_offset;
        frame.scale = information.scale_offset;
        frame.rotation = information.default_rotation;
        if (information.has_visibility) {
            const auto byte_offset = information.visibility.payload + frame_index / 8U;
            if (byte_offset < information.visibility.end) {
                const auto bits = std::to_integer<unsigned char>(bytes[byte_offset]);
                frame.visible = ((bits >> (frame_index % 8U)) & 1U) != 0;
            }
        }

        if (animation.version == 2) {
            if (track.translation) {
                const auto word = static_cast<std::size_t>(frame_index) *
                                      animation.translation_block_words +
                                  static_cast<std::size_t>(information.translation_index);
                const auto offset = translations == nullptr ? 0 : translations->payload + word * 2U;
                if (range_contains(translations, offset, 6)) {
                    frame.translation = unpack_vector(bytes, offset,
                                                      information.translation_offset,
                                                      information.translation_scale);
                } else if (!warned_translation) {
                    warnings.push_back("Translation track data is truncated for " + track.name);
                    warned_translation = true;
                }
            }
            if (track.scale) {
                const auto word = static_cast<std::size_t>(frame_index) *
                                      animation.scale_block_words +
                                  static_cast<std::size_t>(information.scale_index);
                const auto offset = scales == nullptr ? 0 : scales->payload + word * 2U;
                if (range_contains(scales, offset, 6)) {
                    frame.scale = unpack_vector(bytes, offset, information.scale_offset,
                                                information.scale_scale);
                } else if (!warned_scale) {
                    warnings.push_back("Scale track data is truncated for " + track.name);
                    warned_scale = true;
                }
            }
            if (track.rotation) {
                const auto word = static_cast<std::size_t>(frame_index) *
                                      animation.rotation_block_words +
                                  static_cast<std::size_t>(information.rotation_index);
                const auto offset = rotations == nullptr ? 0 : rotations->payload + word * 2U;
                if (range_contains(rotations, offset, 8)) {
                    frame.rotation = packed_quaternion(bytes, offset);
                } else if (!warned_rotation) {
                    warnings.push_back("Rotation track data is truncated for " + track.name);
                    warned_rotation = true;
                }
            }
        } else {
            if (information.has_version_one_translation) {
                const auto offset = information.version_one_translation.payload +
                                    static_cast<std::size_t>(frame_index) * 6U;
                if (range_contains(&information.version_one_translation, offset, 6)) {
                    frame.translation = unpack_vector(bytes, offset,
                                                      information.translation_offset,
                                                      information.translation_scale);
                }
            }
            if (information.has_version_one_scale) {
                const auto offset = information.version_one_scale.payload +
                                    static_cast<std::size_t>(frame_index) * 6U;
                if (range_contains(&information.version_one_scale, offset, 6)) {
                    frame.scale = unpack_vector(bytes, offset, information.scale_offset,
                                                information.scale_scale);
                }
            }
            if (track.rotation && information.has_version_one_rotation) {
                const auto offset = information.version_one_rotation.payload +
                                    static_cast<std::size_t>(frame_index) * 8U;
                if (range_contains(&information.version_one_rotation, offset, 8)) {
                    frame.rotation = packed_quaternion(bytes, offset);
                }
            }
        }
        track.frames.push_back(frame);
    }
}

AloVector3 interpolate(const AloVector3& left,
                       const AloVector3& right,
                       const float amount) {
    return {left.x + (right.x - left.x) * amount,
            left.y + (right.y - left.y) * amount,
            left.z + (right.z - left.z) * amount};
}

AloQuaternion slerp(AloQuaternion left,
                    AloQuaternion right,
                    const float amount) {
    auto dot = left.x * right.x + left.y * right.y + left.z * right.z +
               left.w * right.w;
    if (dot < 0.0F) {
        dot = -dot;
        right = {-right.x, -right.y, -right.z, -right.w};
    }
    if (dot > 0.9995F) {
        AloQuaternion result{left.x + (right.x - left.x) * amount,
                             left.y + (right.y - left.y) * amount,
                             left.z + (right.z - left.z) * amount,
                             left.w + (right.w - left.w) * amount};
        const auto length = std::sqrt(result.x * result.x + result.y * result.y +
                                      result.z * result.z + result.w * result.w);
        if (length > 0.0F) {
            result = {result.x / length, result.y / length, result.z / length,
                      result.w / length};
        }
        return result;
    }
    const auto theta = std::acos(std::clamp(dot, -1.0F, 1.0F));
    const auto sine = std::sin(theta);
    const auto left_weight = std::sin((1.0F - amount) * theta) / sine;
    const auto right_weight = std::sin(amount * theta) / sine;
    return {left.x * left_weight + right.x * right_weight,
            left.y * left_weight + right.y * right_weight,
            left.z * left_weight + right.z * right_weight,
            left.w * left_weight + right.w * right_weight};
}

} // namespace

AlaAnimation AlaAnimation::parse(const std::span<const std::byte> bytes) {
    AlaAnimation animation;
    const auto root_chunks = read_chunks(bytes, 0, bytes.size());
    if (root_chunks.size() != 1 || root_chunks.front().type != 0x1000) {
        throw std::runtime_error("file does not begin with an ALA animation container");
    }
    const auto chunks = read_chunks(bytes, root_chunks.front().payload,
                                    root_chunks.front().end);
    const auto* information = find_chunk(chunks, 0x1001);
    if (information == nullptr) {
        throw std::runtime_error("ALA is missing animation information chunk 0x1001");
    }
    const auto minis = read_minis(bytes, *information);
    animation.stored_frame_count = mini_u32(bytes, find_mini(minis, 1));
    if (const auto* fps = find_mini(minis, 2);
        fps != nullptr && fps->end - fps->payload >= 4) {
        animation.frames_per_second = read_float(bytes, fps->payload);
    }
    const auto declared_tracks = mini_u32(bytes, find_mini(minis, 3));
    if (find_mini(minis, 11) != nullptr) {
        animation.version = 2;
        animation.rotation_block_words = mini_u32(bytes, find_mini(minis, 11));
        animation.translation_block_words = mini_u32(bytes, find_mini(minis, 12));
        animation.scale_block_words = mini_u32(bytes, find_mini(minis, 13));
    } else {
        animation.rotation_block_words = declared_tracks * 4U;
        animation.translation_block_words = declared_tracks * 3U;
        animation.scale_block_words = declared_tracks * 3U;
    }

    std::vector<TrackDecodeInfo> track_information;
    const Chunk* rotation_data = nullptr;
    const Chunk* translation_data = nullptr;
    const Chunk* scale_data = nullptr;
    for (const auto& chunk : chunks) {
        if (chunk.type == 0x1002) {
            track_information.push_back(parse_track(bytes, chunk, animation.version));
        } else if (chunk.type == 0x1009) {
            animation.rotation_data_bytes = chunk.end - chunk.payload;
            rotation_data = &chunk;
        } else if (chunk.type == 0x100a) {
            animation.translation_data_bytes = chunk.end - chunk.payload;
            translation_data = &chunk;
        } else if (chunk.type == 0x100b) {
            animation.scale_data_bytes = chunk.end - chunk.payload;
            scale_data = &chunk;
        }
    }
    if (declared_tracks != track_information.size()) {
        animation.warnings.push_back("Animation declares " +
                                     std::to_string(declared_tracks) +
                                     " bone tracks but contains " +
                                     std::to_string(track_information.size()));
    }
    if (animation.frames_per_second <= 0.0F) {
        animation.warnings.emplace_back("Animation FPS is zero or negative");
    }
    animation.tracks.reserve(track_information.size());
    for (auto& track : track_information) {
        decode_track_frames(bytes, track, animation, translation_data, rotation_data,
                            scale_data, animation.warnings);
        animation.tracks.push_back(std::move(track.track));
    }
    return animation;
}

std::uint32_t AlaAnimation::playable_frame_count() const noexcept {
    return stored_frame_count == 0 ? 0 : stored_frame_count - 1;
}

float AlaAnimation::duration_seconds() const noexcept {
    return frames_per_second > 0.0F
               ? static_cast<float>(playable_frame_count()) / frames_per_second
               : 0.0F;
}

std::vector<AlaPoseBone> AlaAnimation::sample_pose(const AloModel& model,
                                                  float seconds) const {
    auto local_pose = model.local_bind_pose();
    std::vector<bool> visibility;
    visibility.reserve(model.bones.size());
    for (const auto& bone : model.bones) {
        visibility.push_back(bone.visible);
    }

    const auto playable = playable_frame_count();
    if (playable != 0 && frames_per_second > 0.0F) {
        auto position = std::fmod(seconds * frames_per_second,
                                  static_cast<float>(playable));
        if (position < 0.0F) {
            position += static_cast<float>(playable);
        }
        const auto first = static_cast<std::size_t>(std::floor(position));
        const auto second = std::min(first + 1,
                                     static_cast<std::size_t>(stored_frame_count - 1));
        const auto amount = position - static_cast<float>(first);
        for (const auto& track : tracks) {
            if (track.bone_index >= local_pose.size() ||
                first >= track.frames.size() || second >= track.frames.size()) {
                continue;
            }
            const auto& left = track.frames[first];
            const auto& right = track.frames[second];
            const auto interpolation = track.stepped ? 0.0F : amount;
            local_pose[track.bone_index] = alo_compose_transform(
                interpolate(left.scale, right.scale, interpolation),
                slerp(left.rotation, right.rotation, interpolation),
                interpolate(left.translation, right.translation, interpolation));
            visibility[track.bone_index] = left.visible;
        }
    }

    std::vector<AlaPoseBone> result;
    result.reserve(model.bones.size());
    for (std::size_t index = 0; index < model.bones.size(); ++index) {
        auto transform = local_pose[index];
        if (model.bones[index].parent >= 0 &&
            static_cast<std::size_t>(model.bones[index].parent) < result.size()) {
            const auto parent = static_cast<std::size_t>(model.bones[index].parent);
            transform = alo_multiply(transform, result[parent].transform);
            visibility[index] = visibility[index] && result[parent].visible;
        }
        result.push_back({transform, visibility[index]});
    }
    return result;
}

} // namespace eaw
