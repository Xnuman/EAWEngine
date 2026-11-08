// GuiApp::State: asset loading and selection, previews, and DAT and workspace files.

#include "gui_internal.hpp"

namespace eaw::tools {

void GuiApp::State::reset_map_objects()
{
    map_object_layout.reset();
    map_object_placement.clear();
    map_object_warnings.clear();
    map_object_drawn.clear();
    map_object_cached = false;
}

void GuiApp::State::load_map_objects()
{
    reset_map_objects();
    if (!preview_map)
    {
        return;
    }
    if (!object_index)
    {
        auto index = std::make_unique<ObjectIndex>();
        index->database = XmlAssetDatabase::build(vfs);
        index->catalog = GameObjectCatalog::build(index->database);
        object_index = std::move(index);
    }
    map_object_layout = build_map_object_layout(*preview_map, object_index->catalog, vfs);
    map_object_placement.assign(preview_map->placed_objects.size(), -1);
    for (std::size_t index = 0; index < map_object_layout->placements.size(); ++index)
    {
        const auto& placement = map_object_layout->placements[index];
        map_object_placement[placement.object_index] = static_cast<int>(index);
        if (placement.model_path.empty() || map_models.contains(placement.model_path))
        {
            continue;
        }
        auto& gpu = map_models[placement.model_path];
        const auto model = read_model(vfs, placement.model_path);
        if (!model)
        {
            // Particle ALOs and malformed models fall back to a marker.
            map_object_warnings.push_back(placement.model_path + ": " + model.error().message);
            continue;
        }
        const auto geometry = build_preview_geometry(*model, nullptr, {}, false,
                                                     [this](const std::string& reference)
                                                     {
                                                         return load_texture(object_textures, map_object_warnings, reference);
                                                     });
        gpu.radius = std::max(geometry.radius, 1.0F);
        gpu.valid = renderer.ready() && !geometry.indices.empty() && gpu.mesh.upload(renderer.device(), renderer.context(), geometry.vertices, geometry.indices, geometry.parts);
    }
}

GuiApp::State::State(ID3D11Device* device) : graphics_device(device)
{
    // Previews fall back to a message when the GPU preview pipeline is unavailable.
    static_cast<void>(renderer.initialize(device));
    const auto installations = InstallationLocator{}.discover();
    if (!installations.empty())
    {
        game_root = path_to_utf8(installations.front());
        message = "Detected Empire at War installation";
    }
    else
    {
        message = "Choose the Empire at War installation folder";
    }
}

void GuiApp::State::succeed(std::string text)
{
    message = std::move(text);
    message_error = false;
}

void GuiApp::State::fail(const Error& error)
{
    message = error.message;
    message_error = true;
}

void GuiApp::State::load_assets()
{
    const auto root = utf8_to_path(game_root);
    if (!root)
    {
        fail(root.error());
        return;
    }
    const auto profile = make_game_profile(*root, variant);
    if (!profile)
    {
        fail(make_error("The selected folder is not an Empire at War installation"));
        return;
    }
    VirtualFileSystem next;
    const auto report = mount_game_assets(next, *profile, mods);
    auto listed = next.list("Data");
    std::ranges::sort(listed, {}, &ResolvedAsset::virtual_path);
    vfs = std::move(next);
    assets = std::move(listed);
    mount_warnings = report.warnings;
    selected_asset = -1;
    asset_preview.clear();
    preview_model.reset();
    preview_map.reset();
    preview_inverse_bind.clear();
    preview_animation_assets.clear();
    preview_animation.reset();
    preview_animation_index = -1;
    preview_time = 0.0F;
    model_mesh_cached = false;
    preview_textures.clear();
    preview_texture_warnings.clear();
    map_preview_triangles.clear();
    map_preview_cached = false;
    map_preview_large_window = false;
    object_index.reset();
    map_models.clear();
    object_textures.clear();
    reset_map_objects();
    succeed("Loaded " + std::to_string(assets.size()) + " logical assets from " + std::to_string(vfs.mounts().size()) + " layers");
}

void GuiApp::State::select_preview_animation(const int index)
{
    preview_animation_index = index;
    preview_animation.reset();
    preview_time = 0.0F;
    if (index < 0)
    {
        return;
    }
    if (static_cast<std::size_t>(index) >= preview_animation_assets.size())
    {
        fail(make_error("The selected animation is no longer available"));
        return;
    }
    const auto& asset = preview_animation_assets[static_cast<std::size_t>(index)];
    const auto bytes = vfs.read(asset.virtual_path);
    if (!bytes)
    {
        fail(bytes.error());
        return;
    }
    auto animation = AlaAnimation::parse(*bytes);
    if (!animation)
    {
        fail(animation.error());
        return;
    }
    preview_animation = std::move(animation).value();
    succeed("Loaded animation " + asset.virtual_path);
}

void GuiApp::State::inspect_asset(const int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= assets.size())
    {
        return;
    }
    selected_asset = index;
    const auto& asset = assets[static_cast<std::size_t>(index)];
    const auto read = vfs.read(asset.virtual_path);
    auto preview = read ? bytes_preview(asset, *read) : Result<std::string>(read.error());
    if (!preview)
    {
        // Shown in the preview pane as well as the status line.
        fail(preview.error());
        asset_preview = preview.error().message;
        return;
    }
    const auto& bytes = *read;
    asset_preview = std::move(preview).value();
    preview_model.reset();
    preview_map.reset();
    preview_inverse_bind.clear();
    preview_animation_assets.clear();
    preview_animation.reset();
    preview_animation_index = -1;
    preview_time = 0.0F;
    model_mesh_cached = false;
    preview_textures.clear();
    preview_texture_warnings.clear();
    map_preview_triangles.clear();
    map_preview_cached = false;
    reset_map_objects();
    terrain_mesh_cached = false;

