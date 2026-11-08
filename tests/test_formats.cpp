#include "test_support.hpp"

namespace {

using namespace eaw_test;

void test_alo_model()
{
    const auto model = expect_ok(eaw::AloModel::parse(make_test_alo()), "ALO parse");
    require(model.bones.size() == 1 && model.bones[0].name == "ROOT" && model.bones[0].parent == -1, "ALO skeleton decoding failed");
    require(model.meshes.size() == 1 && model.meshes[0].name == "TEST_MESH" && model.meshes[0].attached_bone == 0U, "ALO mesh decoding failed");
    require(model.submesh_count() == 1 && model.vertex_count() == 3 && model.triangle_count() == 1 && model.meshes[0].submeshes[0].vertex_stride == 144 &&
                model.meshes[0].submeshes[0].vertices.size() == 3 && model.meshes[0].submeshes[0].indices.size() == 3 && model.meshes[0].submeshes[0].shader == "MeshGloss.fx",
            "ALO submesh decoding failed");
    const auto& parameters = model.meshes[0].submeshes[0].parameters;
    require(parameters.size() == 2 && parameters[0].name == "Diffuse" && parameters[0].values == std::vector<float>{0.25F, 0.5F, 0.75F, 0.123456789F} && parameters[1].name == "Specular" &&
                parameters[1].values.empty() && parameters[1].value.empty(),
            "ALO numeric shader parameters were not decoded exactly or a short chunk was over-read");
    const auto bind_pose = model.bind_pose();
    require(bind_pose.size() == 1 && bind_pose[0].values[0] == 1.0F && bind_pose[0].values[5] == 1.0F && bind_pose[0].values[10] == 1.0F, "ALO bind-pose decoding failed");
}

void test_alo_default_mesh_visibility()
{
    require(eaw::alo_name_variant("NB_dish_ALT2").kind == eaw::AloVariantKind::alternate && eaw::alo_name_variant("NB_dish_ALT2").index == 2, "ALT suffix was not parsed");
    require(eaw::alo_name_variant("hull_lod1").kind == eaw::AloVariantKind::level_of_detail && eaw::alo_name_variant("hull_lod1").index == 1, "LOD suffix is case-insensitive");
    require(eaw::alo_name_variant("ALTAR").kind == eaw::AloVariantKind::none && eaw::alo_name_variant("BASE_ALT").kind == eaw::AloVariantKind::none &&
                eaw::alo_name_variant("BASE_ALT1X").kind == eaw::AloVariantKind::none,
            "names without a trailing numbered variant were misread");

    eaw::AloModel model;
    model.bones = {{.name = "ROOT", .visible = true}, {.name = "FIRE_ALT1", .visible = true}, {.name = "DEBRIS", .visible = false}};
    const auto mesh = [](std::string name, std::optional<std::uint32_t> bone = std::nullopt, const bool hidden = false)
    {
        eaw::AloMesh result;
        result.name = std::move(name);
        result.attached_bone = bone;
        result.hidden = hidden;
        return result;
    };
    model.meshes = {
        mesh("BASE_ALT0"), mesh("BASE_ALT1"), mesh("TOWER"), mesh("HULL_LOD0"), mesh("HULL_LOD2"), mesh("HULL_LOD1"), mesh("FLAME", 1U), mesh("RUBBLE", 2U), mesh("PLATE", std::nullopt, true)};
    const auto visible = eaw::alo_default_mesh_visibility(model);
    require(visible == std::vector<bool>{true, false, true, false, true, false, false, false, false}, "default mesh visibility must keep ALT0 and the highest LOD only");
    const auto animated = eaw::alo_default_mesh_visibility(model, false);
    require(animated[7] && !animated[6] && !animated[8], "an animated caller must decide bind-pose bone visibility from its pose");
}

void test_texture_formats_and_materials()
{
    auto dds = make_test_dds();
    const auto write_u32 = [](std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            bytes[offset + shift / 8] = static_cast<std::byte>((value >> shift) & 0xffU);
        }
    };
    const auto texture = expect_ok(eaw::DdsTexture::parse(dds), "DDS parse");
    require(texture.width == 4 && texture.height == 4 && texture.mip_levels == 1 && texture.format == eaw::DdsFormat::bc1_unorm && texture.data_offset == 128 && texture.data_size == 8 &&
                texture.subresources.size() == 1 && texture.subresources[0].row_pitch == 8,
            "DDS semantic decoding failed");
    const auto truncated_dds = eaw::DdsTexture::parse(std::span<const std::byte>(dds).first(dds.size() - 1U));
    require(!truncated_dds.ok() && truncated_dds.error().message.find("truncated DDS image data") != std::string::npos, "truncated DDS data was accepted");

