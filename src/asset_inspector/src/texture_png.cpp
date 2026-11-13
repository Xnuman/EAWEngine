// `texture-png`: the top level of one texture as a PNG file the user names, to look at a texture
// (an atlas's layout, a channel's use) with ordinary tools. Uncompressed formats only. Textures of
// the stock game are protected data: keep the files outside the repository.

#include "core/file_io.hpp"
#include "formats/dds_texture.hpp"
#include "formats/png_image.hpp"
#include "asset_inspector/inspector.hpp"

namespace eaw::asset_inspector {
namespace {

// Where a format keeps red, green, blue, and alpha within a pixel of `size` bytes (-1: absent,
// giving 255 for alpha and 0 for a color).
struct PixelLayout
{
    std::size_t size{};
    std::array<int, 4> channels{};
};

std::optional<PixelLayout> pixel_layout(const eaw::DdsFormat format)
{
    switch (format)
    {
    case eaw::DdsFormat::rgba8_unorm:
    case eaw::DdsFormat::rgba8_srgb:
        return PixelLayout{4, {0, 1, 2, 3}};
    case eaw::DdsFormat::bgra8_unorm:
    case eaw::DdsFormat::bgra8_srgb:
        return PixelLayout{4, {2, 1, 0, 3}};
    case eaw::DdsFormat::rgbx8_unorm:
        return PixelLayout{4, {0, 1, 2, -1}};
    case eaw::DdsFormat::bgrx8_unorm:
    case eaw::DdsFormat::bgrx8_srgb:
        return PixelLayout{4, {2, 1, 0, -1}};
    case eaw::DdsFormat::rgb8_unorm:
        return PixelLayout{3, {0, 1, 2, -1}};
    case eaw::DdsFormat::bgr8_unorm:
        return PixelLayout{3, {2, 1, 0, -1}};
    default:
        return std::nullopt;
    }
}

} // namespace

eaw::Status texture_png(const eaw::VirtualFileSystem& vfs, const Options& options, const std::vector<std::filesystem::path>& read_only_roots)
{
    if (!options.output)
    {
        return eaw::make_error("texture-png needs --output <file.png>");
    }
    for (const auto& root : read_only_roots)
    {
        if (is_within(*options.output, root))
        {
            return eaw::make_error("the output " + options.output->string() + " is inside " + root.string() + ", which is only read");
        }
    }
    EAW_TRY_ASSIGN(const auto bytes, vfs.read(options.operand));
    EAW_TRY_ASSIGN(const auto texture, eaw::read_dds(bytes));
    const auto layout = pixel_layout(texture.format);
    if (!layout)
    {
        return eaw::make_error(options.operand + ": " + std::string(eaw::to_string(texture.format)) + " textures are not converted (uncompressed formats only)");
    }
    if (texture.subresources.empty())
    {
        return eaw::make_error(options.operand + ": the texture has no image");
    }
    const auto& top = texture.subresources.front();
    if (top.row_pitch < static_cast<std::size_t>(top.width) * layout->size || top.offset + top.row_pitch * top.height > bytes.size())
    {
        return eaw::make_error(options.operand + ": the top image lies outside the file");
    }
    eaw::RgbaImage image;
    image.width = top.width;
    image.height = top.height;
    image.pixels.resize(static_cast<std::size_t>(top.width) * top.height * 4U);
    for (std::uint32_t y = 0; y < top.height; ++y)
    {
        for (std::uint32_t x = 0; x < top.width; ++x)
        {
            const auto source = top.offset + y * top.row_pitch + x * layout->size;
            const auto target = (static_cast<std::size_t>(y) * top.width + x) * 4U;
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                const auto from = layout->channels[channel];
                image.pixels[target + channel] = from < 0 ? (channel == 3 ? std::uint8_t{255} : std::uint8_t{0}) : static_cast<std::uint8_t>(bytes[source + static_cast<std::size_t>(from)]);
            }
        }
    }
    EAW_TRY_ASSIGN(const auto png, eaw::write_png(image));
    EAW_TRY(eaw::write_file(*options.output, png, eaw::WriteMode::replace));
    std::cout << "Wrote " << top.width << 'x' << top.height << ' ' << eaw::to_string(texture.format) << " image to " << options.output->string() << '\n';
    return {};
}

} // namespace eaw::asset_inspector