    const auto selected_path = std::filesystem::path(asset.virtual_path);
    const auto extension = lower(selected_path.extension().string());
    if (extension != ".ted")
    {
        map_preview_large_window = false;
    }
    if (extension == ".ted")
    {
        auto map = TedMap::parse(bytes);
        if (!map)
        {
            return;
        }
        preview_map = std::move(map).value();
        load_map_objects();
        return;
    }
    if (extension != ".alo")
    {
        return;
    }
    auto model = AloModel::parse(bytes);
    if (!model)
    {
        return;
    }
    preview_model = std::move(model).value();
    const auto bind_pose = preview_model->bind_pose();
    preview_inverse_bind.reserve(bind_pose.size());
    for (const auto& transform : bind_pose)
    {
        const auto inverse = alo_inverse(transform);
        if (!inverse)
        {
            fail(inverse.error());
            asset_preview = inverse.error().message;
            return;
        }
        preview_inverse_bind.push_back(*inverse);
    }

    const auto directory = lower(selected_path.parent_path().generic_string());
    const auto stem = lower(selected_path.stem().string());
    for (const auto& candidate : assets)
    {
        const auto candidate_path = std::filesystem::path(candidate.virtual_path);
        if (lower(candidate_path.extension().string()) != ".ala" || lower(candidate_path.parent_path().generic_string()) != directory)
        {
            continue;
        }
        const auto candidate_stem = lower(candidate_path.stem().string());
        if (candidate_stem == stem || candidate_stem.starts_with(stem + "_"))
        {
            preview_animation_assets.push_back(candidate);
        }
    }

    for (const auto& mesh : preview_model->meshes)
    {
        for (const auto& submesh : mesh.submeshes)
        {
            for (const auto& parameter : submesh.parameters)
            {
                if (parameter.type == AloShaderParameterType::texture && !parameter.value.empty())
                {
                    static_cast<void>(load_texture(preview_textures, preview_texture_warnings, parameter.value));
                }
            }
        }
    }
}

