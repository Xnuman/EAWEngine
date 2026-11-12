#include "assets/alamo_loaders.hpp"

#include "assets/alo_interpretation.hpp"
#include "formats/ala_animation_helpers.hpp"
#include "formats/alo_model_helpers.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace eaw {

Skeleton load_skeleton(const AloModelData& model)
{
    const auto local = alo_local_bind_pose(model);
    Skeleton skeleton;
    skeleton.bones.reserve(model.bones.size());
    for (std::size_t index = 0; index < model.bones.size(); ++index)
    {
        const auto& bone = model.bones[index];
        skeleton.bones.push_back({bone.name, bone.parent, local[index], bone.visible});
    }
    return skeleton;
}

Model load_model(const AloModelData& data)
{
    Model model;
    model.skeleton = load_skeleton(data);
    model.warnings = data.warnings;
    // Bone visibility is left to the scene: a mesh on a bone hidden in the bind pose appears when an
    // animation shows the bone. Damage states and detail levels are tagged; the scene builder
    // arranges them.
    model.meshes.reserve(data.meshes.size());
    for (std::size_t mesh_index = 0; mesh_index < data.meshes.size(); ++mesh_index)
    {
        const auto& mesh = data.meshes[mesh_index];
        ModelMesh loaded{mesh.name, mesh.attached_bone, mesh.hidden, {}, {}, {}};
        const auto variant = alo_mesh_variant(data, mesh);
        if (variant.kind == AloVariantKind::alternate)
        {
            loaded.damage_state = variant.index;
        }
        else if (variant.kind == AloVariantKind::level_of_detail)
        {
            loaded.detail_level = variant.index;
        }
        loaded.submeshes.reserve(mesh.submeshes.size());
        for (const auto& submesh : mesh.submeshes)
        {
            ModelSubmesh part;
            part.material = LegacyMaterial::translate(submesh);
            part.skinned = !submesh.skin_bones.empty();
            part.vertices.reserve(submesh.vertices.size());
            for (const auto& vertex : submesh.vertices)
            {
                VisualVertex out;
                out.position = vertex.position;
                out.normal = vertex.normal;
                out.texture_coordinates = vertex.texture_coordinates[0];
                out.tangent = vertex.tangent;
                out.binormal = vertex.binormal;
                out.bone_indices = {};
                out.bone_weights = {};
                if (part.skinned)
                {
                    // The submesh's palette slots to the model's bones; slots out of range stay unweighted.
                    for (std::size_t influence = 0; influence < out.bone_indices.size(); ++influence)
                    {
                        const auto palette_index = vertex.bone_indices[influence];
                        if (vertex.bone_weights[influence] <= 0.0F || palette_index >= submesh.skin_bones.size())
                        {
                            continue;
                        }
                        const auto bone_index = submesh.skin_bones[palette_index];
                        if (bone_index >= data.bones.size())
                        {
                            continue;
                        }
                        out.bone_indices[influence] = bone_index;
                        out.bone_weights[influence] = vertex.bone_weights[influence];
                    }
                }
                // Authored vertex colors only drive materials whose legacy profile reads them; other
                // files leave the channel unused, so it stays white.
                out.color = {1.0F, 1.0F, 1.0F, 1.0F};
                if (part.material.features.vertex_color)
                {
                    const Color authored{vertex.color.x, vertex.color.y, vertex.color.z, vertex.color.w};
                    if (is_finite(authored))
                    {
                        out.color = authored;
                    }
                }
                part.vertices.push_back(out);
            }
            part.indices.assign(submesh.indices.begin(), submesh.indices.end());
            loaded.submeshes.push_back(std::move(part));
        }
        model.meshes.push_back(std::move(loaded));
    }
    model.proxies.reserve(data.proxies.size());
    for (const auto& proxy : data.proxies)
    {
        model.proxies.push_back({proxy.name, proxy.bone, proxy.hidden});
    }
    return model;
}

Animation load_animation(const AlaAnimationData& animation)
{
    Animation result;
    result.frames_per_second = animation.frames_per_second;
    result.key_count = animation.stored_frame_count;
    result.loop_frame_count = ala_playable_frame_count(animation);
    result.tracks.reserve(animation.tracks.size());
    for (const auto& track : animation.tracks)
    {
        AnimationTrack loaded{track.name, track.bone_index, track.stepped, {}};
        loaded.keys.reserve(track.frames.size());
        for (const auto& frame : track.frames)
        {
            loaded.keys.push_back({frame.translation, frame.scale, frame.rotation, frame.visible});
        }
        result.tracks.push_back(std::move(loaded));
    }
    return result;
}

} // namespace eaw
