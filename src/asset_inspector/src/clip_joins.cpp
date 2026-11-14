// clip-joins: whether a model's clips meet where the engine joins them. For each join (the end of one
// clip and the start of the next: idle to MOVESTART, MOVESTART to MOVE, MOVE at each quarter of its
// stride to the matching MOVE_END clip, a MOVE_END clip to idle, and the turns' BEGIN, loop, and END),
// the largest distance any bone's model-space position jumps. A join near 0 was authored to meet;
// a large one pops when the engine cuts from one clip to the other.

#include "core/string_utils.hpp"
#include "formats/ala_animation.hpp"
#include "assets/alamo_loaders.hpp"
#include "assets/pose.hpp"
#include "asset_inspector/inspector.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <optional>

namespace eaw::asset_inspector {

namespace {

struct LoadedClip
{
    std::string name;
    eaw::Animation animation;
};

} // namespace

eaw::Status clip_joins(const eaw::VirtualFileSystem& vfs, const std::string& model_path)
{
    EAW_TRY_ASSIGN(const auto model, decode_asset<eaw::AloModelData>(vfs, model_path));
    const auto skeleton = eaw::load_skeleton(model);
    const auto upper = eaw::ascii_upper(model_path);
    const auto stem = upper.substr(0, upper.rfind(".ALO"));
    const auto load = [&](const std::string& suffix) -> std::optional<LoadedClip>
    {
        const auto path = stem + "_" + suffix + ".ALA";
        const auto bytes = vfs.read(path);
        if (!bytes)
        {
            return std::nullopt;
        }
        const auto data = eaw::read_ala(*bytes);
        if (!data)
        {
            return std::nullopt;
        }
        return LoadedClip{suffix, eaw::retarget_by_bone_name(eaw::load_animation(*data), skeleton)};
    };
    // Model-space bone positions of `clip` at `frame` (the last playable frame for -1).
    const auto positions = [&](const LoadedClip& clip, const double frame)
    {
        const auto frames = std::max<std::uint32_t>(clip.animation.loop_frame_count, 1U);
        const auto fps = clip.animation.frames_per_second > 0.0F ? clip.animation.frames_per_second : 30.0F;
        // The last frame is sampled just before the loop wraps.
        const auto at = frame < 0.0 ? (static_cast<double>(frames) - 0.001) : frame;
        const auto pose = eaw::sample_pose(clip.animation, skeleton, static_cast<float>(at / fps));
        std::vector<eaw::Vector3> out;
        for (const auto& bone : pose)
        {
            out.push_back(eaw::translation(bone.transform));
        }
        return out;
    };
    const auto join = [&](const std::optional<LoadedClip>& from, const double from_frame, const std::optional<LoadedClip>& to, const std::string& label)
    {
        if (!from || !to)
        {
            return;
        }
        const auto a = positions(*from, from_frame);
        const auto b = positions(*to, 0.0);
        float worst = 0.0F;
        std::size_t worst_bone = 0;
        for (std::size_t bone = 0; bone < a.size() && bone < b.size(); ++bone)
        {
            // Proxies (effect attachment points, P_*) are not always keyed alike in every clip; the
            // body's bones decide whether a join shows.
            if (bone < skeleton.bones.size() && (skeleton.bones[bone].name.starts_with("P_") || skeleton.bones[bone].name.starts_with("p_")))
            {
                continue;
            }
            const auto gap = eaw::length(a[bone] - b[bone]);
            if (gap > worst)
            {
                worst = gap;
                worst_bone = bone;
            }
        }
        std::cout << "  " << std::left << std::setw(44) << label << std::right << " largest jump " << std::setw(8) << worst << " ("
                  << (worst_bone < skeleton.bones.size() ? skeleton.bones[worst_bone].name : std::string("?")) << ")\n";
    };
    const auto idle = load("IDLE_00");
    const auto start = load("MOVESTART_00");
    const auto move = load("MOVE_00");
    static constexpr std::array<const char*, 4> ends{"MOVE_ENDONE_00", "MOVE_ENDTWO_00", "MOVE_ENDTHREE_00", "MOVE_ENDFOUR_00"};
    std::cout << "Clip joins of " << model_path << " (largest bone jump, world units)\n";
    join(idle, -1.0, idle, "IDLE end -> IDLE start (loop)");
    join(idle, 0.0, start, "IDLE start -> MOVESTART start");
    if (idle)
    {
        // How far IDLE strays from its first frame mid-loop (a cut into MOVESTART there pops by it).
        const auto frames = static_cast<double>(std::max<std::uint32_t>(idle->animation.loop_frame_count, 1U));
        for (std::size_t at = 1; at < 4; ++at)
        {
            join(idle, frames * static_cast<double>(at) / 4.0, start, std::string("IDLE at ") + std::to_string(at) + "/4 -> MOVESTART start");
        }
    }
    join(start, -1.0, move, "MOVESTART end -> MOVE start");
    // A leg lifted for walking and turning, dropped for standing (the pod walker's middle leg):
    // between IDLE and the walk and turn clips.
    const auto lift = load("IDLE_LEGLIFT_00");
    const auto drop = load("IDLE_LEGDROP_00");
    join(idle, -1.0, lift, "IDLE end -> IDLE_LEGLIFT start");
    join(idle, 0.0, lift, "IDLE start -> IDLE_LEGLIFT start");
    join(lift, -1.0, start, "IDLE_LEGLIFT end -> MOVESTART start");
    join(lift, -1.0, load("TURNL_BEGIN_00"), "IDLE_LEGLIFT end -> TURNL_BEGIN start");
    join(load("MOVE_ENDONE_00"), -1.0, drop, "MOVE_ENDONE_00 end -> IDLE_LEGDROP start");
    join(load("TURNL_END_00"), -1.0, drop, "TURNL_END end -> IDLE_LEGDROP start");
    join(drop, -1.0, idle, "IDLE_LEGDROP end -> IDLE start");
    // A second idle may stand with the leg raised: whether it meets the raised clips.
    const auto idle_raised = load("IDLE_01");
    join(lift, -1.0, idle_raised, "IDLE_LEGLIFT end -> IDLE_01 start");
    join(idle_raised, -1.0, idle_raised, "IDLE_01 end -> IDLE_01 start (loop)");
    join(idle_raised, 0.0, start, "IDLE_01 start -> MOVESTART start");
    join(idle_raised, 0.0, drop, "IDLE_01 start -> IDLE_LEGDROP start");
    join(load("MOVE_ENDONE_00"), -1.0, idle_raised, "MOVE_ENDONE_00 end -> IDLE_01 start");
    join(idle, 0.0, idle_raised, "IDLE start -> IDLE_01 start");
    // How far an attack strays from its first frame (an overlay adds that to the pose below).
    if (const auto attack = load("ATTACK_00"))
    {
        const auto frames = static_cast<double>(std::max<std::uint32_t>(attack->animation.loop_frame_count, 1U));
        for (std::size_t at = 1; at < 4; ++at)
        {
            join(attack, frames * static_cast<double>(at) / 4.0, attack, std::string("ATTACK at ") + std::to_string(at) + "/4 vs its start");
        }
    }
    join(move, -1.0, move, "MOVE end -> MOVE start (loop)");
    if (move)
    {
        // The stride is split by the number of MOVE_END clips the model has (four on the AT-AT, three
        // on the AT-ST and pod walkers).
        std::size_t count = 0;
        while (count < ends.size() && load(ends[count]))
        {
            ++count;
        }
        const auto frames = static_cast<double>(std::max<std::uint32_t>(move->animation.loop_frame_count, 1U));
        for (std::size_t part = 0; part < count; ++part)
        {
            const auto end = load(ends[part]);
            for (std::size_t at = 0; at < count; ++at)
            {
                join(move, frames * static_cast<double>(at) / static_cast<double>(count), end,
                     std::string("MOVE at ") + std::to_string(at) + "/" + std::to_string(count) + " -> " + ends[part] + " start");
            }
            join(end, -1.0, idle, std::string(ends[part]) + " end -> IDLE start");
        }
    }
    for (const auto* side : {"L", "R"})
    {
        const auto begin = load(std::string("TURN") + side + "_BEGIN_00");
        const auto loop = load(std::string("TURN") + side + "_00");
        const auto end = load(std::string("TURN") + side + "_END_00");
        join(idle, 0.0, begin, std::string("IDLE start -> TURN") + side + "_BEGIN start");
        join(begin, -1.0, loop, std::string("TURN") + side + "_BEGIN end -> TURN" + side + " start");
        join(loop, -1.0, loop, std::string("TURN") + side + " end -> start (loop)");
        join(loop, -1.0, end, std::string("TURN") + side + " end -> TURN" + side + "_END start");
        if (loop)
        {
            const auto frames = static_cast<double>(std::max<std::uint32_t>(loop->animation.loop_frame_count, 1U));
            for (std::size_t at = 1; at < 4; ++at)
            {
                join(loop, frames * static_cast<double>(at) / 4.0, end, std::string("TURN") + side + " at " + std::to_string(at) + "/4 -> TURN" + side + "_END start");
            }
        }
        join(begin, -1.0, end, std::string("TURN") + side + "_BEGIN end -> TURN" + side + "_END start");
        join(end, -1.0, idle, std::string("TURN") + side + "_END end -> IDLE start");
        // The partial turns (HALF, QUARTER): whether each is a loop, or a one-shot from BEGIN or IDLE
        // into END or IDLE.
        for (const auto* part : {"HALF", "QUARTER"})
        {
            const auto name = std::string("TURN") + side + "_" + part;
            const auto clip = load(name + "_00");
            join(clip, -1.0, clip, name + " end -> start (loop)");
            join(idle, 0.0, clip, "IDLE start -> " + name + " start");
            join(begin, -1.0, clip, std::string("TURN") + side + "_BEGIN end -> " + name + " start");
            join(loop, -1.0, clip, std::string("TURN") + side + " end -> " + name + " start");
            join(clip, -1.0, loop, name + " end -> TURN" + side + " start");
            join(clip, -1.0, end, name + " end -> TURN" + side + "_END start");
            join(clip, -1.0, idle, name + " end -> IDLE start");
        }
    }
    return {};
}

} // namespace eaw::asset_inspector