ID3D11ShaderResourceView* GuiApp::State::load_texture(std::unordered_map<std::string, PreviewTexture>& cache, std::vector<std::string>& warnings, const std::string& reference)
{
    const auto key = lower(reference);
    if (const auto found = cache.find(key); found != cache.end())
    {
        return found->second.view.Get();
    }
    auto& entry = cache[key];
    auto filename = std::filesystem::path(reference).filename();
    filename.replace_extension(".DDS");
    const std::array candidates{std::string("DATA/ART/TEXTURES/") + filename.generic_string(), std::string("DATA/ART/") + filename.generic_string(), filename.generic_string()};
    for (const auto& virtual_path : candidates)
    {
        if (!vfs.resolve(virtual_path))
        {
            continue;
        }
        const auto bytes = vfs.read(virtual_path);
        auto texture = bytes ? create_dds_texture(graphics_device, *bytes) : Result<PreviewTexture>(bytes.error());
        if (texture)
        {
            entry = std::move(texture).value();
        }
        else
        {
            warnings.push_back(reference + ": " + texture.error().message);
        }
        return entry.view.Get();
    }
    warnings.push_back(reference + ": matching DDS not found");
    return nullptr;
}

void GuiApp::State::open_dat(const std::filesystem::path& path)
{
    const auto dat = DatFile::load(path);
    if (!dat)
    {
        fail(dat.error());
        return;
    }
    std::vector<LocalizationRow> loaded;
    loaded.reserve(dat->entries().size());
    for (const auto& entry : dat->entries())
    {
        auto text = utf16_to_utf8(entry.value);
        if (!text)
        {
            fail(with_context("DAT entry " + entry.key, std::move(text).error()));
            return;
        }
        loaded.push_back({entry.key, std::move(text).value(), {}, "existing", {}, {}});
    }
    std::ranges::sort(loaded, {}, &LocalizationRow::key);
    base_dat_path = path;
    workspace_path.reset();
    rows = std::move(loaded);
    selected_row = rows.empty() ? -1 : 0;
    dirty = false;
    validation_messages.clear();
    succeed("Opened " + std::to_string(rows.size()) + " localization entries");
}

void GuiApp::State::open_workspace(const std::filesystem::path& path)
{
    auto loaded = read_localization_workspace(path);
    if (!loaded)
    {
        fail(loaded.error());
        return;
    }
    rows = std::move(loaded).value();
    workspace_path = path;
    selected_row = rows.empty() ? -1 : 0;
    dirty = false;
    validation_messages.clear();
    succeed("Opened workspace with " + std::to_string(rows.size()) + " rows");
}

void GuiApp::State::save_workspace(const std::filesystem::path& path)
{
    if (const auto status = write_localization_workspace(path, rows, true); !status)
    {
        fail(status.error());
        return;
    }
    workspace_path = path;
    dirty = false;
    succeed("Saved localization workspace");
}

void GuiApp::State::validate_workspace()
{
    validation_messages.clear();
    std::unordered_set<std::string> keys;
    for (const auto& row : rows)
    {
        if (row.key.empty())
        {
            validation_messages.emplace_back("Error: a row has an empty key");
        }
        else if (!keys.insert(lower(row.key)).second)
        {
            validation_messages.push_back(std::string(lower(row.key) == "text_end_of_data" ? "Warning: " : "Error: ") + "duplicate key " + row.key);
        }
        if (lower(row.status) == "todo")
        {
            if (row.text.empty())
            {
                validation_messages.push_back("TODO: " + row.key);
            }
            else
            {
                validation_messages.push_back("Warning: " + row.key + " has text but is still marked TODO; its value will be included");
            }
        }
    }
    if (base_dat_path)
    {
        auto candidate = DatFile::load(*base_dat_path);
        const auto applied = candidate ? apply_localization_workspace(*candidate, rows, skip_todo) : Status(candidate.error());
        if (!applied)
        {
            validation_messages.push_back("Error: " + applied.error().message);
        }
        else
        {
            for (const auto& issue : candidate->validate())
            {
                validation_messages.push_back(std::string(issue.severity == DatIssueSeverity::error ? "Error: " : "Warning: ") + issue.message);
            }
        }
    }
    const auto errors = std::ranges::count_if(validation_messages,
                                              [](const auto& item)
                                              {
                                                  return item.starts_with("Error:");
                                              });
    succeed(errors == 0 ? "Validation completed without errors" : "Validation found " + std::to_string(errors) + " errors");
    message_error = errors != 0;
}