    std::vector<std::byte> mtd;
    append_u32(mtd, 2);
    const auto append_mtd_entry = [&](const std::string_view name, const std::uint32_t x, const std::uint32_t y, const std::uint32_t width, const std::uint32_t height, const bool alpha)
    {
        const auto begin = mtd.size();
        for (const auto character : name)
        {
            mtd.push_back(static_cast<std::byte>(character));
        }
        mtd.resize(begin + 64U, std::byte{});
        append_u32(mtd, x);
        append_u32(mtd, y);
        append_u32(mtd, width);
        append_u32(mtd, height);
        mtd.push_back(alpha ? std::byte{1} : std::byte{});
    };
    append_mtd_entry("ICON_A.TGA", 0, 0, 32, 16, true);
    append_mtd_entry("ICON_B.TGA", 32, 8, 8, 24, false);
    const auto directory = expect_ok(eaw::MtdDirectory::parse(mtd), "MTD parse");
    require(directory.entries.size() == 2 && directory.entries[0].name == "ICON_A.TGA" && directory.entries[0].has_alpha && directory.entries[0].name_crc == eaw::dat_key_crc("ICON_A.TGA") &&
                directory.atlas_width == 40 && directory.atlas_height == 32 && directory.alpha_entries == 1,
            "MTD semantic decoding failed");
    const auto truncated_mtd = eaw::MtdDirectory::parse(std::span<const std::byte>(mtd).first(mtd.size() - 1U));
    require(!truncated_mtd.ok() && truncated_mtd.error().message.find("invalid MTD entry table size") != std::string::npos, "truncated MTD data was accepted");

    eaw::AloSubMesh submesh;
    submesh.shader = "MeshGloss.fx";
    submesh.parameters.push_back({"DiffuseTexture", eaw::AloShaderParameterType::texture, "UNIT.TGA"});
    submesh.parameters.push_back({"BumpTexture", eaw::AloShaderParameterType::texture, "UNIT_BUMP.TGA"});
    submesh.parameters.push_back({"Glossiness", eaw::AloShaderParameterType::scalar, "32"});
    const auto material = eaw::LegacyMaterial::translate(submesh);
    require(material.family == eaw::LegacyMaterialFamily::mesh && material.blend == eaw::LegacyBlendMode::opaque && material.textures.size() == 2 &&
                material.textures[0].semantic == eaw::LegacyTextureSemantic::base_color && material.textures[1].semantic == eaw::LegacyTextureSemantic::normal && material.parameters.size() == 3,
            "legacy material translation failed");

    require(material.source == eaw::LegacyTranslationSource::reference_profile && material.features.gloss == eaw::LegacyGlossSource::base_alpha && material.render.depth_write &&
                material.render.fade_blend,
            "gloss reference profile was not applied");

    // The reference corpus renders shields additively (ONE/ONE) with culling disabled.
    submesh.shader = "MeshShield.fx";
    const auto shield = eaw::LegacyMaterial::translate(submesh);
    require(shield.family == eaw::LegacyMaterialFamily::shield && shield.blend == eaw::LegacyBlendMode::additive && shield.two_sided() && !shield.render.depth_write &&
                shield.features.uv_animation == eaw::LegacyUvAnimation::shield_layers,
            "shield material did not select the additive two-sided profile");

