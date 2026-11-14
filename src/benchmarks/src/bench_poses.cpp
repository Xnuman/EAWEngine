// Pose sampling, presentation's first work on the job system (docs/ecs-design.md, 7.5): one frame of
// UnitAnimator::animate in which every pose is new (an empty cache, as after a content load or with
// many unit types on screen), serially and with the poses sampled on the job system.

#include "math/quaternion.hpp"
#include "math/transform.hpp"
#include "jobs/job_system.hpp"
#include "assets/animation.hpp"
#include "assets/skeleton.hpp"
#include "presentation/unit_animation.hpp"
#include "presentation/visual_rig.hpp"

#include "benchmarks/benchmarks.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace eaw::benchmarks {

namespace {

// 256 meshes of one synthetic rig (40 bones in chains of 8, an idle clip of 60 keys turning every
// bone), one standing unit each, so the frame samples 256 different poses.
constexpr std::uint32_t mesh_count = 256;
constexpr std::uint32_t bone_count = 40;
constexpr std::uint32_t chain_length = 8;
constexpr std::uint32_t key_count = 60;
// Palette room for every pose (the renderer's limit plays no part here).
constexpr std::size_t palette_budget = std::size_t{1} << 24U;

struct Frame
{
    std::vector<VisualRig> rigs;
    std::vector<Matrix4> rest;
    std::vector<EntitySnapshot> snapshot;
    std::vector<EntityId> entities;
    std::vector<VisualInstance> instances;
    std::vector<Matrix4> palette;
};

Workload prepare_poses()
{
    auto skeleton = std::make_shared<Skeleton>();
    auto idle = std::make_shared<Animation>();
    idle->frames_per_second = 30.0F;
    idle->key_count = key_count;
    idle->loop_frame_count = key_count;
    for (std::uint32_t bone = 0; bone < bone_count; ++bone)
    {
        const auto parent = bone % chain_length == 0 ? -1 : static_cast<std::int32_t>(bone - 1);
        skeleton->bones.push_back({"B" + std::to_string(bone), parent, compose_transform({1.0F, 1.0F, 1.0F}, Quaternion::identity(), {0.0F, 0.0F, 2.0F})});
        AnimationTrack track{.bone_name = skeleton->bones.back().name, .bone = bone};
        for (std::uint32_t key = 0; key < key_count; ++key)
        {
            const auto angle = 0.02F * static_cast<float>(key) + 0.1F * static_cast<float>(bone);
            track.keys.push_back({.translation = {0.0F, 0.0F, 2.0F}, .rotation = axis_angle({0.0F, 1.0F, 0.0F}, angle)});
        }
        idle->tracks.push_back(std::move(track));
    }
    VisualRig rig;
    rig.skeleton = skeleton;
    rig.inverse_bind_pose = skeleton_inverse_bind_pose(*skeleton).value_or(std::vector<Matrix4>(bone_count, Matrix4::identity()));
    rig.palette_size = bone_count;
    rig.clips = {{AnimationRole::idle, 0, "IDLE_00", idle}};
    auto frame = std::make_shared<Frame>();
    frame->rigs.assign(mesh_count, rig);
    frame->rest.assign(bone_count, Matrix4::identity());
    for (std::uint32_t mesh = 0; mesh < mesh_count; ++mesh)
    {
        EntitySnapshot entity;
        entity.id = {mesh + 1U, 1};
        entity.max_speed = 10.0F;
        frame->snapshot.push_back(entity);
        frame->entities.push_back(entity.id);
        VisualInstance instance;
        instance.mesh = mesh;
        frame->instances.push_back(instance);
    }
    return {[frame](JobSystem* jobs)
            {
                // A new animator each run: nothing cached, every pose sampled.
                UnitAnimator animator(frame->rigs, frame->rest);
                auto entities = frame->entities;
                auto instances = frame->instances;
                animator.animate(frame->snapshot, entities, instances, 0.5, palette_budget, frame->palette, {}, jobs);
                return fingerprint(0, frame->palette.data(), frame->palette.size() * sizeof(Matrix4));
            }};
}

constexpr std::array<Suite, 1> suites{{
    {"poses", "one animation frame of 256 new 40-bone poses (empty pose cache), 4 to a job", &prepare_poses},
}};

} // namespace

std::span<const Suite> pose_suites()
{
    return suites;
}

} // namespace eaw::benchmarks
