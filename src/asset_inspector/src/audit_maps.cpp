// `map-objects` and `map-audit`: placed objects and map decoding across the stock maps.

#include "asset_inspector/inspector.hpp"

namespace eaw::inspector {

void audit_map_objects(const eaw::VirtualFileSystem& vfs, const std::string_view prefix, const std::size_t limit)
{
    const auto database = eaw::XmlAssetDatabase::build(vfs);
    const auto catalog = eaw::GameObjectCatalog::build(database);

    std::size_t maps = 0;
    std::size_t placed = 0;
    std::array<float, 3> rotation_minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    std::array<float, 3> rotation_maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    std::array<std::size_t, 3> rotation_nonzero{};
    std::size_t typed = 0;
    std::size_t ambiguous = 0;
    std::size_t modeled = 0;
    std::size_t model_files = 0;
    std::size_t squadron_members = 0;
    std::size_t unexpanded_containers = 0;
    std::size_t company_members = 0;
    std::map<std::uint32_t, AuditExample> unknown_crcs;
    std::map<std::string, std::size_t> element_types;
    // Placements per TED owner slot, by XML element type (untyped objects as "?").
    std::map<std::uint32_t, std::map<std::string, std::size_t>> owner_types;
    std::map<std::string, std::size_t> model_fields;
    std::map<std::string, AuditExample> types_without_model;
    std::map<std::string, AuditExample> missing_models;
    // Placements per resolved model path.
    std::map<std::string, std::size_t> unique_models;
    std::set<std::string> diagnostics;
    std::vector<std::pair<std::string, std::string>> failures;

    for (const auto& asset : vfs.list(prefix))
    {
        if (extension_of(asset.virtual_path) != ".ted")
        {
            continue;
        }
        auto decoded = decode_asset<eaw::TedMap>(vfs, asset.virtual_path);
        if (!decoded)
        {
            failures.emplace_back(asset.virtual_path, decoded.error().message);
            continue;
        }
        const eaw::TedMap map = std::move(decoded).value();
        ++maps;
        const auto context = map.type == eaw::TedMapType::land ? eaw::GameModelContext::land : eaw::GameModelContext::space;
        const auto layout = eaw::build_map_object_layout(map, catalog, vfs);
        squadron_members += layout.expanded_members;
        unexpanded_containers += layout.unexpanded_containers;
        company_members += layout.company_members;
        for (const auto& object : map.placed_objects)
        {
            ++placed;
            const std::array<float, 3> rotation{object.rotation.x, object.rotation.y, object.rotation.z};
            for (std::size_t axis = 0; axis < rotation.size(); ++axis)
            {
                rotation_minimum[axis] = std::min(rotation_minimum[axis], rotation[axis]);
                rotation_maximum[axis] = std::max(rotation_maximum[axis], rotation[axis]);
                rotation_nonzero[axis] += rotation[axis] != 0.0F ? 1U : 0U;
            }
            const auto match = catalog.resolve(object.crc, context);
            ++owner_types[object.player][match.definition != nullptr ? match.definition->type : std::string("?")];
            if (match.definition == nullptr)
            {
                auto& entry = unknown_crcs[object.crc];
                if (entry.count++ == 0)
                {
                    entry.map = asset.virtual_path;
                }
                continue;
            }
            ++typed;
            ambiguous += match.candidates > 1 ? 1U : 0U;
            ++element_types[match.definition->type];
            std::vector<std::string> messages;
            const auto model = catalog.model(*match.definition, context, &messages);
            diagnostics.insert(messages.begin(), messages.end());
            if (!model)
            {
                record_example(types_without_model, match.definition->type + " " + match.definition->name, asset.virtual_path);
                continue;
            }
            ++modeled;
            ++model_fields[model->field];
            if (const auto path = eaw::resolve_model_path(vfs, model->value))
            {
                ++model_files;
                ++unique_models[*path];
            }
            else
            {
                record_example(missing_models, model->value, asset.virtual_path, match.definition->name);
            }
        }
    }

    const auto percent = [](const std::size_t part, const std::size_t whole)
    {
        std::ostringstream text;
        text << std::fixed << std::setprecision(1) << (whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole)) << '%';
        return text.str();
    };