    // Lookup ignores case, directories, and the .fx/.fxo extension; skinning is a vertex feature.
    submesh.shader = "Shaders\\RSKINBUMPCOLORIZE.FX";
    const auto skinned = eaw::LegacyMaterial::translate(submesh);
    require(skinned.source == eaw::LegacyTranslationSource::reference_profile && skinned.family == eaw::LegacyMaterialFamily::mesh && skinned.vertex.rigid_skinning && skinned.vertex.tangent_frame &&
                skinned.features.colorize && skinned.features.gloss == eaw::LegacyGlossSource::normal_alpha && skinned.features.lighting == eaw::LegacyLightingModel::normal_mapped,
            "skinned bump-colorize profile was not applied");

    submesh.shader = "MeshHeat.fx";
    const auto heat = eaw::LegacyMaterial::translate(submesh);
    require(heat.family == eaw::LegacyMaterialFamily::heat && heat.render.phase == eaw::LegacyRenderPhase::heat && !heat.render.depth_write, "heat profile was not applied");

    submesh.shader = "Nebula.fx";
    const auto nebula = eaw::LegacyMaterial::translate(submesh);
    require(nebula.blend == eaw::LegacyBlendMode::additive && nebula.two_sided() && nebula.features.vertex_animation == eaw::LegacyVertexAnimation::nebula_wobble, "nebula profile was not applied");

    submesh.shader = "Tree.fx";
    const auto tree = eaw::LegacyMaterial::translate(submesh);
    require(tree.blend == eaw::LegacyBlendMode::alpha_test && tree.render.alpha_test_threshold && *tree.render.alpha_test_threshold > 0.5F && *tree.render.alpha_test_threshold < 0.51F &&
                tree.render.depth_write,
            "tree alpha-test profile was not applied");

    submesh.shader = "MeshOccludedUnit.fx";
    const auto occluded = eaw::LegacyMaterial::translate(submesh);
    require(occluded.render.depth_test == eaw::LegacyDepthTest::greater && occluded.render.stencil, "occluded-unit profile did not select a depth-greater test");

    submesh.shader = "MeshShadowVolume.fx";
    const auto shadow = eaw::LegacyMaterial::translate(submesh);
    require(shadow.family == eaw::LegacyMaterialFamily::shadow && !shadow.render.color_write && shadow.render.phase == eaw::LegacyRenderPhase::shadow,
            "shadow-volume profile did not disable color writes");

    // Planet cloud layers must not be mistaken for a second normal or base-color map.
    submesh.shader = "Planet.fx";
    submesh.parameters = {{"BaseTexture", eaw::AloShaderParameterType::texture, "P.TGA"},
                          {"NormalTexture", eaw::AloShaderParameterType::texture, "P_B.TGA"},
                          {"CloudTexture", eaw::AloShaderParameterType::texture, "C.TGA"},
                          {"CloudNormalTexture", eaw::AloShaderParameterType::texture, "C_B.TGA"}};
    const auto planet = eaw::LegacyMaterial::translate(submesh);
    require(planet.textures.size() == 4 && planet.textures[0].semantic == eaw::LegacyTextureSemantic::base_color && planet.textures[1].semantic == eaw::LegacyTextureSemantic::normal &&
                planet.textures[2].semantic == eaw::LegacyTextureSemantic::cloud && planet.textures[3].semantic == eaw::LegacyTextureSemantic::cloud_normal &&
                planet.features.lighting == eaw::LegacyLightingModel::planet_atmosphere,
            "planet texture semantics were not separated");

