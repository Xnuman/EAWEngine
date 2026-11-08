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

// Splits a list of XML type names (Squadron_Units, Company_Units) on commas and whitespace. Stock
// FoC data writes one Company_Units list without commas; type names never contain whitespace.
[[nodiscard]] std::vector<std::string_view> split_type_names(std::string_view text);

struct MapObjectPlacement
{
    std::size_t object_index{};
    const XmlDefinition* type{};
    std::string model_reference;
    // Winning virtual path of the model, or empty when the type has no model for the map's
    // context or the referenced file is missing from every layer.
    std::string model_path;
    AloMatrix4 transform{};
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
[[nodiscard]] MapObjectLayout build_map_object_layout(const TedMap& map, const GameObjectCatalog& catalog, const AssetReader& vfs);

} // namespace eaw
