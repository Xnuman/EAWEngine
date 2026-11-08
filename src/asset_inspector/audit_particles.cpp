#include "inspector.hpp"

#include <bit>

namespace eaw::inspector {
namespace {

template <typename Key> void print_counts(const std::string_view title, const std::map<Key, std::size_t>& counts)
{
    std::cout << '\n' << title << ":\n";
    for (const auto& [key, count] : counts)
    {
        if constexpr (std::is_integral_v<Key>)
        {
            std::cout << "  " << static_cast<long long>(key) << ": " << count << '\n';
        }
        else
        {
            std::cout << "  " << key << ": " << count << '\n';
        }
    }
}

std::string hex_byte(const std::uint32_t value)
{
    std::ostringstream text;
    text << "0x" << std::hex << std::setw(2) << std::setfill('0') << value;
    return text.str();
}

// A warning's kind: its text after the "emitter N: " prefix, with numbers removed.
std::string warning_kind(const std::string& warning)
{
    auto text = warning;
    if (const auto colon = text.find(": "); colon != std::string::npos && text.starts_with("emitter "))
    {
        text = text.substr(colon + 2);
    }
    std::string kind;
    for (const auto character : text)
    {
        if (std::isdigit(static_cast<unsigned char>(character)) == 0)
        {
            kind.push_back(character);
        }
        else if (kind.empty() || kind.back() != '#')
        {
            kind.push_back('#');
        }
    }
    return kind;
}

} // namespace

void audit_particles(const eaw::VirtualFileSystem& vfs, const std::string_view prefix, const std::size_t limit)
{
    std::size_t alo_files = 0;
    std::size_t plugin_files = 0;
    std::size_t decoded_files = 0;
    std::size_t emitters = 0;
    std::map<std::string, AuditExample> failures;
    std::map<std::string, AuditExample> warnings;
    std::map<std::string, std::size_t> property_ids;
    std::map<std::uint32_t, std::size_t> blend_modes;
    std::map<std::string, std::size_t> renderers;
    std::map<std::string, std::size_t> spawning;
    std::map<std::uint32_t, std::size_t> group_types;
    std::map<std::string, std::size_t> interpolations;
    std::map<std::uint32_t, std::size_t> texture_cells;
    std::map<std::uint32_t, std::size_t> ground_behaviors;
    std::map<std::uint32_t, std::size_t> mesh_emission;
    std::map<std::string, std::size_t> links;
    std::map<std::string, std::size_t> header_flags;
    std::map<std::size_t, std::size_t> emitters_per_system;
    std::map<std::size_t, std::size_t> keys_per_track;
    std::set<std::string> textures;
    std::map<std::uint8_t, std::pair<float, float>> float_ranges;
    std::map<std::uint8_t, std::size_t> nonzero;
    std::size_t leave_particles = 0;
    // The largest steady particle count one emitter can reach (rate times lifetime, or a burst's count
    // times the bursts alive at once).
    double busiest_emitter = 0.0;
    std::string busiest_name;

    for (const auto& asset : vfs.list(prefix))
    {
        if (extension_of(asset.virtual_path) != ".alo")
        {
            continue;
        }
        ++alo_files;
        const auto bytes = vfs.read(asset.virtual_path);
        if (!bytes)
        {
            record_example(failures, "unreadable: " + bytes.error().message, asset.virtual_path);
            continue;
        }
        const auto kind = eaw::particle_file_kind(*bytes);
        if (kind == eaw::ParticleFileKind::other)
        {
            continue;
        }
        if (kind == eaw::ParticleFileKind::plugin)
        {
            ++plugin_files;
            continue;
        }
        const auto system = eaw::ParticleSystem::parse(*bytes);
        if (!system)
        {
            record_example(failures, system.error().message, asset.virtual_path);
            continue;
        }
        ++decoded_files;
        leave_particles += system->leave_particles ? 1U : 0U;
        ++emitters_per_system[system->emitters.size()];
        std::string flags;
        for (const auto byte : system->header_flags)
        {
            flags += hex_byte(std::to_integer<std::uint32_t>(byte)) + ' ';
        }
        ++header_flags[flags.empty() ? "(absent)" : flags];
        for (const auto& warning : system->warnings)
        {
            record_example(warnings, warning_kind(warning), asset.virtual_path, warning);
        }
        for (const auto& emitter : system->emitters)
        {
            ++emitters;
            for (const auto& property : emitter.properties)
            {
                ++property_ids[hex_byte(property.id) + " (" + std::to_string(property.payload.size()) + " bytes)"];
            }
            ++blend_modes[emitter.blend_mode];
            for (const auto& property : emitter.properties)
            {
                if (property.payload.size() == 4)
                {
                    std::uint32_t raw = 0;
                    for (std::size_t b = 0; b < 4; ++b)
                    {
                        raw |= std::to_integer<std::uint32_t>(property.payload[b]) << (b * 8U);
                    }
                    const auto value = std::bit_cast<float>(raw);
                    auto& range = float_ranges.try_emplace(property.id, value, value).first->second;
                    range.first = std::min(range.first, value);
                    range.second = std::max(range.second, value);
                    if (value != 0.0F)
                    {
                        ++nonzero[property.id];
                    }
                }
            }
            std::string renderer = emitter.has_tail ? "kite" : (emitter.world_oriented ? "xy-aligned" : "billboard");
            if (emitter.heat)
            {
                renderer += "+heat";
            }
            if (emitter.weather)
            {
                renderer += "+weather";
            }
            if (emitter.no_depth_test)
            {
                renderer += "+no-depth-test";
            }
            ++renderers[renderer];
            ++spawning[emitter.use_bursts ? (emitter.burst_count <= 0 ? "bursts, unlimited" : "bursts, limited") : "continuous"];
            for (const auto& group : emitter.groups)
            {
                ++group_types[group.type];
            }
            for (const auto& track : emitter.tracks)
            {
                ++interpolations[track.present ? std::to_string(track.raw_interpolation) : "(absent)"];
                ++keys_per_track[track.keys.size()];
            }
            ++texture_cells[emitter.texture_cells];
            ++ground_behaviors[emitter.ground_behavior];
            ++mesh_emission[emitter.emit_from_mesh];
            if (emitter.death_child >= 0)
            {
                ++links["spawn on death"];
            }
            if (emitter.life_child >= 0)
            {
                ++links["spawn during life"];
            }
            if (!emitter.normal_texture.empty())
            {
                ++links["normal texture"];
            }
            textures.insert(lower_ascii(emitter.texture));
            const auto lifetime = static_cast<double>(emitter.lifetime) * (1.0 + static_cast<double>(std::max(emitter.random_lifetime, 0.0F)) / 100.0);
            const auto alive = emitter.use_bursts ? static_cast<double>(emitter.particles_per_burst) * std::max(1.0, lifetime / std::max(static_cast<double>(emitter.burst_delay), 0.001))
                                                  : static_cast<double>(emitter.particles_per_second) * lifetime;
            if (alive > busiest_emitter)
            {
                busiest_emitter = alive;
                busiest_name = asset.virtual_path + " / " + emitter.name;
            }
        }
    }

    std::cout << "ALO files: " << alo_files << '\n'
              << "Classic particle systems decoded: " << decoded_files << '\n'
              << "Plugin-format particle systems (not supported): " << plugin_files << '\n'
              << "Particle systems that failed: " <<
        [&]
    {
        std::size_t count = 0;
        for (const auto& [message, example] : failures)
        {
            count += example.count;
        }
        return count;
    }() << '\n'
              << "Emitters: " << emitters << '\n'
              << "Systems that leave particles: " << leave_particles << '\n'
              << "Distinct emitter textures: " << textures.size() << '\n'
              << "Busiest emitter (particles alive, estimate): " << busiest_emitter << " (" << busiest_name << ")\n";

    std::cout << "\nFailures:\n";
    std::size_t shown = 0;
    for (const auto& [message, example] : by_count(failures))
    {
        if (shown++ >= limit)
        {
            break;
        }
        std::cout << "  " << example.count << "x " << message << " (e.g. " << example.map << ")\n";
    }
    std::cout << "\nWarnings by kind:\n";
    shown = 0;
    for (const auto& [kind, example] : by_count(warnings))
    {
        if (shown++ >= limit)
        {
            break;
        }
        std::cout << "  " << example.count << "x " << kind << " (e.g. " << example.map << ": " << example.detail << ")\n";
    }
    print_counts("Emitters per system", emitters_per_system);
    print_counts("System chunk 0x1 bytes", header_flags);
    print_counts("Property mini-chunks (id, size)", property_ids);
    print_counts("Blend modes", blend_modes);
    print_counts("Renderers", renderers);
    print_counts("Spawning", spawning);
    print_counts("Shape group types", group_types);
    print_counts("Track interpolation", interpolations);
    print_counts("Keys per track", keys_per_track);
    print_counts("Texture cells", texture_cells);
    print_counts("Ground behavior", ground_behaviors);
    print_counts("Emit from mesh", mesh_emission);
    print_counts("Links", links);
    std::cout << "\nFour-byte properties read as floats (minimum, maximum, nonzero):\n";
    for (const auto& [id, range] : float_ranges)
    {
        std::cout << "  " << hex_byte(id) << ": " << range.first << " .. " << range.second << " (" << nonzero[id] << ")\n";
    }
}

} // namespace eaw::inspector