    // Shaders outside the reference corpus keep a conservative, explicitly tagged fallback.
    submesh.shader = "alDefault.fx";
    require(eaw::LegacyMaterial::translate(submesh).source == eaw::LegacyTranslationSource::name_heuristic, "unreferenced shader was not tagged as heuristic");
    submesh.shader = "ModRSkinAlphaGlow.fx";
    const auto modded = eaw::LegacyMaterial::translate(submesh);
    require(modded.source == eaw::LegacyTranslationSource::name_heuristic && modded.blend == eaw::LegacyBlendMode::alpha_blend && modded.vertex.rigid_skinning && !modded.render.depth_write,
            "mod shader heuristics were not applied");
    submesh.shader.clear();
    require(eaw::LegacyMaterial::translate(submesh).family == eaw::LegacyMaterialFamily::unknown, "empty shader name was not reported as unknown");

    const auto names = eaw::legacy_reference_shader_names();
    std::set<std::string_view> unique_names(names.begin(), names.end());
    require(names.size() == 39 && unique_names.size() == names.size(), "legacy reference profile names are missing or duplicated");
    for (const auto name : names)
    {
        submesh.shader = std::string(name) + ".fx";
        require(eaw::LegacyMaterial::translate(submesh).source == eaw::LegacyTranslationSource::reference_profile, "reference profile name did not resolve: " + std::string(name));
    }
}

void test_lua_inspection()
{
    const std::string source = "-- require('IGNORED')\n"
                               "require('PGStateMachine')\n"
                               "Script.Load(\"Story/Helper\")\n"
                               "function On_Enter_State()\n"
                               "  Find_First_Object('Executor')\n"
                               "  Controller:Advance()\n"
                               "end\n";
    const auto source_info = expect_ok(eaw::LuaScriptInfo::inspect(std::as_bytes(std::span(source))), "Lua source inspection");
    require(source_info.kind == eaw::LuaScriptKind::source && source_info.lines == 7 && source_info.dependencies.size() == 2 && source_info.dependencies[0] == "PGStateMachine" &&
                source_info.dependencies[1] == "Story/Helper" && source_info.defined_functions.size() == 1 && source_info.defined_functions[0] == "On_Enter_State" &&
                std::ranges::find(source_info.called_functions, "Find_First_Object") != source_info.called_functions.end() &&
                std::ranges::find(source_info.called_functions, "Controller:Advance") != source_info.called_functions.end(),
            "Lua source inspection failed");

    std::vector<std::byte> bytecode{std::byte{0x1b}, std::byte{'L'}, std::byte{'u'}, std::byte{'p'}, std::byte{0x51}, std::byte{1}, std::byte{4},
                                    std::byte{4},    std::byte{4},   std::byte{6},   std::byte{8},   std::byte{9},    std::byte{9}, std::byte{8}};
    bytecode.resize(bytecode.size() + 8U, std::byte{});
    const auto append_lua_string = [&](const std::string_view value)
    {
        append_u32(bytecode, static_cast<std::uint32_t>(value.size() + 1U));
        for (const auto character : value)
        {
            bytecode.push_back(static_cast<std::byte>(character));
        }
        bytecode.push_back(std::byte{});
    };
    append_lua_string("@TEST.LUA");
    append_u32(bytecode, 1);
    append_u32(bytecode, 42);
    bytecode.insert(bytecode.end(), 4U, std::byte{});
    append_u32(bytecode, 0);
    append_u32(bytecode, 0);
    append_u32(bytecode, 0);
    append_u32(bytecode, 2);
    bytecode.push_back(std::byte{3});
    bytecode.resize(bytecode.size() + 8U, std::byte{});
    bytecode.push_back(std::byte{4});
    append_lua_string("constant");
    append_u32(bytecode, 0);
    append_u32(bytecode, 1);
    append_u32(bytecode, 0);

    const auto bytecode_info = expect_ok(eaw::LuaScriptInfo::inspect(bytecode), "Lua bytecode inspection");
    require(bytecode_info.kind == eaw::LuaScriptKind::petroglyph_bytecode && bytecode_info.bytecode_header.version == 0x51 && bytecode_info.prototypes == 1 && bytecode_info.instructions == 1 &&
                bytecode_info.number_constants == 1 && bytecode_info.string_constants == 1 && bytecode_info.persistent_function_ids == std::vector<std::uint64_t>{42},
            "Petroglyph Lua bytecode inspection failed");
}

