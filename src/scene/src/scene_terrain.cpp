#include "core/checksum.hpp"
#include "core/string_utils.hpp"
#include "formats/dds_texture.hpp"
#include "render/sky_visibility.hpp"
#include "scene/scene_builder_internal.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <system_error>

namespace eaw::scene_build {

namespace {

// Height of the rendered terrain surface at world X/Y, using the same two-triangle split per cell
// as append_terrain so draped geometry follows the drawn surface exactly. Outside the grid the
// nearest edge is used.
float terrain_surface_height(const TedMapData& map, const float world_x, const float world_y)
{
    const auto width = map.terrain_width;
    const auto height = map.terrain_height;
    const auto grid_x = std::clamp(world_x / terrain_sample_spacing, 0.0F, static_cast<float>(width - 1));
    const auto grid_y = std::clamp(world_y / terrain_sample_spacing, 0.0F, static_cast<float>(height - 1));
    const auto x = std::min(static_cast<std::uint32_t>(grid_x), width - 2);
    const auto y = std::min(static_cast<std::uint32_t>(grid_y), height - 2);
    const auto fx = grid_x - static_cast<float>(x);
    const auto fy = grid_y - static_cast<float>(y);
    const auto sample = [&](const std::uint32_t sx, const std::uint32_t sy)
    {
        return static_cast<float>(map.elevation_map[static_cast<std::size_t>(sy) * width + sx]) / terrain_elevation_scale;
    };
    const auto h00 = sample(x, y);
    const auto h10 = sample(x + 1, y);
    const auto h01 = sample(x, y + 1);
    const auto h11 = sample(x + 1, y + 1);
    // Triangles (x,y)-(x+1,y)-(x+1,y+1) when fx >= fy, otherwise (x,y)-(x+1,y+1)-(x,y+1).
    return fx >= fy ? h00 + fx * (h10 - h00) + fy * (h11 - h10) : h00 + fx * (h11 - h01) + fy * (h01 - h00);
}

// Water is a grid (enhanced water's waves displace its vertices; docs/water-research.md, the ocean
// of the roadmap's item h): cells of about this size, at most this many per side. Engine choices: a
// wave's crest spans a few cells at the shortest wavelengths the shader uses.
constexpr float water_cell_size = 12.0F;
constexpr std::uint32_t water_max_cells = 512;
// How far waves may lift or lower the surface: the sum of sandbox.hlsl's OCEAN_AMPLITUDES (9.6) at
// the largest height the Graphics panel allows (VisualOceanWaves::max_height_scale, 1.25); the mesh's
// bounds, so culling never drops a raised crest.
constexpr float water_wave_reach = 12.0F;

} // namespace

// Builds the land heightfield as independent triangles whose vertices carry the triangle's corner
// layers. Each TED layer becomes a material; its texture tiles world X/Y once per tile_size.
void append_terrain(SceneBuilder& builder, const TedMapData& map)
{
    auto& scene = builder.scene;
    const auto width = map.terrain_width;
    const auto height = map.terrain_height;
    const auto samples = static_cast<std::size_t>(width) * height;
    if (width < 2 || height < 2 || map.elevation_map.size() < samples || map.terrain_layer_map.size() < samples || map.terrain_layers.empty())
    {
        return;
    }
    if (width > maximum_terrain_dimension || height > maximum_terrain_dimension)
    {
        scene.warnings.push_back("terrain " + std::to_string(width) + "x" + std::to_string(height) + " exceeds the preview limit of " + std::to_string(maximum_terrain_dimension) +
                                 " samples per side and is not drawn");
        return;
    }

    auto& terrain = scene.terrain;
    terrain.first_material = static_cast<std::uint32_t>(scene.materials.size());
    terrain.layer_count = static_cast<std::uint32_t>(map.terrain_layers.size());
    for (std::size_t index = 0; index < map.terrain_layers.size(); ++index)
    {
        const auto& layer = map.terrain_layers[index];
        VisualMaterial material;
        material.name = "terrain layer " + std::to_string(index) + (layer.color_texture.empty() ? std::string{} : " (" + layer.color_texture + ")");
        material.lighting = VisualLightingModel::lit;
        // Terrain highlights are modest; the reference terrain-mesh shader masks them by base alpha.
        material.specular = {0.25F, 0.25F, 0.25F};
        material.uv_scale = 1.0F / (layer.tile_size > 0.0F ? layer.tile_size : 64.0F);
        if (!layer.color_texture.empty())
        {
            material.base_color_texture = scene_texture(builder, layer.color_texture);
        }
        else
        {
            // A layer without a color texture covers nothing: the terrain is not drawn where it
            // dominates, so the sky and background show through (Cloud City's platforms float over
            // such terrain). Tint alpha carries the layer's coverage to the terrain shader.
            material.tint[3] = 0.0F;
        }
        if (!layer.normal_texture.empty())
        {
            material.normal_texture = scene_texture(builder, layer.normal_texture);
            if (material.normal_texture != invalid_visual_texture)
            {
                material.lighting = VisualLightingModel::normal_mapped;
            }
        }
        scene.materials.push_back(std::move(material));
    }

    const auto elevation = [&](const std::uint32_t x, const std::uint32_t y)
    {
        return static_cast<float>(map.elevation_map[static_cast<std::size_t>(y) * width + x]) / terrain_elevation_scale;
    };
    std::size_t invalid_layers = 0;
    const auto layer_at = [&](const std::uint32_t x, const std::uint32_t y)
    {
        const std::uint32_t layer = map.terrain_layer_map[static_cast<std::size_t>(y) * width + x];
        if (layer >= terrain.layer_count)
        {
            ++invalid_layers;
            return 0U;
        }
        return layer;
    };
    const auto vertex_position = [&](const std::uint32_t x, const std::uint32_t y)
    {
        return Vector3{static_cast<float>(x) * terrain_sample_spacing, static_cast<float>(y) * terrain_sample_spacing, elevation(x, y)};
    };
    const auto vertex_normal = [&](const std::uint32_t x, const std::uint32_t y)
    {
        const auto left = elevation(x == 0 ? 0 : x - 1, y);
        const auto right = elevation(std::min(x + 1, width - 1), y);
        const auto down = elevation(x, y == 0 ? 0 : y - 1);
        const auto up = elevation(x, std::min(y + 1, height - 1));
        const auto span_x = static_cast<float>(std::min(x + 1, width - 1) - (x == 0 ? 0 : x - 1)) * terrain_sample_spacing;
        const auto span_y = static_cast<float>(std::min(y + 1, height - 1) - (y == 0 ? 0 : y - 1)) * terrain_sample_spacing;
        const Vector3 normal{-(right - left) / span_x, -(up - down) / span_y, 1.0F};
        return normal / length(normal);
    };

    terrain.vertices.reserve(static_cast<std::size_t>(width - 1) * (height - 1) * 6U);
    terrain.bounds_minimum = empty_bounds_minimum;
    terrain.bounds_maximum = empty_bounds_maximum;
    const auto emit_triangle = [&](const std::array<std::array<std::uint32_t, 2>, 3>& corners)
    {
        const std::array<std::uint32_t, 3> layers{layer_at(corners[0][0], corners[0][1]), layer_at(corners[1][0], corners[1][1]), layer_at(corners[2][0], corners[2][1])};
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            VisualTerrainVertex vertex;
            vertex.position = vertex_position(corners[corner][0], corners[corner][1]);
            vertex.normal = vertex_normal(corners[corner][0], corners[corner][1]);
            vertex.layers = layers;
            vertex.barycentric[corner] = 1.0F;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                terrain.bounds_minimum[axis] = std::min(terrain.bounds_minimum[axis], vertex.position[axis]);
                terrain.bounds_maximum[axis] = std::max(terrain.bounds_maximum[axis], vertex.position[axis]);
            }
            terrain.vertices.push_back(vertex);
        }
    };
    for (std::uint32_t y = 0; y + 1 < height; ++y)
    {
        for (std::uint32_t x = 0; x + 1 < width; ++x)
        {
            emit_triangle({{{x, y}, {x + 1, y}, {x + 1, y + 1}}});
            emit_triangle({{{x, y}, {x + 1, y + 1}, {x, y + 1}}});
        }
    }
    if (invalid_layers != 0)
    {
        scene.warnings.push_back(std::to_string(invalid_layers) + " terrain vertex references to layers outside the map's " + std::to_string(terrain.layer_count) + " layers were drawn with layer 0");
    }
    // How much sky each height sample sees, as a texture over the terrain (enhanced lighting reads
    // it; headless builds skip it).
    if (builder.textures && !terrain.vertices.empty())
    {
        std::vector<float> heights(samples);
        for (std::size_t index = 0; index < samples; ++index)
        {
            heights[index] = static_cast<float>(map.elevation_map[index]) / terrain_elevation_scale;
        }
        const auto visibility = compute_sky_visibility(heights, width, height, terrain_sample_spacing);
        if (!visibility.empty())
        {
            VisualTexture texture;
            texture.name = "terrain sky visibility";
            texture.format = VisualTextureFormat::r8_unorm;
            VisualTextureMip mip;
            mip.width = width;
            mip.height = height;
            mip.row_pitch = width;
            mip.slice_pitch = samples;
            mip.data.resize(samples);
            for (std::size_t index = 0; index < samples; ++index)
            {
                mip.data[index] = static_cast<std::byte>(std::lround(std::clamp(visibility[index], 0.0F, 1.0F) * 255.0F));
            }
            // Identified by its texels, so a rebuilt map keeps the uploaded texture.
            texture.identity = "terrain sky visibility " + std::to_string(width) + "x" + std::to_string(height) + " " + std::to_string(crc32(mip.data));
            texture.mips.push_back(std::move(mip));
            terrain.sky_visibility_texture = static_cast<std::uint32_t>(scene.textures.size());
            scene.textures.push_back(std::move(texture));
        }
    }
}

