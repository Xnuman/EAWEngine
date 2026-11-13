#pragma once

// Dynamic tracks (XML DynamicTrackFX.xml, `DynamicTrack`): the trails a moving unit leaves on the
// ground, tread marks behind tanks and footprint trails behind infantry, named by surface effects
// (SurfaceFX Dynamic_Track_Name) that a unit's SurfaceFX_Name applies while it moves. A trail is a
// ribbon of segments laid every `segment_length` along the path (at a tread bone, or the unit),
// `width` wide, showing its own texture (bump or diffuse, as decals are), and fading along its
// length from `fade_begin` to `fade_end` world units behind its head, that distance growing by
// `fade_per_second` as time passes (an inference from the names and values).

#include "assets/terrain_decals.hpp"
#include "assets/xml_asset_database.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eaw {

struct DynamicTrack
{
    std::string name;
    std::string texture;
    TerrainDecalMode mode{TerrainDecalMode::diffuse_alpha};
    float width{};
    float opacity{1.0F};
    float segment_length{};
    float fade_begin{};
    float fade_end{};
    float fade_per_second{};
};

class DynamicTrackCatalog final
{
public:
    // Malformed fields are reported to `warnings` when given; a track without a texture, a width,
    // or a segment length is reported and left out.
    [[nodiscard]] static DynamicTrackCatalog build(const XmlAssetDatabase& database, std::vector<std::string>* warnings = nullptr);

    // A track by name, ignoring case; null when unknown.
    [[nodiscard]] const DynamicTrack* find(std::string_view name) const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::unordered_map<std::string, DynamicTrack> tracks_;
};

} // namespace eaw