void test_ala_animation()
{
    const auto animation = expect_ok(eaw::AlaAnimation::parse(make_test_ala()), "ALA parse");
    require(animation.version == 2 && animation.stored_frame_count == 31 && animation.playable_frame_count() == 30 && animation.frames_per_second == 30.0F && animation.duration_seconds() == 1.0F,
            "ALA animation header decoding failed");
    require(animation.tracks.size() == 1 && animation.tracks[0].name == "ROOT" && animation.tracks[0].translation && animation.tracks[0].rotation && !animation.tracks[0].scale &&
                animation.tracks[0].visibility && animation.tracks[0].frames.size() == 31,
            "ALA bone track decoding failed");
    const auto pose = animation.sample_pose(expect_ok(eaw::AloModel::parse(make_test_alo()), "ALO parse"), 0.5F);
    require(pose.size() == 1 && pose[0].visible, "ALA sampled pose decoding failed");
}

// Local poses: transforms decompose and recompose, local sampling composes to the same pose as
// sample_pose, and blending moves between poses by weight.
void test_ala_blending()
{
    const auto near = [](const eaw::AloMatrix4& a, const eaw::AloMatrix4& b, const float tolerance)
    {
        for (std::size_t index = 0; index < 16; ++index)
        {
            if (std::fabs(a.values[index] - b.values[index]) > tolerance)
            {
                return false;
            }
        }
        return true;
    };
    // Rotations about several axes (including near 180 degrees, which takes the non-trace
    // branches), with scale and translation, and a mirrored matrix.
    const std::array<eaw::AloQuaternion, 5> rotations{{
        {0.0F, 0.0F, 0.0F, 1.0F},
        {0.2F, -0.5F, 0.3F, 0.787401F},
        {0.99F, 0.1F, 0.0F, 0.0995F},
        {0.05F, 0.998F, 0.02F, 0.03F},
        {0.0F, 0.1F, 0.995F, 0.0F},
    }};
    for (auto rotation : rotations)
    {
        const auto length = std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w);
        rotation = {rotation.x / length, rotation.y / length, rotation.z / length, rotation.w / length};
        for (const auto scale : {eaw::AloVector3{1.0F, 1.0F, 1.0F}, eaw::AloVector3{2.0F, 0.5F, 3.0F}, eaw::AloVector3{-1.5F, 1.0F, 2.0F}})
        {
            const auto matrix = eaw::alo_compose_transform(scale, rotation, {4.0F, -5.0F, 6.0F});
            const auto parts = eaw::alo_decompose_transform(matrix);
            require(near(eaw::alo_compose_transform(parts.scale, parts.rotation, parts.translation), matrix, 1.0e-4F), "a transform must recompose from its parts");
        }
    }

    const auto model = expect_ok(eaw::AloModel::parse(make_test_alo()), "ALO parse");
    const auto animation = expect_ok(eaw::AlaAnimation::parse(make_test_ala()), "ALA parse");
    for (const auto seconds : {0.0F, 0.25F, 0.5F, 1.75F, -0.3F})
    {
        const auto direct = animation.sample_pose(model, seconds);
        const auto composed = eaw::ala_compose_pose(model, animation.sample_local_pose(model, seconds));
        require(direct.size() == composed.size() && near(direct[0].transform, composed[0].transform, 1.0e-4F) && direct[0].visible == composed[0].visible,
                "a composed local pose must match sample_pose");
    }
    const auto bind = eaw::ala_compose_pose(model, eaw::ala_bind_local_pose(model));
    require(near(bind[0].transform, model.bind_pose()[0], 1.0e-4F), "the bind local pose must compose to the bind pose");

    // Identity versus 90 degrees about Z (with the sign flipped, to take the shorter arc), moved
    // and scaled: halfway is 45 degrees, halfway translation and scale.
    const auto half_turn = std::sqrt(0.5F);
    std::vector<eaw::AlaLocalBone> pose{{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, true}};
    const std::vector<eaw::AlaLocalBone> turned{{{2.0F, 4.0F, -6.0F}, {3.0F, 3.0F, 3.0F}, {0.0F, 0.0F, -half_turn, -half_turn}, false}};
    eaw::ala_blend_pose(pose, turned, 0.0F);
    eaw::ala_blend_pose(pose, turned, std::numeric_limits<float>::quiet_NaN());
    require(pose[0].translation.x == 0.0F && pose[0].rotation.w == 1.0F && pose[0].visible, "zero and non-finite weights must keep the pose");
    eaw::ala_blend_pose(pose, turned, 0.5F);
    const auto& q = pose[0].rotation;
    const auto angle = 2.0F * std::atan2(q.z, q.w);
    require(std::fabs(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1.0F) < 1.0e-5F && std::fabs(angle - std::numbers::pi_v<float> / 4.0F) < 1.0e-4F,
            "a half weight must turn halfway along the shorter arc and stay normalized");
    require(pose[0].translation.y == 2.0F && pose[0].scale.x == 2.0F && pose[0].visible, "a half weight must land halfway and keep the heavier side's visibility");
    eaw::ala_blend_pose(pose, turned, 1.0F);
    require(near(eaw::alo_compose_transform(pose[0].scale, pose[0].rotation, pose[0].translation), eaw::alo_compose_transform(turned[0].scale, turned[0].rotation, turned[0].translation), 1.0e-5F) &&
                !pose[0].visible,
            "a full weight must take the target pose");
    // A shorter target blends over the common bones and leaves the rest.
    std::vector<eaw::AlaLocalBone> empty;
    eaw::ala_blend_pose(pose, empty, 1.0F);
    require(pose.size() == 1, "blending with an empty pose must keep the bones");
}

