#pragma once

// Surface effects (XML SurfaceFX): what a trigger (a foot planting, a track, an impact) leaves on
// the ground, by the kind of surface there (Surface_Settings: default, dirt, grass, snow, water,
// ...): a sound event, a particle, a decal, a dynamic track. Triggers name a SurfaceFX directly
// (ENUM/SURFACEFXTRIGGERTYPE.XML lists them), ignoring case.

#include "assets/xml_asset_database.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eaw {

struct SurfaceEffectSettings
{
    // The surface kind, lower case.
    std::string surface;
    std::string sound;
    std::string particle;
    std::string decal;
    // The trail it lays while its unit moves (Dynamic_Track_Name) and the bones the two treads run
    // under (Dynamic_Track_Left_Bone, _Right_Bone; none: one trail under the unit).
    std::string track;
    std::string track_left_bone;
    std::string track_right_bone;
};

class SurfaceEffectCatalog final
{
public:
    [[nodiscard]] static SurfaceEffectCatalog build(const XmlAssetDatabase& database);

    // A trigger's settings for a surface, else its "default" settings; null when the trigger has
    // neither.
    [[nodiscard]] const SurfaceEffectSettings* find(std::string_view trigger, std::string_view surface = "default") const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    // Lower-case SurfaceFX name -> its settings.
    std::unordered_map<std::string, std::vector<SurfaceEffectSettings>> effects_;
};

} // namespace eaw
