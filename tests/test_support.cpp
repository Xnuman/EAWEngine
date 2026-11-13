#include "test_support.hpp"

namespace eaw_test {

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

ModuleRegistrar::ModuleRegistrar(const char* module, const std::initializer_list<std::pair<const char*, void (*)()>> tests)
{
    for (const auto& [name, run] : tests)
    {
        registry().push_back({module, name, run});
    }
}

// A failed requirement ends the run: the remaining tests may depend on what failed.
void require(const bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "test failure: " << message << '\n';
        std::exit(1);
    }
}

void expect_ok(const eaw::Status& status, const std::string& what)
{
    require(status.ok(), what + ": " + (status.ok() ? std::string{} : status.error().message));
}

void write_test_meg(const std::filesystem::path& path, const std::vector<std::pair<std::string, std::string>>& entries)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(stream), "could not create synthetic MEG archive");
    write_little_endian<std::uint32_t>(stream, static_cast<std::uint32_t>(entries.size()));
    write_little_endian<std::uint32_t>(stream, static_cast<std::uint32_t>(entries.size()));
    std::uint32_t payload_offset = 8U + static_cast<std::uint32_t>(entries.size() * 20U);
    for (const auto& [name, payload] : entries)
    {
        (void)payload;
        require(name.size() <= std::numeric_limits<std::uint16_t>::max(), "synthetic MEG filename is too long");
        write_little_endian<std::uint16_t>(stream, static_cast<std::uint16_t>(name.size()));
        stream.write(name.data(), static_cast<std::streamsize>(name.size()));
        payload_offset += 2U + static_cast<std::uint32_t>(name.size());
    }
    auto next_offset = payload_offset;
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        const auto& payload = entries[index].second;
        write_little_endian<std::uint32_t>(stream, 0);
        write_little_endian<std::uint32_t>(stream, static_cast<std::uint32_t>(index));
        write_little_endian<std::uint32_t>(stream, static_cast<std::uint32_t>(payload.size()));
        write_little_endian<std::uint32_t>(stream, next_offset);
        write_little_endian<std::uint32_t>(stream, static_cast<std::uint32_t>(index));
        next_offset += static_cast<std::uint32_t>(payload.size());
    }
    for (const auto& [name, payload] : entries)
    {
        (void)name;
        stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }
    stream.close();
}

std::filesystem::path write_test_meg()
{
    const auto path = std::filesystem::temp_directory_path() / "eawengine-meg-reader-test.meg";
    write_test_meg(path, {{"DATA\\XML\\TEST.XML", "<Test />"}});
    return path;
}