void test_ted_map()
{
    const auto map = expect_ok(eaw::TedMap::parse(make_test_ted()), "TED parse");
    require(map.type == eaw::TedMapType::land && map.players == 2 && map.name == "TEXT_TEST_MAP" && map.planet == "TEST_PLANET", "TED property decoding failed");
    require(map.terrain_width == 3 && map.terrain_height == 2 && map.elevation_sample_count == 6 && map.minimum_elevation == -10 && map.maximum_elevation == 40 && map.passability_sample_count == 6,
            "TED terrain decoding failed");
    require(map.elevation_map == std::vector<std::int16_t>({-10, 0, 10, 20, 30, 40}) && map.passability_map == std::vector<std::uint32_t>({0, 2, 4, 1, 3, 5}),
            "TED terrain sample retention or passability alignment failed");
    require(map.water.type == eaw::TedWaterType::water && map.water.raw_type == 1 && map.water.level == 1.5F && map.water.color == std::array<float, 3>{0.4F, 0.6F, 0.8F} &&
                map.water_maps[0] == "WATER_BUMP.TGA" && map.water_maps[1] == "WATER.TGA" && map.terrain_info_fields.size() == 8,
            "TED water settings were not decoded");
    require(map.tracks.size() == 1 && map.tracks[0].texture == "RIVER.TGA" && map.tracks[0].points.size() == 2 && map.tracks[0].points[1] == std::array<float, 3>{35.0F, 15.0F, 0.0F} &&
                map.tracks[0].widths == std::vector<float>{10.0F, 6.0F} && map.tracks[0].scroll == std::array<float, 2>{0.1F, 0.2F} &&
                map.tracks[0].opacity == std::array<float, 3>{0.0F, 1.0F, 0.0F} && map.tracks[0].header_fields.size() == 2 && map.track_textures.size() == 1,
            "TED track decoding failed");
    require(map.terrain_layer_map == std::vector<std::uint8_t>({0, 0, 1, 1, 0, 1}) && map.terrain_samples.size() == 6 && (map.terrain_samples[0] >> 24U) == 0xffU,
            "TED terrain layer indices or raw samples were not retained");
    require(map.terrain_layers.size() == 3 && map.terrain_layers[2].color_texture.empty() && map.terrain_layers[0].tile_size == 64.0F && map.terrain_layers[0].fields.size() == 3 &&
                map.terrain_layers[1].tile_size == 0.0F,
            "TED terrain layer tiling or raw fields were not decoded");
    require(map.terrain_layers[0].color_texture == "GROUND.TGA" && map.environments.size() == 2 && map.environments[0].name == "Noon_Clear" && map.environments[0].skybox_primary == "DAY_BLUE_SKY" &&
                map.active_environment == 1 && map.environments[0].has_lighting && map.environments[0].lights[0].color == std::array<float, 3>{1.0F, 0.9F, 0.8F} &&
                map.environments[0].lights[2].intensity == 0.25F && map.environments[0].lights[1].azimuth == 3.0F && map.environments[0].lights[0].elevation == 0.4F &&
                map.environments[0].ambient[0] == 0.05F && map.environments[0].fields.size() == 17 && map.environments[0].fields.back().type == 0x12 &&
                map.environments[0].fields.back().payload.size() == 4 && !map.environments[1].has_lighting && map.warnings.size() == 1,
            "TED dependency decoding failed");
    require(map.object_count == 1 && map.object_types.size() == 1 && map.object_types[0].crc == 0x12345678U && map.zone_count == 1, "TED object or zone decoding failed");
    require(map.placed_objects.size() == 1 && map.placed_objects[0].instance_id == 42U && map.placed_objects[0].crc == 0x12345678U && map.placed_objects[0].player == 3U &&
                map.placed_objects[0].scale == 1.5F && map.placed_objects[0].position.x == 100.0F && map.placed_objects[0].position.y == 25.0F && map.placed_objects[0].position.z == -50.0F &&
                map.placed_objects[0].rotation.y == 90.0F && map.placed_objects[0].has_transform,
            "TED placed-object transform decoding failed");
}

