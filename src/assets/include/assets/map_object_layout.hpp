#pragma once

#include "formats/alo_model.hpp"
#include "formats/ted_map.hpp"
#include "assets/game_object_catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eaw {

class AssetReader;

// Object-to-world transform for a placed TED object in the row-vector convention used by
// transform_point. The Z component is the object's facing (degrees counter-clockwise from +X,
// as the simulation's headings); models face -Y, so the model first turns a quarter right
// (placed_model_facing) to face +X. The rotation components then apply about X, then Y, then Z;
// stock maps store almost all rotation in Z. Uniform scale comes from the object's scale field.
// The facing offset is measured (pieces laid end to end follow it; see
// docs/compatibility-research.md); the X/Y order stays inference.
[[nodiscard]] Matrix4 placed_object_transform(const TedPlacedObject& object) noexcept;

// The model's turn before the placement rotation: from facing -Y to facing +X (degrees).
inline constexpr float placed_model_facing = 90.0F;

struct MapObjectPlacement
{
    std::size_t object_index{};
    const XmlDefinition* type{};
    std::string model_reference;
    // Winning virtual path of the model, or empty when the type has no model for the map's
    // context or the referenced file is missing from every layer.
    std::string model_path;
    Matrix4 transform{Matrix4::identity()};
    // Set for members expanded from a space squadron or a land company: the container type and
    // the member's index. Several placements then share one object_index.
    const XmlDefinition* container{};
    std::uint32_t member{};
    // A land company member: the data gives no member offsets (the original arranges companies by
    // runtime formation behavior), so the transform is the company's and the consumer arranges
    // the members.
    bool needs_formation{};
};

struct MapObjectLayout
{
    GameModelContext context{GameModelContext::space};
    std::vector<MapObjectPlacement> placements;
    std::size_t untyped{};
    std::size_t without_model{};
    std::size_t missing_model_files{};
    std::size_t expanded_members{};
    // Members expanded from land companies (Company_Units), placed at their company's origin.
    std::size_t company_members{};
    // Placed containers with no member list (Container types without Company_Units).
    std::size_t unexpanded_containers{};
    std::vector<std::string> diagnostics;
};

// Resolves every positioned placed object of a map to its XML type, model, and world transform.
// Objects without a decoded transform are skipped. The catalog (and its database) must outlive
// the returned layout because placements point at catalog definitions.
[[nodiscard]] MapObjectLayout build_map_object_layout(const TedMapData& map, const GameObjectCatalog& catalog, const AssetReader& vfs);

} // namespace eaw