    std::cout << "Map object resolution audit\n"
              << "XML definitions: " << catalog.size() << '\n'
              << "Maps: " << maps << " (failures: " << failures.size() << ")\n"
              << "Placed objects: " << placed << '\n'
              << "Typed by name CRC: " << typed << " (" << percent(typed, placed) << ")\n"
              << "Ambiguous CRC matches: " << ambiguous << '\n'
              << "With a model reference: " << modeled << " (" << percent(modeled, placed) << ")\n"
              << "Model files resolved: " << model_files << " (" << percent(model_files, placed) << "), unique models: " << unique_models.size() << '\n'
              << "Unknown CRCs: " << unknown_crcs.size() << '\n'
              << "Space squadron members expanded: " << squadron_members << '\n'
              << "Land company members expanded (arranged at runtime): " << company_members << '\n'
              << "Containers without members: " << unexpanded_containers << '\n';
    for (std::size_t axis = 0; axis < rotation_minimum.size() && placed > 0; ++axis)
    {
        std::cout << "Rotation component " << axis << ": " << rotation_minimum[axis] << " .. " << rotation_maximum[axis] << ", non-zero " << rotation_nonzero[axis] << '\n';
    }
    for (const auto& [owner, types] : owner_types)
    {
        std::size_t total = 0;
        for (const auto& [type, count] : types)
        {
            total += count;
        }
        std::cout << "Owner slot " << owner << ": " << total << " placements (";
        bool first = true;
        for (const auto& [type, count] : types)
        {
            std::cout << (first ? "" : ", ") << type << ' ' << count;
            first = false;
        }
        std::cout << ")\n";
    }
    // A single-map audit lists its models and the layer each loads from, which shows which files
    // a mod overrides for that map.
    if (maps == 1)
    {
        std::cout << "\nModels (placements, winning layer):\n";
        for (const auto& [path, count] : unique_models)
        {
            const auto resolved = vfs.resolve(path);
            std::cout << "  " << path << ": " << count << ", " << (resolved ? resolved->mount_name : std::string("unresolved")) << '\n';
        }
    }
    std::cout << "\nResolved element types:\n";
    for (const auto& [type, count] : element_types)
    {
        std::cout << "  " << type << ": " << count << '\n';
    }
    std::cout << "Model fields:\n";
    for (const auto& [field, count] : model_fields)
    {
        std::cout << "  " << field << ": " << count << '\n';
    }
    std::cout << "\nUnknown CRCs:\n";
    std::size_t shown = 0;
    for (const auto& [crc, entry] : by_count(unknown_crcs))
    {
        if (shown++ >= limit)
        {
            break;
        }
        std::cout << "  0x" << std::hex << std::setw(8) << std::setfill('0') << crc << std::dec << std::setfill(' ') << "  x" << entry.count << "  e.g. " << entry.map << '\n';
    }
    std::cout << "\nTyped objects without a model for their map context:\n";
    shown = 0;
    for (const auto& [type, entry] : by_count(types_without_model))
    {
        if (shown++ >= limit)
        {
            break;
        }
        std::cout << "  " << type << "  x" << entry.count << "  e.g. " << entry.map << '\n';
    }
    std::cout << "\nMissing model files:\n";
    shown = 0;
    for (const auto& [model, entry] : by_count(missing_models))
    {
        if (shown++ >= limit)
        {
            break;
        }
        std::cout << "  " << model << "  x" << entry.count << "  (" << entry.detail << ", e.g. " << entry.map << ")\n";
    }
    std::cout << "\nVariant diagnostics: " << diagnostics.size() << '\n';
    shown = 0;
    for (const auto& message : diagnostics)
    {
        if (shown++ >= limit)
        {
            break;
        }
        std::cout << "  " << message << '\n';
    }
    for (const auto& [path, message] : failures)
    {
        std::cout << "map failure: " << path << ": " << message << '\n';
    }
}

void audit_maps(const eaw::VirtualFileSystem& vfs, const std::string_view prefix, const std::size_t limit)
{
    const auto assets = vfs.list(prefix);
    std::size_t files = 0;
    std::size_t land = 0;
    std::size_t space = 0;
    std::size_t unknown = 0;
    std::size_t warnings = 0;
    std::size_t terrain_layers = 0;
    std::size_t objects = 0;
    std::size_t positioned_objects = 0;
    std::size_t active_environment_out_of_range = 0;
    std::map<std::uint32_t, std::size_t> active_environments;
    std::vector<std::pair<std::string, std::string>> failures;
    for (const auto& asset : assets)
    {
        if (extension_of(asset.virtual_path) != ".ted")
        {
            continue;
        }
        ++files;
        const auto decoded = decode_asset<eaw::TedMap>(vfs, asset.virtual_path);
        if (!decoded)
        {
            failures.emplace_back(asset.virtual_path, decoded.error().message);
            continue;
        }
        const auto& map = *decoded;
        if (map.type == eaw::TedMapType::land)
        {
            ++land;
        }
        else if (map.type == eaw::TedMapType::space)
        {
            ++space;
        }
        else
        {
            ++unknown;
        }
        warnings += map.warnings.size();
        if (!map.environments.empty())
        {
            ++active_environments[map.active_environment];
            active_environment_out_of_range += map.active_environment >= map.environments.size() ? 1U : 0U;
        }
        terrain_layers += map.terrain_layers.size();
        objects += map.object_count;
        positioned_objects += std::ranges::count_if(map.placed_objects,
                                                    [](const auto& object)
                                                    {
                                                        return object.has_transform;
                                                    });
    }
    std::cout << "TED semantic audit\n"
              << "Map files: " << files << '\n'
              << "Decoded: " << files - failures.size() << '\n'
              << "Land maps: " << land << '\n'
              << "Space maps: " << space << '\n'
              << "Unknown type: " << unknown << '\n'
              << "Terrain layers: " << terrain_layers << '\n'
              << "Objects: " << objects << '\n'
              << "Positioned objects: " << positioned_objects << '\n'
              << "Active environment out of range: " << active_environment_out_of_range << '\n'
              << "Active environment indices:";
    for (const auto& [index, count] : active_environments)
    {
        std::cout << ' ' << index << 'x' << count;
    }
    std::cout << '\n' << "Warnings: " << warnings << '\n' << "Failures: " << failures.size() << '\n';
    for (std::size_t index = 0; index < std::min(limit, failures.size()); ++index)
    {
        std::cout << "  " << failures[index].first << ": " << failures[index].second << '\n';
    }
}

} // namespace eaw::inspector