void test_dat_and_workspace()
{
    const auto original = make_test_dat();
    auto dat = expect_ok(eaw::DatFile::parse(original), "DAT parse");
    require(dat.entries().size() == 2, "DAT entry count failed");
    require(dat.find("TEXT_UNIT_TEST") != nullptr, "DAT key lookup failed");
    require(expect_ok(dat.serialize(), "DAT serialize") == original, "DAT byte-identical round trip failed");
    require(dat.validate().empty(), "valid DAT reported issues");
    require(eaw::dat_key_crc("TEXT_SPEECH_EMPIRE_ACT01_MISSION_THREE_62") == 0x0001db80U, "Petroglyph DAT CRC32 failed");

    expect_ok(dat.upsert("TEXT_UNIT_NEW", expect_ok(eaw::utf8_to_utf16("Žluťoučký 🚀"), "UTF-8 to UTF-16")), "DAT upsert");
    require(expect_ok(eaw::utf16_to_utf8(dat.find("TEXT_UNIT_NEW")->value), "UTF-16 to UTF-8") == "Žluťoučký 🚀", "UTF-8/UTF-16 conversion failed");
    require(!dat.upsert("", u"value").ok(), "an empty DAT key was accepted");
    require(!eaw::utf8_to_utf16("\xc3").ok() && !eaw::utf16_to_utf8(std::u16string(1, char16_t{0xd800})).ok(), "invalid UTF-8/UTF-16 was accepted");

    const auto workspace = std::filesystem::temp_directory_path() / "eawengine-localization-test.tsv";
    std::error_code ignored;
    std::filesystem::remove(workspace, ignored);
    const std::vector<eaw::LocalizationRow> rows{{"TEXT_UNIT_NEW", "Name\twith tab", "unit:new", "ready", "Data/XML/Test.xml:2", "A \"quoted\" note\nwith a new line"}};
    expect_ok(eaw::write_localization_workspace(workspace, rows), "writing the workspace");
    const auto loaded = expect_ok(eaw::read_localization_workspace(workspace), "reading the workspace");
    require(loaded.size() == 1 && loaded[0].text == rows[0].text && loaded[0].notes == rows[0].notes, "localization TSV round trip failed");

    auto resolved_todo = expect_ok(eaw::DatFile::parse(original), "DAT parse");
    expect_ok(eaw::apply_localization_workspace(resolved_todo, {{"TEXT_UNIT_TEST", "Completed TODO value", "unit:test", "todo", {}, {}}}), "applying a completed TODO row");
    require(expect_ok(eaw::utf16_to_utf8(resolved_todo.find("TEXT_UNIT_TEST")->value), "UTF-16 to UTF-8") == "Completed TODO value", "completed TODO localization value was skipped");

    auto unresolved_todo = expect_ok(eaw::DatFile::parse(original), "DAT parse");
    const auto unresolved = eaw::apply_localization_workspace(unresolved_todo, {{"TEXT_UNIT_EMPTY", {}, "unit:empty", "todo", {}, {}}});
    require(!unresolved.ok() && unresolved.error().message.find("unresolved TODO localization entry: TEXT_UNIT_EMPTY") != std::string::npos, "empty TODO localization row was not rejected");
    expect_ok(eaw::apply_localization_workspace(unresolved_todo, {{"TEXT_UNIT_EMPTY", {}, "unit:empty", "todo", {}, {}}}, true), "skipping an empty TODO row");
    require(unresolved_todo.find("TEXT_UNIT_EMPTY") == nullptr, "empty TODO localization row was not skipped");
    std::filesystem::remove(workspace, ignored);
}