void GuiApp::State::build_dat(const std::filesystem::path& output)
{
    if (!base_dat_path)
    {
        fail(make_error("Open a baseline DAT before building an output DAT"));
        return;
    }
    std::error_code path_error;
    const auto baseline = std::filesystem::weakly_canonical(*base_dat_path, path_error);
    path_error.clear();
    const auto destination = std::filesystem::weakly_canonical(output, path_error);
    if (lower(path_to_utf8(baseline)) == lower(path_to_utf8(destination)))
    {
        fail(make_error("Choose a different output path; EAWTools will not overwrite the baseline DAT"));
        return;
    }
    auto loaded = DatFile::load(*base_dat_path);
    if (!loaded)
    {
        fail(loaded.error());
        return;
    }
    auto& dat = *loaded;
    if (const auto status = apply_localization_workspace(dat, rows, skip_todo); !status)
    {
        fail(status.error());
        return;
    }
    const auto issues = dat.validate();
    const auto errors = std::ranges::count_if(issues,
                                              [](const auto& issue)
                                              {
                                                  return issue.severity == DatIssueSeverity::error;
                                              });
    if (errors != 0)
    {
        fail(make_error("The built DAT has " + std::to_string(errors) + " validation errors"));
        return;
    }
    if (const auto status = dat.save(output, true); !status)
    {
        fail(status.error());
        return;
    }
    succeed("Built " + path_to_utf8(output) + " with " + std::to_string(dat.entries().size()) + " entries");
}

void GuiApp::State::scan_mod(const std::filesystem::path& mod_root)
{
    if (!base_dat_path)
    {
        fail(make_error("Open a baseline DAT before scanning a mod"));
        return;
    }
    const auto baseline = DatFile::load(*base_dat_path);
    if (!baseline)
    {
        fail(baseline.error());
        return;
    }
    const auto scanned = scan_mod_localization(mod_root, *baseline);
    if (!scanned)
    {
        fail(scanned.error());
        return;
    }
    const auto& result = *scanned;
    std::unordered_set<std::string> existing;
    for (const auto& row : rows)
    {
        existing.insert(lower(row.key));
    }
    std::size_t added = 0;
    for (const auto& row : result.missing)
    {
        if (existing.insert(lower(row.key)).second)
        {
            rows.push_back(row);
            ++added;
        }
    }
    std::ranges::sort(rows,
                      [](const auto& left, const auto& right)
                      {
                          return std::tie(left.group, left.key) < std::tie(right.group, right.key);
                      });
    dirty = dirty || added != 0;
    selected_row = rows.empty() ? -1 : 0;
    succeed("Scanned " + std::to_string(result.reference_count) + " references and added " + std::to_string(added) + " missing keys");
}

Status GuiApp::State::add_localization_row(LocalizationRow row)
{
    if (row.key.empty())
    {
        return make_error("Localization key cannot be empty");
    }
    if (std::ranges::any_of(rows,
                            [&](const auto& existing)
                            {
                                return lower(existing.key) == lower(row.key);
                            }))
    {
        return make_error("The workspace already contains " + row.key);
    }
    rows.push_back(std::move(row));
    selected_row = static_cast<int>(rows.size() - 1);
    dirty = true;
    return Status::success();
}

} // namespace eaw::tools
