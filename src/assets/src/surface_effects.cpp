#include "assets/surface_effects.hpp"

#include "core/string_utils.hpp"
#include "formats/xml_helpers.hpp"
#include "assets/animation_events.hpp"

#include <algorithm>

namespace eaw {
namespace {

std::string child_text(const XmlNode& node, const std::string_view name)
{
    // NONE names nothing (the data's placeholder).
    const auto* child = xml_child(node, name);
    const auto text = child != nullptr ? ascii_trim(child->text) : std::string_view{};
    return ascii_equal_ignoring_case(text, none_name) ? std::string{} : std::string(text);
}

} // namespace

SurfaceEffectCatalog SurfaceEffectCatalog::build(const XmlAssetDatabase& database)
{
    SurfaceEffectCatalog catalog;
    for (const auto& definition : database.definitions())
    {
        if (!ascii_equal_ignoring_case(definition.type, "SurfaceFX"))
        {
            continue;
        }
        // The first definition of a name wins, as for object types.
        const auto [entry, added] = catalog.effects_.try_emplace(ascii_lower(definition.name));
        if (!added)
        {
            continue;
        }
        for (const auto& field : definition.fields)
        {
            const auto* surface = xml_attribute(field, "name");
            if (!ascii_equal_ignoring_case(field.name, "Surface_Settings") || surface == nullptr)
            {
                continue;
            }
            entry->second.push_back({ascii_lower(ascii_trim(surface->value)), child_text(field, "SoundFX_Name"), child_text(field, "Particle_Name"), child_text(field, "Decal_Name"),
                                     child_text(field, "Dynamic_Track_Name"), child_text(field, "Dynamic_Track_Left_Bone"), child_text(field, "Dynamic_Track_Right_Bone")});
        }
    }
    return catalog;
}

const SurfaceEffectSettings* SurfaceEffectCatalog::find(const std::string_view trigger, const std::string_view surface) const
{
    const auto found = effects_.find(ascii_lower(ascii_trim(trigger)));
    if (found == effects_.end())
    {
        return nullptr;
    }
    const auto& settings = found->second;
    const auto with = [&](const std::string_view name)
    {
        return std::ranges::find_if(settings,
                                    [&](const SurfaceEffectSettings& candidate)
                                    {
                                        return ascii_equal_ignoring_case(candidate.surface, name);
                                    });
    };
    auto chosen = with(surface);
    if (chosen == settings.end())
    {
        chosen = with("default");
    }
    return chosen != settings.end() ? &*chosen : nullptr;
}

std::size_t SurfaceEffectCatalog::size() const noexcept
{
    return effects_.size();
}

} // namespace eaw
