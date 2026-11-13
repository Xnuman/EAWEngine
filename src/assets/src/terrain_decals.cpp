#include "assets/terrain_decals.hpp"

#include "core/string_utils.hpp"
#include "assets/game_object_catalog.hpp"
#include "assets/type_fields.hpp"

#include <cmath>
#include <numbers>

namespace eaw {
namespace {

// Slot_Scale values above this are taken for typing errors (the atlas has 4 cells per side).
constexpr float maximum_cell_span = 4.0F;

} // namespace

TerrainDecalCatalog TerrainDecalCatalog::build(const XmlAssetDatabase& database, std::vector<std::string>* warnings)
{
    TerrainDecalCatalog catalog;
    const auto warn = [&](const TerrainDecal& decal, const std::string_view field, const std::string_view value)
    {
        if (warnings != nullptr)
        {
            warnings->push_back("decal '" + decal.name + "': " + std::string(field) + " '" + std::string(value) + "' is not usable");
        }
    };
    for (const auto& definition : database.definitions())
    {
        if (!ascii_equal_ignoring_case(definition.type, "Decal"))
        {
            continue;
        }
        // The first definition of a name wins, as for object types.
        const auto [entry, added] = catalog.decals_.try_emplace(ascii_lower(definition.name));
        if (!added)
        {
            continue;
        }
        auto& decal = entry->second;
        decal.name = definition.name;
        const auto text = [&](const std::string_view name)
        {
            const auto* child = definition_field(definition, name);
            return child != nullptr ? ascii_trim(child->text) : std::string_view{};
        };
        const auto number = [&](const std::string_view name, const float fallback)
        {
            const auto value = text(name);
            if (value.empty())
            {
                return fallback;
            }
            const auto parsed = parse_xml_number(value);
            if (!parsed || !std::isfinite(*parsed))
            {
                warn(decal, name, value);
                return fallback;
            }
            return *parsed;
        };
        const auto flag = [&](const std::string_view name)
        {
            const auto value = text(name);
            const auto parsed = value.empty() ? std::optional<bool>(false) : parse_xml_bool(value);
            if (!parsed)
            {
                warn(decal, name, value);
            }
            return parsed.value_or(false);
        };

        const auto mode = text("Render_Mode");
        if (ascii_equal_ignoring_case(mode, "DECAL_BUMP_ALPHA"))
        {
            decal.mode = TerrainDecalMode::bump_alpha;
        }
        else if (ascii_equal_ignoring_case(mode, "ADDITIVE"))
        {
            decal.mode = TerrainDecalMode::additive;
        }
        else if (!mode.empty() && !ascii_equal_ignoring_case(mode, "DIFFUSE_ALPHA"))
        {
            warn(decal, "Render_Mode", mode);
        }
        decal.intensity = std::clamp(number("Intensity", 1.0F), 0.0F, 1.0F);
        const auto scale = split_list(text("Scale"));
        if (scale.size() >= 2)
        {
            const auto width = parse_xml_number(scale[0]);
            const auto height = parse_xml_number(scale[1]);
            if (width && height && *width > 0.0F && *height > 0.0F && std::isfinite(*width) && std::isfinite(*height))
            {
                decal.width = *width;
                decal.height = *height;
            }
            else
            {
                warn(decal, "Scale", text("Scale"));
            }
        }
        decal.angle = number("Z_Angle", 0.0F) * std::numbers::pi_v<float> / 180.0F;
        decal.random_rotate = flag("Random_Rotate");
        decal.size_variation = std::clamp(number("Random_Size_Offset", 0.0F), 0.0F, 1.0F);
        decal.fade_seconds = std::max(number("Fadeout_Time", 0.0F), 0.0F);
        decal.permanent = flag("Permanent");
        const auto span = number("Slot_Scale", 1.0F);
        decal.cell_span = span >= 1.0F && span <= maximum_cell_span ? static_cast<std::uint32_t>(span) : 1U;
        // UV_Slot: column, row pairs; the cells and their span must lie inside the atlas.
        const auto slots = split_list(text("UV_Slot"));
        for (std::size_t index = 0; index + 1 < slots.size(); index += 2)
        {
            const auto column = parse_xml_number(slots[index]);
            const auto row = parse_xml_number(slots[index + 1]);
            if (column && row && *column >= 0.0F && *row >= 0.0F && *column + static_cast<float>(decal.cell_span) <= static_cast<float>(decal_atlas_cells) &&
                *row + static_cast<float>(decal.cell_span) <= static_cast<float>(decal_atlas_cells))
            {
                decal.cells.push_back({static_cast<std::uint32_t>(*column), static_cast<std::uint32_t>(*row)});
            }
            else
            {
                warn(decal, "UV_Slot", std::string(slots[index]) + "," + std::string(slots[index + 1]));
            }
        }
    }
    return catalog;
}

const TerrainDecal* TerrainDecalCatalog::find(const std::string_view name) const
{
    const auto found = decals_.find(ascii_lower(ascii_trim(name)));
    return found != decals_.end() ? &found->second : nullptr;
}

std::size_t TerrainDecalCatalog::size() const noexcept
{
    return decals_.size();
}

} // namespace eaw
