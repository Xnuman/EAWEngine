#pragma once

// Which of a model's geometry is drawn, and its bounds. One rule for scene building (the meshes it
// appends) and for what gameplay measures from a model (ground footprints and separation radii,
// until collision meshes are read), so a battle built without drawing measures exactly what a
// drawn one does.
//
// Drawn: meshes not hidden (and, where the bind pose decides bone visibility, not on a hidden
// bone), every damage state and detail level of the model itself, only the intact state at full
// detail of an attached model (a hardpoint's); submeshes with vertices and indices that are not
// collision, shadow, heat-distortion, or occluded-unit geometry.

#include "math/matrix.hpp"
#include "math/vector.hpp"
#include "assets/model.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace eaw {

// A model placed on a bone of its host (a hardpoint's Model_To_Attach), drawn as part of the host.
struct ModelAttachment
{
    std::string path;
    std::shared_ptr<const Model> model;
    std::uint32_t bone{};
};

// How a model's meshes are chosen: `bind_pose_visibility` when bone visibility is fixed to the bind
// pose (attached models, and models without animated visibility), and for attached models the
// detail level drawn (their most detailed one).
struct DrawnMeshFilter
{
    bool bind_pose_visibility{};
    bool attached{};
    std::optional<std::uint32_t> attached_detail;
};

[[nodiscard]] DrawnMeshFilter drawn_mesh_filter(const Model& source, bool attached, bool animated_visibility);
[[nodiscard]] bool drawn_mesh(const Model& source, const ModelMesh& mesh, const DrawnMeshFilter& filter) noexcept;
// A submesh with geometry of a drawn kind (heat distortion and occluded-unit overlays are skipped
// until their passes exist; collision and shadow geometry is never drawn).
[[nodiscard]] bool drawn_submesh(const ModelSubmesh& submesh) noexcept;

// Where a drawn vertex of `mesh` lies in its model's space: on its bone's bind pose when rigid.
[[nodiscard]] Vector3 drawn_vertex_position(const Vector3& position, const ModelMesh& mesh, const ModelSubmesh& submesh, std::span<const Matrix4> bind_pose);

struct ModelBounds
{
    // Empty until a vertex is added: minimum above maximum.
    Vector3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vector3 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

    [[nodiscard]] bool empty() const noexcept
    {
        return minimum.x > maximum.x;
    }
};

// Whether `model` with its `attachments` has anything to draw: a drawn submesh with at least one
// triangle whose corners are among its vertices. A placed object exists in a battle only then
// (decorative particle props and geometry-free models do not).
[[nodiscard]] bool has_drawn_geometry(const Model& model, std::span<const ModelAttachment> attachments, bool animated_visibility);

// The bounds of every drawn vertex of `model` and its `attachments` (each placed at its host
// bone's bind pose), in the model's space.
[[nodiscard]] ModelBounds drawn_model_bounds(const Model& model, std::span<const ModelAttachment> attachments, bool animated_visibility);
// The bounds of a model's collision meshes (LegacyMaterialFamily::collision, hidden or not) in its bind
// pose; empty without any.
[[nodiscard]] ModelBounds collision_model_bounds(const Model& model);

// Grows `bounds` by the eight corners of `local` placed with `transform` (row-vector), skipping
// corners that are not finite (malformed vertex data must not make a frame non-finite).
void add_placed_bounds(ModelBounds& bounds, const ModelBounds& local, const Matrix4& transform) noexcept;

// Billboard grass (the Grass material): the stock files leave their second texture-coordinate set
// empty, so the clump centers the billboards turn about are computed here, as the original engine
// must have done ("x, y center of each billboard polygon", the legacy shader's comment): each
// connected piece of the submesh (a billboard: two triangles over four vertices in the stock grass)
// gets the mean x, y of its vertices in `texture_coordinates_1`. Indices out of range are ignored.
void assign_billboard_centers(std::span<VisualVertex> vertices, std::span<const std::uint32_t> indices);

} // namespace eaw