// Water, lava, and ice surfaces: a flat quad over the terrain at the map's water level. Texture
// roles follow the corpus: for water the first texture (0x1d) is a bump map and the second (0x1e)
// a plain color fallback; lava and ice store their color texture first. Tiling (one repeat per
// 200 world units) and scroll speeds are engine choices; the related TED fields are preserved but
// not interpreted yet.
bool ocean_map(const std::string_view map_path)
{
    static constexpr std::array<std::string_view, 2> ocean_planets{"KAMINO", "MANAAN"};
    const auto file_name = map_path.substr(map_path.find_last_of("/\\") + 1);
    return std::ranges::any_of(ocean_planets,
                               [&](const std::string_view planet)
                               {
                                   return ascii_find_ignoring_case(file_name, planet) != std::string_view::npos;
                               });
}

std::optional<std::uint32_t> append_water(SceneBuilder& builder, const TedMapData& map)
{
    auto& scene = builder.scene;
    const auto& terrain = scene.terrain;
    const auto type = map.water.type;
    if (terrain.vertices.empty() || (type != TedWaterType::water && type != TedWaterType::lava && type != TedWaterType::ice))
    {
        return std::nullopt;
    }
    // A surface entirely below the ground is never visible (for example Cloud City's -150).
    if (map.water.level < terrain.bounds_minimum[2])
    {
        return std::nullopt;
    }

    VisualMaterial material;
    const auto texture = [&](const std::string& reference)
    {
        return reference.empty() ? invalid_visual_texture : scene_texture(builder, reference);
    };
    material.diffuse = {map.water.color[0], map.water.color[1], map.water.color[2], 1.0F};
    material.specular = {0.6F, 0.6F, 0.6F};
    if (type == TedWaterType::water)
    {
        material.name = "water surface";
        material.water = true;
        material.blend = VisualBlendMode::alpha_blend;
        material.diffuse[3] = 0.75F;
        material.base_color_texture = texture(map.water_maps[1]);
        material.normal_texture = texture(map.water_maps[0]);
        material.uv_scroll = {0.010F, 0.004F};
    }
    else if (type == TedWaterType::lava)
    {
        material.name = "lava surface";
        material.lighting = VisualLightingModel::unlit;
        // Lava maps store black in the tint field (Mustafar), so the texture is shown as authored.
        material.diffuse = {1.0F, 1.0F, 1.0F, 1.0F};
        material.base_color_texture = texture(map.water_maps[0]);
        if (material.base_color_texture == invalid_visual_texture)
        {
            material.base_color_texture = texture(map.water_maps[1]);
        }
        material.uv_scroll = {0.004F, 0.002F};
    }
    else
    {
        material.name = "ice surface";
        material.base_color_texture = texture(map.water_maps[0]);
        material.normal_texture = texture(map.water_maps[1]);
        material.specular = {0.8F, 0.8F, 0.85F};
    }
    if (material.lighting != VisualLightingModel::unlit)
    {
        material.lighting = material.normal_texture != invalid_visual_texture ? VisualLightingModel::normal_mapped : VisualLightingModel::lit;
        // Liquid surfaces take the full key-light highlight (the color textures are opaque, so
        // base alpha is 1); the original's reflections and refraction are not modeled yet.
        material.gloss = VisualGlossSource::base_alpha;
    }

    constexpr float tile = 200.0F;
    const auto level = map.water.level;
    const auto minimum_x = terrain.bounds_minimum[0];
    const auto minimum_y = terrain.bounds_minimum[1];
    const auto maximum_x = terrain.bounds_maximum[0];
    const auto maximum_y = terrain.bounds_maximum[1];
    VisualMesh mesh;
    mesh.name = material.name;
    mesh.first_draw = static_cast<std::uint32_t>(scene.draws.size());
    mesh.draw_count = 1;
    const bool waves = type == TedWaterType::water;
    mesh.bounds_minimum = {minimum_x, minimum_y, level - (waves ? water_wave_reach : 0.0F)};
    mesh.bounds_maximum = {maximum_x, maximum_y, level + (waves ? water_wave_reach : 0.0F)};
    const auto base_vertex = static_cast<std::int32_t>(scene.vertices.size());
    const auto first_index = static_cast<std::uint32_t>(scene.indices.size());
    // Water as a grid whose vertices know the depth beneath them (the second texture coordinates'
    // x: the surface's level less the ground's height, negative over land) for the waves to fade
    // toward the shore; lava and ice stay one flat quad.
    const auto cells_x = waves ? std::clamp(static_cast<std::uint32_t>(std::ceil((maximum_x - minimum_x) / water_cell_size)), 1U, water_max_cells) : 1U;
    const auto cells_y = waves ? std::clamp(static_cast<std::uint32_t>(std::ceil((maximum_y - minimum_y) / water_cell_size)), 1U, water_max_cells) : 1U;
    for (std::uint32_t row = 0; row <= cells_y; ++row)
    {
        for (std::uint32_t column = 0; column <= cells_x; ++column)
        {
            const auto x = minimum_x + (maximum_x - minimum_x) * static_cast<float>(column) / static_cast<float>(cells_x);
            const auto y = minimum_y + (maximum_y - minimum_y) * static_cast<float>(row) / static_cast<float>(cells_y);
            VisualVertex vertex;
            vertex.position = {x, y, level};
            vertex.normal = {0.0F, 0.0F, 1.0F};
            vertex.texture_coordinates = {x / tile, y / tile};
            vertex.tangent = {1.0F, 0.0F, 0.0F};
            vertex.binormal = {0.0F, 1.0F, 0.0F};
            vertex.texture_coordinates_1 = {waves ? level - terrain_surface_height(map, x, y) : 0.0F, 0.0F};
            scene.vertices.push_back(vertex);
        }
    }
    for (std::uint32_t row = 0; row < cells_y; ++row)
    {
        for (std::uint32_t column = 0; column < cells_x; ++column)
        {
            const auto corner = row * (cells_x + 1U) + column;
            const auto above = corner + cells_x + 1U;
            scene.indices.insert(scene.indices.end(), {corner, corner + 1U, above + 1U, corner, above + 1U, above});
        }
    }
    const auto index_count = static_cast<std::uint32_t>(scene.indices.size()) - first_index;
    const auto material_index = static_cast<std::uint32_t>(scene.materials.size());
    scene.materials.push_back(std::move(material));
    scene.draws.push_back({first_index, index_count, base_vertex, material_index});
    const auto mesh_index = static_cast<std::uint32_t>(scene.meshes.size());
    scene.meshes.push_back(std::move(mesh));
    scene.instances.push_back({mesh_index});
    return mesh_index;
}

