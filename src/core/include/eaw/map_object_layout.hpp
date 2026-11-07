#pragma once

#include "eaw/alo_model.hpp"
#include "eaw/game_object_catalog.hpp"
#include "eaw/ted_map.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eaw {

class AssetReader;

// Object-to-world transform for a placed TED object in the row-vector convention used by
// alo_transform_point. Rotation components are degrees applied about X, then Y, then Z; stock
// maps store almost all rotation in the Z (heading) component. Uniform scale comes from the
// object's scale field. The rotation order and handedness are inference pending black-box
// comparison; see docs/compatibility-research.md.
[[nodiscard]] AloMatrix4 placed_object_transform(const TedPlacedObject& object) noexcept;

struct MapObjectPlacement
{
    std::size_t object_index{};
    const XmlDefinition* type{};
    std::string model_reference;
    // Winning virtual path of the model, or empty when the type has no model for the map's
    // context or the referenced file is missing from every layer.
    std::string model_path;
    AloMatrix4 transform{};
    // Set for members expanded from a space squadron: the squadron type and the member's index.
    // Several placements then share one object_index.
    const XmlDefinition* container{};
    std::uint32_t member{};
};

struct MapObjectLayout
{
    GameModelContext context{GameModelContext::space};
    std::vector<MapObjectPlacement> placements;
    std::size_t untyped{};
    std::size_t without_model{};
    std::size_t missing_model_files{};
    std::size_t expanded_members{};
    // Land teams and companies whose members are arranged by runtime formation behavior.
    std::size_t unexpanded_containers{};
    std::vector<std::string> diagnostics;
};

// Resolves every positioned placed object of a map to its XML type, model, and world transform.
// Objects without a decoded transform are skipped. The catalog (and its database) must outlive
// the returned layout because placements point at catalog definitions.
[[nodiscard]] MapObjectLayout build_map_object_layout(const TedMap& map, const GameObjectCatalog& catalog, const AssetReader& vfs);

} // namespace eaw
