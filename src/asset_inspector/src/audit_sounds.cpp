// `sound-audit`: the sound-event catalog against the files: every event's samples resolved through
// the VFS (the sound archives, then speech) and decoded, with what is missing or cannot be decoded.

#include "asset_inspector/inspector.hpp"
#include "assets/animation_events.hpp"
#include "assets/sound_events.hpp"
#include "assets/surface_effects.hpp"
#include "core/string_utils.hpp"
#include "assets/sound_loaders.hpp"

#include <set>

namespace eaw::inspector {

namespace {

// The distinct animations the table names (its raw rows), in order.
std::vector<std::string> animation_sfx_rows(const eaw::VirtualFileSystem& vfs)
{
    std::vector<std::string> names;
    std::set<std::string> seen;
    const auto bytes = vfs.read(eaw::animation_events_path);
    const auto data = bytes ? eaw::read_animation_sfx_map(*bytes) : eaw::Result<eaw::AnimationSfxMapData>(bytes.error());
    if (!data)
    {
        return names;
    }
    for (const auto& row : data->rows)
    {
        if (row.fields.size() > 1 && seen.insert(eaw::ascii_lower(row.fields[1])).second)
        {
            names.push_back(row.fields[1]);
        }
    }
    return names;
}

} // namespace

void audit_sounds(const eaw::VirtualFileSystem& vfs, const std::string_view language, const std::size_t limit)
{
    const auto database = eaw::XmlAssetDatabase::build(vfs);
    const auto catalog = eaw::SoundEventCatalog::build(database);
    std::size_t positional = 0;
    std::size_t looping = 0;
    std::size_t without_samples = 0;
    std::set<std::string> samples;
    for (const auto& event : catalog.events())
    {
        positional += event.positional ? 1U : 0U;
        looping += event.loop ? 1U : 0U;
        without_samples += event.samples.empty() ? 1U : 0U;
        for (const auto* list : {&event.samples, &event.pre_samples, &event.post_samples})
        {
            for (const auto& sample : *list)
            {
                samples.insert(eaw::ascii_upper(sample));
            }
        }
    }
    std::map<std::string, std::size_t> formats;
    std::vector<std::string> missing;
    std::vector<std::string> undecodable;
    std::size_t resolved = 0;
    double seconds = 0.0;
    for (const auto& sample : samples)
    {
        const auto path = eaw::resolve_sound_sample(vfs, sample, language);
        if (!path)
        {
            missing.push_back(sample);
            continue;
        }
        ++resolved;
        const auto bytes = vfs.read(*path);
        if (!bytes)
        {
            undecodable.push_back(*path + ": " + bytes.error().message);
            continue;
        }
        const auto sound = eaw::decode_sound(*bytes);
        if (!sound)
        {
            undecodable.push_back(*path + ": " + sound.error().message);
            continue;
        }
        seconds += sound->seconds();
        ++formats[std::to_string(sound->channels) + " channel(s), " + std::to_string(sound->sample_rate) + " Hz"];
    }
    std::cout << "Sound events: " << catalog.size() << " (" << positional << " positional, " << looping << " looping, " << without_samples << " without samples)\n"
              << "Events naming a missing preset: " << catalog.missing_presets().size() << '\n'
              << "Distinct samples: " << samples.size() << " | resolved: " << resolved << " | missing: " << missing.size() << " | not decodable: " << undecodable.size() << '\n'
              << "Decoded audio: " << std::fixed << std::setprecision(1) << seconds / 60.0 << " minutes\n"
              << "Decoded formats:\n";
    for (const auto& [format, count] : formats)
    {
        std::cout << "  " << std::setw(6) << count << "  " << format << '\n';
    }
    const auto print = [&](const char* title, const std::vector<std::string>& lines)
    {
        if (lines.empty())
        {
            return;
        }
        std::cout << title << '\n';
        for (std::size_t index = 0; index < std::min(limit, lines.size()); ++index)
        {
            std::cout << "  " << lines[index] << '\n';
        }
    };
    print("Missing samples:", missing);
    print("Not decodable:", undecodable);
    print("Missing presets:", catalog.missing_presets());

    // Sounds placed in animations: rows that do not read, and names that lead nowhere.
    std::vector<std::string> table_warnings;
    const auto animation_events = eaw::load_animation_events(vfs, &table_warnings);
    const auto surfaces = eaw::SurfaceEffectCatalog::build(database);
    std::set<std::string> unknown_events;
    std::set<std::string> unknown_triggers;
    std::size_t sound_rows = 0;
    std::size_t surface_rows = 0;
    std::size_t surface_sounds = 0;
    for (const auto& row : animation_sfx_rows(vfs))
    {
        const auto events = animation_events.find(row);
        for (const auto& event : events)
        {
            if (event.kind == eaw::AnimationEventKind::sound)
            {
                ++sound_rows;
                if (catalog.find(event.name) == nullptr)
                {
                    unknown_events.insert(event.name);
                }
                continue;
            }
            ++surface_rows;
            const auto* settings = surfaces.find(event.name);
            if (settings == nullptr)
            {
                unknown_triggers.insert(event.name);
            }
            else if (!settings->sound.empty())
            {
                ++surface_sounds;
                if (catalog.find(settings->sound) == nullptr)
                {
                    unknown_events.insert(settings->sound);
                }
            }
        }
    }
    std::cout << "Animation events: " << animation_events.size() << " (" << sound_rows << " sounds, " << surface_rows << " surface triggers, " << surface_sounds
              << " of them with a default sound) | rows not read: " << table_warnings.size() << " | surface effects: " << surfaces.size() << "\n";
    print("Animation event rows not read:", table_warnings);
    print("Sound events named by animations but not defined:", std::vector<std::string>(unknown_events.begin(), unknown_events.end()));
    print("Surface triggers without a SurfaceFX:", std::vector<std::string>(unknown_triggers.begin(), unknown_triggers.end()));

    // Object types' sound fields (SFXEvent_*, *_SFXEvent*) naming a single event that is not
    // defined; lists such as hardpoint reports are left out, and NONE names nothing.
    std::set<std::string> undefined_fields;
    std::size_t sound_fields = 0;
    for (const auto& definition : database.definitions())
    {
        if (eaw::ascii_equal_ignoring_case(definition.type, "SFXEvent"))
        {
            continue;
        }
        for (const auto& field : definition.fields)
        {
            const auto name = eaw::ascii_lower(field.name);
            if (!name.starts_with("sfxevent_") && name.find("_sfxevent") == std::string::npos)
            {
                continue;
            }
            const auto value = eaw::ascii_trim(field.text);
            if (value.empty() || value.find_first_of(", \t") != std::string_view::npos || eaw::ascii_equal_ignoring_case(value, eaw::none_name))
            {
                continue;
            }
            // Numeric and yes/no companions (SFXEvent_Ambient_Moving_Min_Delay_Seconds,
            // Play_SFXEvent_On_Sighting) are not names.
            const bool flag = eaw::ascii_equal_ignoring_case(value, "true") || eaw::ascii_equal_ignoring_case(value, "false") || eaw::ascii_equal_ignoring_case(value, "yes") ||
                              eaw::ascii_equal_ignoring_case(value, "no");
            if (flag || value.find_first_not_of("0123456789.-") == std::string_view::npos)
            {
                continue;
            }
            ++sound_fields;
            if (catalog.find(value) == nullptr)
            {
                undefined_fields.insert(field.name + " " + std::string(value));
            }
        }
    }
    std::cout << "Object sound fields: " << sound_fields << " naming one event | undefined: " << undefined_fields.size() << "\n";
    print("Object sound fields naming undefined events:", std::vector<std::string>(undefined_fields.begin(), undefined_fields.end()));
}

} // namespace eaw::inspector
