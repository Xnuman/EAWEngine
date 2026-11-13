// gait: how fast a walk clip moves its unit over the ground, measured from the clip itself. A
// planted foot moves backward relative to the body at the body's ground speed, so the bones that
// touch the ground (those reaching the lowest point of the clip) are followed while they are down,
// and their mean speed there is the speed at which the clip plays without sliding at 1x. Reported in
// world units per second and per 30 Hz frame (Max_Speed's unit, legacy_units).

#include "core/string_utils.hpp"
#include "formats/ala_animation.hpp"
#include "assets/alamo_loaders.hpp"
#include "assets/pose.hpp"
#include "asset_inspector/inspector.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>

namespace eaw::asset_inspector {

namespace {

// A bone counts as planted while it is this share of the clip's height range above its lowest point
// (an inspection threshold).
constexpr float planted_share = 0.04F;
// Bones whose lowest point is within this share of the height range of the clip's lowest point
// touch the ground.
constexpr float ground_share = 0.08F;
constexpr float legacy_frames_per_second = 30.0F;

} // namespace

eaw::Status gait(const eaw::VirtualFileSystem& vfs, const std::string& clip_path)
{
    // The model: the clip's name without its trailing _PART parts, the longest that exists.
    const auto upper = eaw::ascii_upper(clip_path);
    const auto dot = upper.rfind(".ALA");
    if (dot == std::string::npos)
    {
        return eaw::make_error("gait: " + clip_path + " is not an .ALA clip");
    }
    std::string model_path;
    for (auto cut = upper.rfind('_', dot); cut != std::string::npos && cut > 0; cut = upper.rfind('_', cut - 1))
    {
        const auto candidate = upper.substr(0, cut) + ".ALO";
        if (vfs.read(candidate))
        {
            model_path = candidate;
            break;
        }
    }
    if (model_path.empty())
    {
        return eaw::make_error("gait: no model found for " + clip_path);
    }
    EAW_TRY_ASSIGN(const auto model, decode_asset<eaw::AloModelData>(vfs, model_path));
    EAW_TRY_ASSIGN(const auto clip_bytes, vfs.read(clip_path));
    EAW_TRY_ASSIGN(const auto clip_data, eaw::read_ala(clip_bytes));
    const auto skeleton = eaw::load_skeleton(model);
    const auto animation = eaw::retarget_by_bone_name(eaw::load_animation(clip_data), skeleton);
    const auto frames = std::max<std::uint32_t>(animation.loop_frame_count, 1U);
    const auto fps = animation.frames_per_second > 0.0F ? animation.frames_per_second : legacy_frames_per_second;
    const auto bones = skeleton.bones.size();

    // Every bone's model-space position at every frame.
    std::vector<std::vector<eaw::Vector3>> positions(bones, std::vector<eaw::Vector3>(frames));
    for (std::uint32_t frame = 0; frame < frames; ++frame)
    {
        const auto pose = eaw::sample_pose(animation, skeleton, static_cast<float>(frame) / fps);
        for (std::size_t bone = 0; bone < bones && bone < pose.size(); ++bone)
        {
            positions[bone][frame] = eaw::translation(pose[bone].transform);
        }
    }
    float lowest = 1.0e9F;
    float highest = -1.0e9F;
    for (const auto& track : positions)
    {
        for (const auto& point : track)
        {
            lowest = std::min(lowest, point.z);
            highest = std::max(highest, point.z);
        }
    }
    const auto range = std::max(highest - lowest, 1.0e-3F);
    std::cout << "Gait of " << clip_path << " on " << model_path << ": " << frames << " frames at " << fps << " fps (" << static_cast<float>(frames) / fps << " s)\n";
    const auto root_travel = eaw::length(positions[0][frames - 1] - positions[0][0]);
    std::cout << "  root travel over the clip: " << root_travel << '\n';

    float speed_sum = 0.0F;
    int feet = 0;
    for (std::size_t bone = 0; bone < bones; ++bone)
    {
        const auto& track = positions[bone];
        const auto low = std::ranges::min_element(track, {}, &eaw::Vector3::z)->z;
        if (low > lowest + range * ground_share)
        {
            continue;
        }
        // Mean horizontal speed while planted.
        float travel = 0.0F;
        int steps = 0;
        for (std::uint32_t frame = 0; frame < frames; ++frame)
        {
            const auto& a = track[frame];
            const auto& b = track[(frame + 1) % frames];
            if (a.z <= low + range * planted_share && b.z <= low + range * planted_share)
            {
                travel += std::hypot(b.x - a.x, b.y - a.y);
                ++steps;
            }
        }
        if (steps == 0)
        {
            continue;
        }
        const auto speed = travel / static_cast<float>(steps) * fps;
        std::cout << "  " << std::left << std::setw(28) << skeleton.bones[bone].name << std::right << " planted " << std::setw(3) << steps << " frames, ground speed " << speed << "/s ("
                  << speed / legacy_frames_per_second << " per frame)\n";
        speed_sum += speed;
        ++feet;
    }
    if (feet == 0)
    {
        std::cout << "  no planted bones found\n";
        return {};
    }
    const auto mean = speed_sum / static_cast<float>(feet);
    std::cout << "  mean ground speed at 1x: " << mean << "/s (" << mean / legacy_frames_per_second << " per 30 Hz frame)\n";

    // Turning: while two ground bones far apart are both planted, the line between them turns with
    // the body over the ground (the opposite way in the model's frame); its mean rate is the turn
    // rate at 1x, in degrees per 30 Hz frame (Max_Rate_Of_Turn's unit).
    std::vector<std::size_t> grounded;
    for (std::size_t bone = 0; bone < bones; ++bone)
    {
        const auto low = std::ranges::min_element(positions[bone], {}, &eaw::Vector3::z)->z;
        if (low <= lowest + range * ground_share && bone != 0)
        {
            grounded.push_back(bone);
        }
    }
    float best_turn = 0.0F;
    float best_span = 0.0F;
    for (std::size_t i = 0; i < grounded.size(); ++i)
    {
        for (std::size_t j = i + 1; j < grounded.size(); ++j)
        {
            const auto& a = positions[grounded[i]];
            const auto& b = positions[grounded[j]];
            const auto span = std::hypot(a[0].x - b[0].x, a[0].y - b[0].y);
            float turn = 0.0F;
            int steps = 0;
            for (std::uint32_t frame = 0; frame + 1 < frames; ++frame)
            {
                const auto planted = [&](const std::vector<eaw::Vector3>& track, const std::uint32_t at)
                {
                    const auto low = std::ranges::min_element(track, {}, &eaw::Vector3::z)->z;
                    return track[at].z <= low + range * planted_share;
                };
                if (planted(a, frame) && planted(a, frame + 1) && planted(b, frame) && planted(b, frame + 1))
                {
                    const auto before = std::atan2(a[frame].y - b[frame].y, a[frame].x - b[frame].x);
                    const auto after = std::atan2(a[frame + 1].y - b[frame + 1].y, a[frame + 1].x - b[frame + 1].x);
                    turn += std::remainder(after - before, 6.2831853F);
                    ++steps;
                }
            }
            if (steps >= 4 && span > best_span)
            {
                best_span = span;
                best_turn = turn / static_cast<float>(steps);
            }
        }
    }
    if (best_span > 0.0F)
    {
        const auto degrees_per_frame = best_turn * fps / legacy_frames_per_second * 57.29578F;
        std::cout << "  body turn at 1x (from planted bones " << best_span << " apart): " << -degrees_per_frame << " degrees per 30 Hz frame (positive: left)\n";
    }
    return {};
}

} // namespace eaw::asset_inspector
