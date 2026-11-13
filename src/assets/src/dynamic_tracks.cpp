#include "assets/dynamic_tracks.hpp"

#include "core/string_utils.hpp"
#include "assets/type_fields.hpp"

#include <cmath>

namespace eaw {

DynamicTrackCatalog DynamicTrackCatalog::build(const XmlAssetDatabase& database, std::vector<std::string>* warnings)
{
    DynamicTrackCatalog catalog;
    for (const auto& definition : database.definitions())
    {
        if (!ascii_equal_ignoring_case(definition.type, "DynamicTrack") || catalog.tracks_.contains(ascii_lower(definition.name)))
        {
            continue;
        }
        DynamicTrack track;
        track.name = definition.name;
        const auto text = [&](const std::string_view name)
        {
            const auto* field = definition_field(definition, name);
            return field != nullptr ? ascii_trim(field->text) : std::string_view{};
        };
        // Numbers, some written with a C-style suffix ("20.0f").
        const auto number = [&](const std::string_view name, const float fallback)
        {
            auto value = text(name);
            if (value.empty())
            {
                return fallback;
            }
            if (value.back() == 'f' || value.back() == 'F')
            {
                value.remove_suffix(1);
            }
            const auto parsed = parse_xml_number(value);
            if (!parsed || !std::isfinite(*parsed) || *parsed < 0.0F)
            {
                if (warnings != nullptr)
                {
                    warnings->push_back("dynamic track '" + track.name + "': " + std::string(name) + " '" + std::string(text(name)) + "' is not usable");
                }
                return fallback;
            }
            return *parsed;
        };
        track.texture = std::string(text("texture_name"));
        const auto mode = text("Render_Mode");
        track.mode = ascii_equal_ignoring_case(mode, "DECAL_BUMP_ALPHA") ? TerrainDecalMode::bump_alpha
                     : ascii_equal_ignoring_case(mode, "ADDITIVE")       ? TerrainDecalMode::additive
                                                                         : TerrainDecalMode::diffuse_alpha;
        track.width = number("width", 0.0F);
        track.opacity = std::min(number("opacity", 1.0F), 1.0F);
        track.segment_length = number("segment_length", 0.0F);
        track.fade_begin = number("fade_begin_distance", 0.0F);
        track.fade_end = std::max(number("fade_end_distance", 0.0F), track.fade_begin);
        track.fade_per_second = number("fade_distance_per_second", 0.0F);
        if (track.texture.empty() || !(track.width > 0.0F) || !(track.segment_length > 0.0F))
        {
            if (warnings != nullptr)
            {
                warnings->push_back("dynamic track '" + track.name + "' needs a texture, a width, and a segment length; it is left out");
            }
            continue;
        }
        catalog.tracks_.emplace(ascii_lower(definition.name), std::move(track));
    }
    return catalog;
}

const DynamicTrack* DynamicTrackCatalog::find(const std::string_view name) const
{
    const auto found = tracks_.find(ascii_lower(ascii_trim(name)));
    return found != tracks_.end() ? &found->second : nullptr;
}

std::size_t DynamicTrackCatalog::size() const noexcept
{
    return tracks_.size();
}

} // namespace eaw
