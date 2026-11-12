#include "assets/sound_events.hpp"

#include "core/string_utils.hpp"
#include "assets/game_object_catalog.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace eaw {
namespace {

// XML percent to a factor.
constexpr float percent = 0.01F;
// XML milliseconds to seconds.
constexpr float milliseconds = 0.001F;

const XmlNode* field_of(const XmlDefinition& definition, const std::string_view name)
{
    const auto found = std::ranges::find_if(definition.fields,
                                            [&](const XmlNode& field)
                                            {
                                                return ascii_equal_ignoring_case(field.name, name);
                                            });
    return found == definition.fields.end() ? nullptr : &*found;
}

std::optional<float> number_of(const XmlNode* field)
{
    if (field == nullptr)
    {
        return std::nullopt;
    }
    const auto text = ascii_trim(field->text);
    float value = 0.0F;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || !std::isfinite(value))
    {
        return std::nullopt;
    }
    return value;
}

std::optional<bool> flag_of(const XmlNode* field)
{
    return field != nullptr ? parse_xml_bool(field->text) : std::nullopt;
}

// Names separated by whitespace or commas.
std::vector<std::string> names_of(const XmlNode* field)
{
    std::vector<std::string> names;
    if (field == nullptr)
    {
        return names;
    }
    for (const auto name : split_list(field->text))
    {
        names.emplace_back(name);
    }
    return names;
}

// Applies a definition's fields over `event` (a preset's first, then the event's own).
void apply(SoundEvent& event, const XmlDefinition& definition)
{
    const auto set_names = [&](std::vector<std::string>& target, const std::string_view field)
    {
        if (const auto* node = field_of(definition, field))
        {
            target = names_of(node);
        }
    };
    set_names(event.samples, "Samples");
    set_names(event.pre_samples, "Pre_Samples");
    set_names(event.post_samples, "Post_Samples");
    if (const auto is_3d = flag_of(field_of(definition, "Is_3D")))
    {
        event.positional = *is_3d;
    }
    else if (const auto is_2d = flag_of(field_of(definition, "Is_2D")))
    {
        event.positional = !*is_2d;
    }
    const auto set_number = [&](float& target, const std::string_view field, const float scale)
    {
        if (const auto value = number_of(field_of(definition, field)))
        {
            target = *value * scale;
        }
    };
    set_number(event.min_volume, "Min_Volume", percent);
    set_number(event.max_volume, "Max_Volume", percent);
    set_number(event.min_pitch, "Min_Pitch", percent);
    set_number(event.max_pitch, "Max_Pitch", percent);
    // Probability may list one chance per sample ("80, 20"); the first is the event's chance.
    if (const auto* node = field_of(definition, "Probability"); node != nullptr)
    {
        const auto chances = names_of(node);
        float chance = 0.0F;
        if (!chances.empty() && std::from_chars(chances.front().data(), chances.front().data() + chances.front().size(), chance).ec == std::errc{})
        {
            event.probability = chance * percent;
        }
    }
    set_number(event.loop_fade_in, "Loop_Fade_In_Seconds", 1.0F);
    set_number(event.loop_fade_out, "Loop_Fade_Out_Seconds", 1.0F);
    set_number(event.min_predelay, "Min_Predelay", milliseconds);
    set_number(event.max_predelay, "Max_Predelay", milliseconds);
    set_number(event.saturation_distance, "Volume_Saturation_Distance", 1.0F);
    if (const auto value = number_of(field_of(definition, "Play_Count")))
    {
        event.loop = *value < 0.0F;
    }
    if (const auto value = number_of(field_of(definition, "Priority")); value && *value >= 0.0F)
    {
        event.priority = static_cast<std::uint32_t>(*value);
    }
    if (const auto value = number_of(field_of(definition, "Max_Instances")); value && *value >= 0.0F)
    {
        event.max_instances = static_cast<std::uint32_t>(*value);
    }
    const auto set_flag = [&](bool& target, const std::string_view field)
    {
        if (const auto value = flag_of(field_of(definition, field)))
        {
            target = *value;
        }
    };
    set_flag(event.sequential, "Play_Sequentially");
    set_flag(event.localized, "Localize");
    set_flag(event.unit_response, "Is_Unit_Response_VO");
    set_flag(event.kills_previous_object_sound, "Kills_Previous_Object_SFX");
    if (const auto* node = field_of(definition, "Overlap_Test"))
    {
        event.overlap_test = std::string(ascii_trim(node->text));
    }
    if (const auto* node = field_of(definition, "Chained_SFXEvent"))
    {
        event.chained_event = std::string(ascii_trim(node->text));
    }
}

} // namespace

SoundEventCatalog SoundEventCatalog::build(const XmlAssetDatabase& database)
{
    SoundEventCatalog catalog;
    std::unordered_map<std::string, const XmlDefinition*> presets;
    std::vector<const XmlDefinition*> definitions;
    for (const auto& definition : database.definitions())
    {
        if (!ascii_equal_ignoring_case(definition.type, "SFXEvent") || definition.name.empty())
        {
            continue;
        }
        if (flag_of(field_of(definition, "Is_Preset")).value_or(false))
        {
            presets.try_emplace(ascii_lower(definition.name), &definition);
            continue;
        }
        definitions.push_back(&definition);
    }
    for (const auto* definition : definitions)
    {
        auto key = ascii_lower(definition->name);
        if (catalog.index_.contains(key))
        {
            continue;
        }
        SoundEvent event;
        event.name = definition->name;
        if (const auto* preset_field = field_of(*definition, "Use_Preset"))
        {
            const auto preset_name = ascii_lower(ascii_trim(preset_field->text));
            if (const auto preset = presets.find(preset_name); preset != presets.end())
            {
                apply(event, *preset->second);
            }
            else if (!preset_name.empty())
            {
                catalog.missing_presets_.push_back(definition->name + " (" + std::string(ascii_trim(preset_field->text)) + ")");
            }
        }
        apply(event, *definition);
        catalog.index_.emplace(std::move(key), catalog.events_.size());
        catalog.events_.push_back(std::move(event));
    }
    return catalog;
}

const SoundEvent* SoundEventCatalog::find(const std::string_view name) const
{
    const auto found = index_.find(ascii_lower(ascii_trim(name)));
    return found == index_.end() ? nullptr : &events_[found->second];
}

std::size_t SoundEventCatalog::size() const noexcept
{
    return events_.size();
}

const std::vector<SoundEvent>& SoundEventCatalog::events() const noexcept
{
    return events_;
}

const std::vector<std::string>& SoundEventCatalog::missing_presets() const noexcept
{
    return missing_presets_;
}

std::optional<std::string> resolve_sound_sample(const AssetReader& reader, const std::string_view sample, const std::string_view language)
{
    const auto file = std::string(ascii_trim(sample));
    if (file.empty())
    {
        return std::nullopt;
    }
    const auto dot = file.find_last_of('.');
    const auto as_mp3 = (dot == std::string::npos ? file : file.substr(0, dot)) + ".MP3";
    for (const auto& folder : {std::string("DATA/AUDIO/SFX/"), "DATA/AUDIO/SPEECH/" + ascii_upper(language) + "/"})
    {
        for (const auto& name : {file, as_mp3})
        {
            if (auto resolved = reader.resolve(folder + name))
            {
                return std::move(resolved->virtual_path);
            }
        }
    }
    return std::nullopt;
}

} // namespace eaw