std::string read_vfs_text(const eaw::VirtualFileSystem& vfs, const std::string_view path)
{
    const auto bytes = expect_ok(vfs.read(path), std::string(path));
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

void append_float(std::vector<std::byte>& bytes, const float value)
{
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

void append_u16(std::vector<std::byte>& bytes, const std::uint16_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void append_string(std::vector<std::byte>& bytes, const std::string_view value)
{
    for (const char character : value)
    {
        bytes.push_back(static_cast<std::byte>(character));
    }
    bytes.push_back(std::byte{});
}

void append_wide_string(std::vector<std::byte>& bytes, const std::u16string_view value)
{
    for (const auto character : value)
    {
        append_u16(bytes, static_cast<std::uint16_t>(character));
    }
    append_u16(bytes, 0);
}

void append_chunk(std::vector<std::byte>& target, const std::uint32_t type, const std::vector<std::byte>& payload, const bool container)
{
    append_u32(target, type);
    append_u32(target, static_cast<std::uint32_t>(payload.size()) | (container ? 0x80000000U : 0U));
    target.insert(target.end(), payload.begin(), payload.end());
}

void append_mini(std::vector<std::byte>& target, const std::uint8_t type, const std::vector<std::byte>& payload)
{
    require(payload.size() <= 255, "test mini-chunk payload is too large");
    target.push_back(static_cast<std::byte>(type));
    target.push_back(static_cast<std::byte>(payload.size()));
    target.insert(target.end(), payload.begin(), payload.end());
}

std::vector<std::byte> make_test_alo(const bool root_visible)
{
    std::vector<std::byte> bone_name;
    append_string(bone_name, "ROOT");
    std::vector<std::byte> bone_data;
    append_u32(bone_data, 0xffffffffU);
    append_u32(bone_data, root_visible ? 1U : 0U);
    append_u32(bone_data, 0);
    for (int index = 0; index < 12; ++index)
    {
        append_float(bone_data, index == 0 || index == 5 || index == 10 ? 1.0F : 0.0F);
    }
    std::vector<std::byte> bone;
    append_chunk(bone, 0x203, bone_name);
    append_chunk(bone, 0x206, bone_data);
    std::vector<std::byte> skeleton;
    std::vector<std::byte> bone_count;
    append_u32(bone_count, 1);
    append_chunk(skeleton, 0x201, bone_count);
    append_chunk(skeleton, 0x202, bone, true);

    std::vector<std::byte> shader_name;
    append_string(shader_name, "MeshGloss.fx");
    std::vector<std::byte> material;
    append_chunk(material, 0x10101, shader_name);
    // A well-formed vector4 parameter and a vector3 whose data chunk is too short.
    const auto append_parameter = [&](const std::uint32_t type, const std::string& name, const std::vector<float>& data)
    {
        std::vector<std::byte> name_bytes;
        append_string(name_bytes, name);
        std::vector<std::byte> data_bytes;
        for (const auto value : data)
        {
            append_float(data_bytes, value);
        }
        std::vector<std::byte> minis;
        append_mini(minis, 1, name_bytes);
        append_mini(minis, 2, data_bytes);
        append_chunk(material, type, minis);
    };
    append_parameter(0x10106, "Diffuse", {0.25F, 0.5F, 0.75F, 0.123456789F});
    append_parameter(0x10104, "Specular", {1.0F, 2.0F});

    std::vector<std::byte> submesh_info(128, std::byte{});
    submesh_info.clear();
    append_u32(submesh_info, 3);
    append_u32(submesh_info, 1);
    submesh_info.resize(128, std::byte{});
    std::vector<std::byte> vertex_format;
    append_string(vertex_format, "PNT1");
    std::vector<std::byte> vertices(3U * 144U, std::byte{});
    std::vector<std::byte> indices(6, std::byte{});
    std::vector<std::byte> submesh;
    append_chunk(submesh, 0x10001, submesh_info);
    append_chunk(submesh, 0x10002, vertex_format);
    append_chunk(submesh, 0x10007, vertices);
    append_chunk(submesh, 0x10004, indices);

    std::vector<std::byte> mesh_name;
    append_string(mesh_name, "TEST_MESH");
    std::vector<std::byte> mesh_info;
    append_u32(mesh_info, 1);
    for (const float value : {-1.0F, -2.0F, -3.0F, 1.0F, 2.0F, 3.0F})
    {
        append_float(mesh_info, value);
    }
    append_u32(mesh_info, 0);
    append_u32(mesh_info, 0);
    append_u32(mesh_info, 0);
    mesh_info.resize(128, std::byte{});
    std::vector<std::byte> mesh;
    append_chunk(mesh, 0x401, mesh_name);
    append_chunk(mesh, 0x402, mesh_info);
    append_chunk(mesh, 0x10100, material, true);
    append_chunk(mesh, 0x10000, submesh, true);

    std::vector<std::byte> count_minis;
    std::vector<std::byte> one;
    append_u32(one, 1);
    std::vector<std::byte> zero;
    append_u32(zero, 0);
    append_mini(count_minis, 1, one);
    append_mini(count_minis, 4, zero);
    std::vector<std::byte> connection_minis;
    append_mini(connection_minis, 2, zero);
    append_mini(connection_minis, 3, zero);
    std::vector<std::byte> connections;
    append_chunk(connections, 0x601, count_minis);
    append_chunk(connections, 0x602, connection_minis);

    std::vector<std::byte> alo;
    append_chunk(alo, 0x200, skeleton, true);
    append_chunk(alo, 0x400, mesh, true);
    append_chunk(alo, 0x600, connections, true);
    return alo;
}

// A minimal 4x4 BC1 DDS with one mip level.
std::vector<std::byte> make_test_dds()
{
    std::vector<std::byte> dds(128U + 8U, std::byte{});
    const auto write_u32 = [](std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            bytes[offset + shift / 8] = static_cast<std::byte>((value >> shift) & 0xffU);
        }
    };
    write_u32(dds, 0, 0x20534444U);
    write_u32(dds, 4, 124);
    write_u32(dds, 12, 4);
    write_u32(dds, 16, 4);
    write_u32(dds, 28, 1);
    write_u32(dds, 76, 32);
    write_u32(dds, 80, 0x4U);
    write_u32(dds, 84, 0x31545844U);
    return dds;
}

std::vector<std::byte> make_test_ala()
{
    std::vector<std::byte> info_minis;
    std::vector<std::byte> frames;
    append_u32(frames, 31);
    append_mini(info_minis, 1, frames);
    std::vector<std::byte> fps;
    append_float(fps, 30.0F);
    append_mini(info_minis, 2, fps);
    std::vector<std::byte> one;
    append_u32(one, 1);
    append_mini(info_minis, 3, one);
    std::vector<std::byte> rotation_words;
    append_u32(rotation_words, 4);
    append_mini(info_minis, 11, rotation_words);
    std::vector<std::byte> translation_words;
    append_u32(translation_words, 3);
    append_mini(info_minis, 12, translation_words);
    std::vector<std::byte> scale_words;
    append_u32(scale_words, 0);
    append_mini(info_minis, 13, scale_words);

    std::vector<std::byte> track_minis;
    std::vector<std::byte> track_name;
    append_string(track_name, "ROOT");
    append_mini(track_minis, 4, track_name);
    std::vector<std::byte> bone_index;
    append_u32(bone_index, 0);
    append_mini(track_minis, 5, bone_index);
    std::vector<std::byte> active;
    append_u16(active, 0);
    std::vector<std::byte> inactive;
    append_u16(inactive, 0xffffU);
    append_mini(track_minis, 14, active);
    append_mini(track_minis, 15, inactive);
    append_mini(track_minis, 16, active);

    std::vector<std::byte> track;
    append_chunk(track, 0x1003, track_minis);
    append_chunk(track, 0x1007, {std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0x7f}});

    std::vector<std::byte> animation;
    append_chunk(animation, 0x1001, info_minis);
    append_chunk(animation, 0x1002, track, true);
    append_chunk(animation, 0x100a, std::vector<std::byte>(31U * 3U * 2U, std::byte{}));
    append_chunk(animation, 0x1009, std::vector<std::byte>(31U * 4U * 2U, std::byte{}));
    std::vector<std::byte> ala;
    append_chunk(ala, 0x1000, animation, true);
    return ala;
}

std::vector<std::byte> make_test_ted()
{
    std::vector<std::byte> properties;
    std::vector<std::byte> value;
    append_u32(value, 1);
    append_mini(properties, 1, value);
    value.clear();
    append_u32(value, 2);
    append_mini(properties, 2, value);
    value.clear();
    append_wide_string(value, u"TEXT_TEST_MAP");
    append_mini(properties, 8, value);
    value.clear();
    append_wide_string(value, u"TEST_PLANET");
    append_mini(properties, 9, value);

    std::vector<std::byte> environment_data;
    value.clear();
    append_string(value, "Noon_Clear");
    append_mini(environment_data, 0x14, value);
    value.clear();
    append_string(value, "DAY_BLUE_SKY");
    append_mini(environment_data, 0x19, value);
    // Complete lighting block: key/fill/fill colors, specular, ambient, intensities, azimuths,
    // elevations (mini types 0x00-0x0d), plus fog (0x0f-0x12).
    const auto append_floats = [&](const std::uint8_t type, const std::vector<float>& floats)
    {
        std::vector<std::byte> payload;
        for (const auto component : floats)
        {
            append_float(payload, component);
        }
        append_mini(environment_data, type, payload);
    };
    append_floats(0x00, {1.0F, 0.9F, 0.8F});
    append_floats(0x01, {0.1F, 0.2F, 0.3F});
    append_floats(0x02, {0.2F, 0.2F, 0.4F});
    append_floats(0x03, {0.7F, 0.7F, 0.7F});
    append_floats(0x04, {0.05F, 0.05F, 0.05F});
    append_floats(0x05, {0.5F});
    append_floats(0x06, {0.4F});
    append_floats(0x07, {0.25F});
    append_floats(0x08, {1.5F});
    append_floats(0x09, {3.0F});
    append_floats(0x0a, {4.5F});
    append_floats(0x0b, {0.4F});
    append_floats(0x0c, {-0.3F});
    append_floats(0x0d, {-0.2F});
    append_floats(0x0f, {0.6F, 0.5F, 0.4F});
    append_mini(environment_data, 0x10, std::vector<std::byte>{std::byte{1}});
    append_floats(0x11, {500.0F});
    append_floats(0x12, {3500.0F});
    std::vector<std::byte> environment_list;
    append_chunk(environment_list, 6, environment_data);
    std::vector<std::byte> night_environment;
    value.clear();
    append_string(value, "Night");
    append_mini(night_environment, 0x14, value);
    append_chunk(environment_list, 6, night_environment);
    std::vector<std::byte> environments;
    append_chunk(environments, 4, environment_list, true);
    // The active-environment index is stored as a mini-chunk, as in stock maps.
    value.clear();
    append_u32(value, 1);
    std::vector<std::byte> active_environment;
    append_mini(active_environment, 0x25, value);
    append_chunk(environments, 8, active_environment);

    std::vector<std::byte> terrain_info;
    value.clear();
    append_u32(value, 3);
    append_mini(terrain_info, 0, value);
    value.clear();
    append_u32(value, 2);
    append_mini(terrain_info, 1, value);
    value.clear();
    append_u32(value, 1);
    append_mini(terrain_info, 5, value);
    // Water: level 0x15 (world units), type 0x16 (1 = water), color 0x1a, textures 0x1d/0x1e.
    value.clear();
    append_float(value, 1.5F);
    append_mini(terrain_info, 0x15, value);
    value.clear();
    append_u32(value, 1);
    append_mini(terrain_info, 0x16, value);
    value.clear();
    append_float(value, 0.4F);
    append_float(value, 0.6F);
    append_float(value, 0.8F);
    append_mini(terrain_info, 0x1a, value);
    value.clear();
    append_string(value, "WATER_BUMP.TGA");
    append_mini(terrain_info, 0x1d, value);
    value.clear();
    append_string(value, "WATER.TGA");
    append_mini(terrain_info, 0x1e, value);
    // Stock samples pack the signed elevation in bits 0-15, the layer index in bits 16-23, and a
    // small signed value (usually -1) in bits 24-31.
    std::vector<std::byte> elevation;
    const std::array<int, 6> heights{-10, 0, 10, 20, 30, 40};
    const std::array<std::uint32_t, 6> sample_layers{0, 0, 1, 1, 0, 1};
    for (std::size_t index = 0; index < heights.size(); ++index)
    {
        append_u32(elevation, (static_cast<std::uint32_t>(heights[index]) & 0xffffU) | (sample_layers[index] << 16U) | (0xffU << 24U));
    }
    std::vector<std::byte> layer_data;
    value.clear();
    append_string(value, "GROUND.TGA");
    append_mini(layer_data, 12, value);
    value.clear();
    append_string(value, "GROUND_B.TGA");
    append_mini(layer_data, 19, value);
    value.clear();
    append_float(value, 64.0F);
    append_mini(layer_data, 6, value);
    std::vector<std::byte> layers;
    append_chunk(layers, 3, layer_data);
    std::vector<std::byte> second_layer;
    value.clear();
    append_string(value, "ROCK.TGA");
    append_mini(second_layer, 12, value);
    append_chunk(layers, 3, second_layer);
    // A third, untextured layer (stock maps use these for terrain that is not drawn).
    std::vector<std::byte> empty_layer;
    value.clear();
    append_string(value, "");
    append_mini(empty_layer, 12, value);
    append_chunk(layers, 3, empty_layer);
    // One track: header (scroll 0.1/0.2), two points, texture, two widths, opacity keys 0/1/0.
    std::vector<std::byte> track_header;
    value.clear();
    append_float(value, 0.1F);
    append_mini(track_header, 0x04, value);
    value.clear();
    append_float(value, 0.2F);
    append_mini(track_header, 0x05, value);
    std::vector<std::byte> track_points;
    for (const auto& point : {eaw::Vector3{5.0F, 5.0F, 0.0F}, eaw::Vector3{35.0F, 15.0F, 0.0F}})
    {
        value.clear();
        append_float(value, point.x);
        append_float(value, point.y);
        append_float(value, point.z);
        append_mini(track_points, 0x07, value);
    }
    std::vector<std::byte> track_widths;
    for (const auto width : {10.0F, 6.0F})
    {
        value.clear();
        append_float(value, width);
        append_mini(track_widths, 0x08, value);
    }
    std::vector<std::byte> track_opacity;
    for (const auto key : {0.0F, 1.0F, 0.0F})
    {
        value.clear();
        append_float(value, key);
        append_mini(track_opacity, 0x09, value);
    }
    value.clear();
    append_string(value, "RIVER.TGA");
    std::vector<std::byte> ted_track;
    append_chunk(ted_track, 0x101, track_header);
    append_chunk(ted_track, 0x103, value);
    append_chunk(ted_track, 0x102, track_points);
    append_chunk(ted_track, 0x104, track_widths);
    append_chunk(ted_track, 0x105, track_opacity);
    std::vector<std::byte> ted_tracks;
    append_chunk(ted_tracks, 0x100, ted_track, true);

    std::vector<std::byte> terrain;
    append_chunk(terrain, 0, terrain_info);
    append_chunk(terrain, 5, elevation);
    append_chunk(terrain, 2, layers, true);
    append_chunk(terrain, 9, ted_tracks, true);

    std::vector<std::byte> identity_data;
    value.clear();
    append_u32(value, 42U);
    append_mini(identity_data, 0, value);
    value.clear();
    append_u32(value, 0x12345678U);
    append_mini(identity_data, 1, value);
    value.clear();
    append_u32(value, 3U);
    append_mini(identity_data, 2, value);
    value.clear();
    append_float(value, 1.5F);
    append_mini(identity_data, 3, value);
    value.clear();
    append_float(value, 100.0F);
    append_float(value, 25.0F);
    append_float(value, -50.0F);
    append_mini(identity_data, 4, value);
    value.clear();
    append_float(value, 0.0F);
    append_float(value, 90.0F);
    append_float(value, 0.0F);
    append_mini(identity_data, 5, value);
    std::vector<std::byte> identity;
    append_chunk(identity, 0x4b0, identity_data);
    std::vector<std::byte> identity_group;
    append_chunk(identity_group, 0x459, identity, true);
    std::vector<std::byte> object_body;
    append_chunk(object_body, 0x454, identity_group, true);
    // The hint, NUL-terminated as in the stock maps.
    std::vector<std::byte> hint_text;
    for (const char character : std::string_view("gate\0", 5))
    {
        hint_text.push_back(static_cast<std::byte>(character));
    }
    std::vector<std::byte> hint_minis;
    append_mini(hint_minis, 1, hint_text);
    std::vector<std::byte> hint_part;
    append_chunk(hint_part, 0, hint_minis);
    append_chunk(object_body, 0x482, hint_part, true);
    std::vector<std::byte> object_list;
    append_chunk(object_list, 0x44c, object_body, true);
    std::vector<std::byte> objects;
    append_chunk(objects, 1, object_list, true);

    std::vector<std::byte> zones;
    append_chunk(zones, 0, {}, true);
    std::vector<std::byte> passability;
    for (int index = 0; index < 6; ++index)
    {
        append_u32(passability, static_cast<std::uint32_t>(index));
    }

    std::vector<std::byte> root;
    append_chunk(root, 0x100, environments, true);
    append_chunk(root, 0x101, terrain, true);
    append_chunk(root, 0x10a, passability);
    append_chunk(root, 0x102, objects, true);
    append_chunk(root, 0x10b, zones, true);
    std::vector<std::byte> ted;
    append_chunk(ted, 0, properties);
    append_chunk(ted, 1, root, true);
    return ted;
}

std::vector<std::byte> make_test_dat()
{
    const std::string first_key = "TEXT_UNIT_TEST";
    const std::string second_key = "TEXT_TOOLTIP_TEST";
    const std::u16string first_value = u"Test Unit";
    const std::u16string second_value = u"Line one\nLine two";
    std::vector<std::byte> bytes;
    append_u32(bytes, 2);
    append_u32(bytes, eaw::dat_key_crc(first_key));
    append_u32(bytes, static_cast<std::uint32_t>(first_value.size()));
    append_u32(bytes, static_cast<std::uint32_t>(first_key.size()));
    append_u32(bytes, eaw::dat_key_crc(second_key));
    append_u32(bytes, static_cast<std::uint32_t>(second_value.size()));
    append_u32(bytes, static_cast<std::uint32_t>(second_key.size()));
    for (const auto& value : {first_value, second_value})
    {
        for (const char16_t character : value)
        {
            bytes.push_back(static_cast<std::byte>(character & 0xffU));
            bytes.push_back(static_cast<std::byte>((character >> 8U) & 0xffU));
        }
    }
    for (const auto& key : {first_key, second_key})
    {
        for (const unsigned char character : key)
        {
            bytes.push_back(static_cast<std::byte>(character));
        }
    }
    return bytes;
}

namespace {

std::vector<std::byte> particle_bytes(std::initializer_list<std::uint8_t> values)
{
    std::vector<std::byte> bytes;
    for (const auto value : values)
    {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

std::vector<std::byte> particle_u32(const std::uint32_t value)
{
    std::vector<std::byte> bytes;
    append_u32(bytes, value);
    return bytes;
}

std::vector<std::byte> particle_float(const float value)
{
    std::vector<std::byte> bytes;
    append_float(bytes, value);
    return bytes;
}

// One emitter: properties, texture, name, three shape groups, seven tracks, and optional links.
std::vector<std::byte> make_test_particle_emitter(const std::string_view name, const std::int32_t death_child, const bool unknown_property, const bool short_group)
{
    std::vector<std::byte> properties;
    append_mini(properties, 0x04, particle_u32(2));
    append_mini(properties, 0x05, particle_u32(1));
    append_mini(properties, 0x07, particle_bytes({1}));
    append_mini(properties, 0x0F, particle_float(2.5F));
    append_mini(properties, 0x26, particle_u32(8));
    append_mini(properties, 0x27, particle_u32(0xffffffffU));
    append_mini(properties, 0x28, particle_float(0.75F));
    append_mini(properties, 0x41, particle_bytes({1}));
    append_mini(properties, 0x14, particle_u32(3));
    if (unknown_property)
    {
        append_mini(properties, 0x7E, particle_bytes({9, 9}));
    }

    std::vector<std::byte> groups;
    for (std::uint32_t index = 0; index < 3; ++index)
    {
        std::vector<std::byte> group;
        append_u32(group, index == 2 ? 3U : 1U);
        for (int value = 0; value < 15; ++value)
        {
            append_float(group, static_cast<float>(value));
        }
        if (short_group)
        {
            group.resize(60);
        }
        std::vector<std::byte> data;
        append_chunk(data, 0x1101, group);
        append_chunk(groups, 0x1100, data, true);
    }

    std::vector<std::byte> tracks;
    for (std::size_t index = 0; index < 7; ++index)
    {
        const auto colour = index < 4;
        std::vector<std::byte> head;
        append_mini(head, 0x02, colour ? particle_bytes({255}) : particle_float(1.0F));
        append_mini(head, 0x03, colour ? particle_bytes({0}) : particle_float(3.0F));
        append_mini(head, 0x04, particle_u32(index == 5 ? 2U : 0U));
        std::vector<std::byte> keys;
        if (index == 3)
        {
            std::vector<std::byte> key = particle_u32(51);
            append_float(key, 0.5F);
            append_mini(keys, 0x05, key);
        }
        append_chunk(tracks, 0x00, head);
        append_chunk(tracks, 0x01, keys);
    }

    std::vector<std::byte> emitter;
    append_chunk(emitter, 0x02, properties);
    std::vector<std::byte> text;
    append_string(text, "p_smoke.tga");
    append_chunk(emitter, 0x03, text);
    text.clear();
    append_string(text, name);
    append_chunk(emitter, 0x16, text);
    append_chunk(emitter, 0x29, groups, true);
    append_chunk(emitter, 0x01, tracks, true);
    std::vector<std::byte> links;
    append_mini(links, 0x37, particle_u32(static_cast<std::uint32_t>(death_child)));
    append_mini(links, 0x39, particle_u32(0xffffffffU));
    append_chunk(emitter, 0x36, links);
    return emitter;
}

} // namespace

std::vector<std::byte> make_test_particle_system(const std::int32_t death_child, const bool unknown_property, const bool short_group)
{
    std::vector<std::byte> list;
    append_chunk(list, 0x0700, make_test_particle_emitter("sparks", death_child, unknown_property, short_group), true);
    append_chunk(list, 0x0700, make_test_particle_emitter("smoke", -1, false, false), true);
    std::vector<std::byte> system;
    std::vector<std::byte> text;
    append_string(text, "p_test");
    append_chunk(system, 0x0000, text);
    append_chunk(system, 0x0001, particle_u32(4));
    append_chunk(system, 0x0800, list, true);
    append_chunk(system, 0x0002, particle_bytes({0}));
    std::vector<std::byte> file;
    append_chunk(file, 0x0900, system, true);
    return file;
}

} // namespace eaw_test
