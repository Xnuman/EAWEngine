#include "engine/sandbox_content.hpp"

#include "formats/ala_animation.hpp"
#include "assets/alamo_loaders.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace eaw {

Result<Animation> read_animation(const AssetReader& reader, const std::string_view path, std::vector<std::string>* warnings)
{
    EAW_TRY_ASSIGN(const auto bytes, reader.read(path));
    EAW_TRY_ASSIGN(const auto data, read_ala(bytes));
    if (warnings != nullptr)
    {
        *warnings = data.warnings;
    }
    return load_animation(data);
}

SandboxContent build_sandbox_content(const VirtualFileSystem& vfs, const SandboxOptions& options, AssetCache* cache, const bool reload, std::vector<std::string> extra_spawn_types)
{
    SandboxContent content;
    content.reload = reload;
    const auto start = std::chrono::steady_clock::now();
    if (reload && cache != nullptr)
    {
        content.invalidated = cache->invalidate(vfs);
    }
    const auto epoch = cache != nullptr ? cache->begin_epoch() : 0;
    const RecordingAssetReader reader(vfs);
    std::optional<Error> failure;
    if (options.map)
    {
        std::vector<std::string> spawn_types;
        for (const auto& request : options.spawns)
        {
            if (std::ranges::find(spawn_types, request.type) == spawn_types.end())
            {
                spawn_types.push_back(request.type);
            }
        }
        for (auto& type : extra_spawn_types)
        {
            if (std::ranges::find(spawn_types, type) == spawn_types.end())
            {
                spawn_types.push_back(std::move(type));
            }
        }
        MapBuildOptions build_options;
        build_options.effects = options.effects;
        build_options.particle_fidelity = options.particle_fidelity;
        if (auto map = build_visual_map(reader, *options.map, cache, spawn_types, build_options))
        {
            content.map = std::move(map).value();
        }
        else
        {
            failure = std::move(map).error();
        }
    }
    else
    {
        if (auto model = build_visual_model(reader, options.model, cache))
        {
            content.model = std::move(model).value();
        }
        else
        {
            failure = std::move(model).error();
        }
    }
    if (failure)
    {
        content.error = std::move(failure->message);
        content.dependencies = reader.dependencies();
        return content;
    }
    if (content.model)
    {
        content.animations = model_animations(reader, options.model);
        const auto animation_path = options.animation ? options.animation : default_model_animation(reader, options.model);
        if (animation_path)
        {
            const auto resolved_animation = reader.resolve(*animation_path);
            if (!resolved_animation)
            {
                content.animation_error = "asset not found: " + *animation_path;
            }
            else
            {
                auto animation = read_animation(reader, resolved_animation->virtual_path, &content.animation_warnings);
                if (animation)
                {
                    content.animation = std::move(animation).value();
                    content.animation_path = resolved_animation->virtual_path;
                }
                else
                {
                    content.animation_error = animation.error().message + "; using the bind pose";
                }
            }
        }
    }
    content.build_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    content.dependencies = reader.dependencies();
    if (cache != nullptr)
    {
        content.evicted = cache->evict_unused_since(epoch);
    }
    return content;
}

} // namespace eaw