void test_localization_scan()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-localization-scan-test";
    const auto xml_root = root / "Data" / "XML";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(xml_root);
    {
        std::ofstream xml(xml_root / "Units.xml", std::ios::binary);
        xml << "<Units>\n"
               "  <Text_ID>TEXT_UNIT_TEST</Text_ID>\n"
               "  <Text_ID>TEXT_UNIT_MISSING</Text_ID>\n"
               "  <!-- TEXT_COMMENTED_OUT -->\n"
               "</Units>\n";
    }

    const auto baseline = expect_ok(eaw::DatFile::parse(make_test_dat()), "DAT parse");
    const auto result = expect_ok(eaw::scan_mod_localization(root, baseline), "localization scan");
    require(result.reference_count == 2, "localization scan reference count failed");
    require(result.missing.size() == 1 && result.missing.front().key == "TEXT_UNIT_MISSING" && result.missing.front().context.find("Units.xml:3") != std::string::npos,
            "localization scan missing-key context failed");
    std::filesystem::remove_all(root, ignored);
}

const ModuleRegistrar registrar("formats", {
                                               {"test_alo_model", &test_alo_model},
                                               {"test_alo_default_mesh_visibility", &test_alo_default_mesh_visibility},
                                               {"test_texture_formats_and_materials", &test_texture_formats_and_materials},
                                               {"test_lua_inspection", &test_lua_inspection},
                                               {"test_ala_animation", &test_ala_animation},
                                               {"test_ala_blending", &test_ala_blending},
                                               {"test_ted_map", &test_ted_map},
                                               {"test_dat_and_workspace", &test_dat_and_workspace},
                                               {"test_localization_scan", &test_localization_scan},
                                           });

} // namespace
