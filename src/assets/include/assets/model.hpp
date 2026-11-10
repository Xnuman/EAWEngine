#pragma once

// A model as the engine uses it, whatever file it came from: its skeleton, meshes with interpreted
// materials, and the attachment points (proxies) effects hang on. Placing it in a scene (bind-pose
// placement of rigid meshes, hardpoint hosts, palette layout, texture registration) is the scene
// builder's job.

#include "assets/legacy_material.hpp"
#include "assets/skeleton.hpp"
#include "eaw/visual_scene.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eaw {

struct ModelSubmesh
{
    LegacyMaterial material;
    // Skinned vertices carry model-wide bone indices and weights; the others are rigid on their
    // mesh's attached bone, which the scene binds.
    bool skinned{};
    // In the mesh's space (rigid meshes: their bone's space), with colors resolved for the material.
    std::vector<VisualVertex> vertices;
    // Triangle list as stored; indices past the vertices are possible and skipped by users.
    std::vector<std::uint32_t> indices;
};

struct ModelMesh
{
    std::string name;
    std::optional<std::uint32_t> attached_bone;
    // Part of the default appearance before bone visibility: not hidden, the intact state (ALT0),
    // and the highest detail level.
    bool default_appearance{true};
    std::vector<ModelSubmesh> submeshes;
};

// A named attachment point on a bone, as effects use it.
struct ModelProxy
{
    std::string name;
    std::uint32_t bone{};
    bool hidden{};
};

struct Model
{
    Skeleton skeleton;
    std::vector<ModelMesh> meshes;
    std::vector<ModelProxy> proxies;
    // What loading reported (the decoder's warnings included).
    std::vector<std::string> warnings;
};

} // namespace eaw
