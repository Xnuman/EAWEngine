#pragma once

// A model as the engine uses it, whatever file it came from: its skeleton, meshes with interpreted
// materials, and the attachment points (proxies) effects hang on. Placing it in a scene (bind-pose
// placement of rigid meshes, hardpoint hosts, palette layout, texture registration) is the scene
// builder's job.

#include "render/visual_material.hpp"
#include "render/visual_vertex.hpp"
#include "assets/legacy_material.hpp"
#include "assets/skeleton.hpp"

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
    // Hidden in the file (never drawn).
    bool hidden{};
    // The damage state the mesh belongs to (legacy _ALT<n>; none: every state) and its detail level
    // (legacy _LOD<n>, a higher number more detailed; none: every level).
    std::optional<std::uint32_t> damage_state;
    std::optional<std::uint32_t> detail_level;
    std::vector<ModelSubmesh> submeshes;
    // How the mesh turns as its bone's billboard mode says (none: fixed).
    VisualBillboard billboard{VisualBillboard::none};
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