// Tracks become unlit, alpha-blended ribbons draped over the terrain: the centerline is the
// control-point polyline (sampled at least every terrain cell), each point's width spans the
// ribbon, and opacity follows the start/middle/end keys. Texture V repeats once per average
// width along the track; the header's scroll values animate it.
std::size_t append_tracks(SceneBuilder& builder, const TedMapData& map)
{
    auto& scene = builder.scene;
    if (map.tracks.empty() || scene.terrain.vertices.empty())
    {
        return 0;
    }
    constexpr float lift = 0.6F;
    VisualMesh mesh;
    mesh.name = "terrain tracks";
    mesh.first_draw = static_cast<std::uint32_t>(scene.draws.size());
    mesh.bounds_minimum = empty_bounds_minimum;
    mesh.bounds_maximum = empty_bounds_maximum;
    std::size_t built = 0;
    for (const auto& track : map.tracks)
    {
        const auto count = std::min(track.points.size(), track.widths.size());
        if (count < 2 || track.texture.empty())
        {
            continue;
        }
        const auto texture = scene_texture(builder, track.texture);
        if (texture == invalid_visual_texture)
        {
            continue;
        }
        double total_length = 0.0;
        float average_width = 0.0F;
        for (std::size_t index = 0; index < count; ++index)
        {
            average_width += std::fabs(track.widths[index]) / static_cast<float>(count);
            if (index + 1 < count)
            {
                total_length += std::hypot(track.points[index + 1][0] - track.points[index][0], track.points[index + 1][1] - track.points[index][1]);
            }
        }
        if (!(total_length > 0.0) || !(average_width > 0.0F))
        {
            continue;
        }
        const auto point_direction = [&](const std::size_t index)
        {
            const auto& previous = track.points[index == 0 ? 0 : index - 1];
            const auto& next = track.points[std::min(index + 1, count - 1)];
            const auto dx = next[0] - previous[0];
            const auto dy = next[1] - previous[1];
            const auto length = std::hypot(dx, dy);
            return length > 1.0e-6F ? Vector2{dx / length, dy / length} : Vector2{1.0F, 0.0F};
        };
        const auto opacity_at = [&](const double fraction)
        {
            const auto t = static_cast<float>(std::clamp(fraction, 0.0, 1.0));
            return t < 0.5F ? std::lerp(track.opacity[0], track.opacity[1], t * 2.0F) : std::lerp(track.opacity[1], track.opacity[2], t * 2.0F - 1.0F);
        };

        const auto base_vertex = static_cast<std::int32_t>(scene.vertices.size());
        const auto first_index = static_cast<std::uint32_t>(scene.indices.size());
        std::uint32_t emitted = 0;
        double travelled = 0.0;
        const auto emit = [&](const Vector2& center, const Vector2& direction, const float width)
        {
            const Vector2 side{-direction[1], direction[0]};
            const auto half = std::fabs(width) * 0.5F;
            const auto alpha = opacity_at(travelled / total_length);
            const auto v = static_cast<float>(travelled / average_width);
            for (const float across : {-1.0F, 1.0F})
            {
                VisualVertex vertex;
                const auto x = center[0] + side[0] * half * across;
                const auto y = center[1] + side[1] * half * across;
                vertex.position = {x, y, terrain_surface_height(map, x, y) + lift};
                vertex.texture_coordinates = {across < 0.0F ? 0.0F : 1.0F, v};
                vertex.tangent = {direction[0], direction[1], 0.0F};
                vertex.binormal = {side[0], side[1], 0.0F};
                vertex.color = {1.0F, 1.0F, 1.0F, alpha};
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    mesh.bounds_minimum[axis] = std::min(mesh.bounds_minimum[axis], vertex.position[axis]);
                    mesh.bounds_maximum[axis] = std::max(mesh.bounds_maximum[axis], vertex.position[axis]);
                }
                scene.vertices.push_back(vertex);
            }
            if (emitted != 0)
            {
                const auto a = (emitted - 1) * 2;
                scene.indices.insert(scene.indices.end(), {a, a + 1, a + 3, a, a + 3, a + 2});
            }
            ++emitted;
        };
        double segment_start = 0.0;
        for (std::size_t index = 0; index + 1 < count; ++index)
        {
            const auto& from = track.points[index];
            const auto& to = track.points[index + 1];
            const auto length = std::hypot(to[0] - from[0], to[1] - from[1]);
            const auto steps = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(length / terrain_sample_spacing)));
            const auto from_direction = point_direction(index);
            const auto to_direction = point_direction(index + 1);
            for (std::size_t step = index == 0 ? 0 : 1; step <= steps; ++step)
            {
                const auto t = static_cast<float>(step) / static_cast<float>(steps);
                travelled = segment_start + static_cast<double>(t) * length;
                Vector2 direction{std::lerp(from_direction[0], to_direction[0], t), std::lerp(from_direction[1], to_direction[1], t)};
                const auto direction_length = std::hypot(direction[0], direction[1]);
                direction = direction_length > 1.0e-6F ? Vector2{direction[0] / direction_length, direction[1] / direction_length} : from_direction;
                emit({std::lerp(from[0], to[0], t), std::lerp(from[1], to[1], t)}, direction, std::lerp(track.widths[index], track.widths[index + 1], t));
            }
            segment_start += length;
        }

        VisualMaterial material;
        material.name = "track " + track.texture;
        material.blend = VisualBlendMode::alpha_blend;
        material.lighting = VisualLightingModel::unlit;
        material.base_color_texture = texture;
        material.uv_scroll = track.scroll;
        const auto material_index = static_cast<std::uint32_t>(scene.materials.size());
        scene.materials.push_back(std::move(material));
        scene.draws.push_back({first_index, static_cast<std::uint32_t>(scene.indices.size()) - first_index, base_vertex, material_index});
        ++built;
    }
    if (built == 0)
    {
        return 0;
    }
    mesh.draw_count = static_cast<std::uint32_t>(scene.draws.size()) - mesh.first_draw;
    const auto mesh_index = static_cast<std::uint32_t>(scene.meshes.size());
    scene.meshes.push_back(std::move(mesh));
    scene.instances.push_back({mesh_index});
    return built;
}

} // namespace eaw::scene_build
