// `texture-audit`: texture references of models and materials across the layered VFS.

#include "asset_inspector/inspector.hpp"

#include "assets/alo_interpretation.hpp"

namespace eaw::inspector {
namespace {

std::optional<std::string> resolve_texture_reference(const eaw::VirtualFileSystem& vfs, const std::string_view reference)
{
    auto filename = std::filesystem::path(reference).filename();
    filename.replace_extension(".DDS");
    const std::array candidates{std::string("DATA/ART/TEXTURES/") + filename.generic_string(), std::string("DATA/ART/") + filename.generic_string(), filename.generic_string()};
    for (const auto& candidate : candidates)
    {
        if (vfs.resolve(candidate))
        {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace

void audit_texture_references(const eaw::VirtualFileSystem& vfs, const std::string_view prefix, const std::size_t limit)
{
    const auto model_assets = vfs.list(prefix);
    std::size_t model_files = 0;
    std::size_t semantic_models = 0;
    std::size_t particle_or_invalid = 0;
    std::size_t materials = 0;
    std::set<std::string> references;
    std::set<std::string> resolved;
    std::map<std::string, std::set<std::string>> unresolved;
    std::map<std::string, std::size_t> shaders;
    std::map<std::string, std::size_t> material_families;
    std::map<std::string, std::size_t> blend_modes;
    std::map<std::string, std::size_t> translation_sources;
    std::map<std::string, std::size_t> render_phases;
    std::map<std::string, std::size_t> lighting_models;
    std::map<std::string, std::size_t> heuristic_shaders;
    std::map<std::string, std::size_t> texture_semantics;
    std::map<std::string, std::size_t> texture_parameters;
    struct NumericUsage
    {
        std::size_t count{};
        std::array<float, 4> minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        std::array<float, 4> maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        std::size_t components{};
    };
    std::map<std::string, NumericUsage> numeric_parameters;
    // State-variant naming census: models and meshes using _ALT<n>/_LOD<n> suffixes.
    std::size_t models_with_alternates = 0;
    std::size_t models_with_lods = 0;
    std::map<std::uint32_t, std::size_t> alternate_mesh_indices;
    std::map<std::uint32_t, std::size_t> lod_mesh_indices;
    std::map<std::uint32_t, std::uint64_t> lod_triangles;
    // Per model: which LOD index carries the most and the fewest triangles.
    std::map<std::uint32_t, std::size_t> densest_lod;
    std::map<std::uint32_t, std::size_t> sparsest_lod;
    std::size_t alternate_bones_without_meshes = 0;
    for (const auto& asset : model_assets)
    {
        if (extension_of(asset.virtual_path) != ".alo")
        {
            continue;
        }
        ++model_files;
        const auto decoded = decode_asset<eaw::AloModelData>(vfs, asset.virtual_path);
        if (!decoded)
        {
            ++particle_or_invalid;
            continue;
        }
        const auto& model = *decoded;
        ++semantic_models;
        bool has_alternate = false;
        bool has_lod = false;
        std::map<std::uint32_t, std::uint64_t> model_lod_triangles;
        for (const auto& mesh : model.meshes)
        {
            const auto variant = eaw::alo_name_variant(mesh.name);
            if (variant.kind == eaw::AloVariantKind::alternate)
            {
                has_alternate = true;
                ++alternate_mesh_indices[variant.index];
            }
            else if (variant.kind == eaw::AloVariantKind::level_of_detail)
            {
                has_lod = true;
                ++lod_mesh_indices[variant.index];
                for (const auto& submesh : mesh.submeshes)
                {
                    lod_triangles[variant.index] += submesh.triangle_count;
                    model_lod_triangles[variant.index] += submesh.triangle_count;
                }
            }
        }
        for (const auto& bone : model.bones)
        {
            alternate_bones_without_meshes += eaw::alo_name_variant(bone.name).kind == eaw::AloVariantKind::alternate ? 1U : 0U;
        }
        models_with_alternates += has_alternate ? 1U : 0U;
        models_with_lods += has_lod ? 1U : 0U;
        if (model_lod_triangles.size() > 1)
        {
            const auto by_triangles = [](const auto& left, const auto& right)
            {
                return left.second < right.second;
            };
            ++densest_lod[std::ranges::max_element(model_lod_triangles, by_triangles)->first];
            ++sparsest_lod[std::ranges::min_element(model_lod_triangles, by_triangles)->first];
        }
        for (const auto& mesh : model.meshes)
        {
            for (const auto& submesh : mesh.submeshes)
            {
                const auto material = eaw::LegacyMaterial::translate(submesh);
                ++materials;
                ++shaders[material.source_shader.empty() ? "<unnamed>" : material.source_shader];
                ++material_families[std::string(eaw::to_string(material.family))];
                ++blend_modes[std::string(eaw::to_string(material.blend))];
                ++translation_sources[std::string(eaw::to_string(material.source))];
                ++render_phases[std::string(eaw::to_string(material.render.phase))];
                ++lighting_models[std::string(eaw::to_string(material.features.lighting))];
                if (material.source == eaw::LegacyTranslationSource::name_heuristic)
                {
                    ++heuristic_shaders[material.source_shader.empty() ? "<unnamed>" : material.source_shader];
                }
                for (const auto& parameter : submesh.parameters)
                {
                    if (parameter.values.empty())
                    {
                        continue;
                    }
                    auto& usage = numeric_parameters[parameter.name];
                    ++usage.count;
                    usage.components = std::max(usage.components, parameter.values.size());
                    for (std::size_t component = 0; component < parameter.values.size() && component < usage.minimum.size(); ++component)
                    {
                        usage.minimum[component] = std::min(usage.minimum[component], parameter.values[component]);
                        usage.maximum[component] = std::max(usage.maximum[component], parameter.values[component]);
                    }
                }
                for (const auto& texture : material.textures)
                {
                    ++texture_semantics[std::string(eaw::to_string(texture.semantic))];
                    ++texture_parameters[texture.parameter];
                    references.insert(texture.reference);
                    if (resolve_texture_reference(vfs, texture.reference))
                    {
                        resolved.insert(texture.reference);
                    }
                    else
                    {
                        unresolved[texture.reference].insert(asset.virtual_path);
                    }
                }
            }
        }
    }

    std::size_t dds_files = 0;
    std::size_t decoded_dds = 0;
    std::size_t dds_mip_levels = 0;
    std::size_t dds_cube_maps = 0;
    std::size_t dds_volumes = 0;
    std::uint32_t maximum_width = 0;
    std::uint32_t maximum_height = 0;
    std::map<std::string, std::size_t> dds_formats;
    std::map<std::string, std::string> dds_format_examples;
    std::vector<std::pair<std::string, std::string>> dds_failures;
    std::size_t mtd_files = 0;
    std::size_t decoded_mtd = 0;
    std::size_t mtd_entries = 0;
    std::size_t mtd_alpha_entries = 0;
    std::size_t mtd_atlases_resolved = 0;
    std::vector<std::pair<std::string, std::string>> mtd_failures;
    for (const auto& asset : vfs.list("DATA"))
    {
        const auto extension = extension_of(asset.virtual_path);
        if (extension == ".dds")
        {
            ++dds_files;
            const auto decoded = decode_asset<eaw::DdsTextureData>(vfs, asset.virtual_path);
            if (!decoded)
            {
                dds_failures.emplace_back(asset.virtual_path, decoded.error().message);
                continue;
            }
            const auto& texture = *decoded;
            ++decoded_dds;
            dds_mip_levels += texture.mip_levels;
            dds_cube_maps += texture.cube_map ? 1U : 0U;
            dds_volumes += texture.volume ? 1U : 0U;
            maximum_width = std::max(maximum_width, texture.width);
            maximum_height = std::max(maximum_height, texture.height);
            const auto format = std::string(eaw::to_string(texture.format));
            ++dds_formats[format];
            dds_format_examples.try_emplace(format, asset.virtual_path);
        }
        else if (extension == ".mtd")
        {
            ++mtd_files;
            const auto decoded = decode_asset<eaw::MtdAtlasData>(vfs, asset.virtual_path);
            if (!decoded)
            {
                mtd_failures.emplace_back(asset.virtual_path, decoded.error().message);
                continue;
            }
            const auto& directory = *decoded;
            ++decoded_mtd;
            mtd_entries += directory.entries.size();
            mtd_alpha_entries += directory.alpha_entries;
            auto atlas = std::filesystem::path(asset.virtual_path);
            atlas.replace_filename(atlas.stem().string() + "COMPRESSED.DDS");
            if (vfs.resolve(atlas.generic_string()))
            {
                ++mtd_atlases_resolved;
            }
            else
            {
                mtd_failures.emplace_back(asset.virtual_path, "matching compressed DDS atlas not found");
            }
        }
    }

    std::cout << "Texture and material compatibility audit\n"
              << "Model files: " << model_files << '\n'
              << "Semantic models: " << semantic_models << '\n'
              << "Particle/unsupported files: " << particle_or_invalid << '\n'
              << "Material instances: " << materials << '\n'
              << "Unique texture references: " << references.size() << '\n'
              << "Resolved references: " << resolved.size() << '\n'
              << "Unresolved references: " << unresolved.size() << "\n\n"
              << "DDS corpus\n"
              << "Files: " << dds_files << '\n'
              << "Decoded: " << decoded_dds << '\n'
              << "Unsupported/corrupt: " << dds_failures.size() << '\n'
              << "Total mip levels: " << dds_mip_levels << '\n'
              << "Largest dimensions: " << maximum_width << 'x' << maximum_height << '\n'
              << "Cube maps: " << dds_cube_maps << '\n'
              << "Volume textures: " << dds_volumes << "\nFormats:\n";
    for (const auto& [format, count] : dds_formats)
    {
        const auto example = dds_format_examples.find(format);
        std::cout << "  " << format << ": " << count << "  (e.g. " << (example != dds_format_examples.end() ? example->second : std::string{}) << ")\n";
    }
    std::cout << "\nMTD corpus\n"
              << "Files: " << mtd_files << '\n'
              << "Decoded: " << decoded_mtd << '\n'
              << "Entries: " << mtd_entries << '\n'
              << "Entries with alpha: " << mtd_alpha_entries << '\n'
              << "Resolved compressed atlases: " << mtd_atlases_resolved << '/' << decoded_mtd << "\n\n"
              << "Material translation\n";
    for (const auto& [family, count] : material_families)
    {
        std::cout << "  family " << family << ": " << count << '\n';
    }
    for (const auto& [blend, count] : blend_modes)
    {
        std::cout << "  blend " << blend << ": " << count << '\n';
    }
    for (const auto& [source, count] : translation_sources)
    {
        std::cout << "  source " << source << ": " << count << '\n';
    }
    for (const auto& [phase, count] : render_phases)
    {
        std::cout << "  phase " << phase << ": " << count << '\n';
    }
    for (const auto& [lighting, count] : lighting_models)
    {
        std::cout << "  lighting " << lighting << ": " << count << '\n';
    }
    for (const auto& [shader, count] : heuristic_shaders)
    {
        std::cout << "  heuristic-only shader " << shader << ": " << count << '\n';
    }
    for (const auto& [semantic, count] : texture_semantics)
    {
        std::cout << "  texture " << semantic << ": " << count << '\n';
    }
    std::cout << "Models with _ALT<n> meshes: " << models_with_alternates << ", _LOD<n> meshes: " << models_with_lods << "\nALT mesh indices:";
    for (const auto& [index, count] : alternate_mesh_indices)
    {
        std::cout << ' ' << index << 'x' << count;
    }
    std::cout << "\nLOD mesh indices:";
    for (const auto& [index, count] : lod_mesh_indices)
    {
        std::cout << ' ' << index << 'x' << count;
    }
    std::cout << "\nLOD triangles by index:";
    for (const auto& [index, count] : lod_triangles)
    {
        std::cout << ' ' << index << ':' << count;
    }
    std::cout << "\nModels whose densest LOD is index:";
    for (const auto& [index, count] : densest_lod)
    {
        std::cout << ' ' << index << 'x' << count;
    }
    std::cout << "\nModels whose sparsest LOD is index:";
    for (const auto& [index, count] : sparsest_lod)
    {
        std::cout << ' ' << index << 'x' << count;
    }
    std::cout << "\nBones with _ALT<n> names (meshes and particle emitters): " << alternate_bones_without_meshes << '\n';
    std::cout << "Numeric parameter usage (count, per-component min..max):\n";
    for (const auto& [name, usage] : numeric_parameters)
    {
        std::cout << "  " << name << ": " << usage.count;
        for (std::size_t component = 0; component < usage.components; ++component)
        {
            std::cout << (component == 0 ? "  " : ", ") << usage.minimum[component] << ".." << usage.maximum[component];
        }
        std::cout << '\n';
    }
    std::cout << "Texture parameter usage:\n";
    for (const auto& [parameter, count] : texture_parameters)
    {
        std::cout << "  " << parameter << ": " << count << '\n';
    }

    std::vector<std::pair<std::string, std::size_t>> sorted_shaders(shaders.begin(), shaders.end());
    std::ranges::sort(sorted_shaders,
                      [](const auto& left, const auto& right)
                      {
                          return left.second != right.second ? left.second > right.second : left.first < right.first;
                      });
    std::cout << "Shader usage:\n";
    for (std::size_t index = 0; index < std::min(limit, sorted_shaders.size()); ++index)
    {
        std::cout << "  " << sorted_shaders[index].first << ": " << sorted_shaders[index].second << '\n';
    }

    std::cout << "\nUnresolved model texture references:\n";
    std::size_t shown = 0;
    for (const auto& [reference, models] : unresolved)
    {
        if (shown++ >= limit)
        {
            break;
        }
        std::cout << "  " << reference << "  (" << models.size() << " model(s))\n";
        if (!models.empty())
        {
            std::cout << "    e.g. " << *models.begin() << '\n';
        }
    }
    std::cout << "DDS failures:\n";
    for (std::size_t index = 0; index < std::min(limit, dds_failures.size()); ++index)
    {
        std::cout << "  " << dds_failures[index].first << ": " << dds_failures[index].second << '\n';
    }
    std::cout << "MTD failures/dependencies:\n";
    for (std::size_t index = 0; index < std::min(limit, mtd_failures.size()); ++index)
    {
        std::cout << "  " << mtd_failures[index].first << ": " << mtd_failures[index].second << '\n';
    }
}

} // namespace eaw::inspector
