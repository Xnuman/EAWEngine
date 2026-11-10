#include "image_compare_command.hpp"

#include "formats/png_image.hpp"
#include "formats/rgba_image.hpp"
#include "core/file_io.hpp"

#include <charconv>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {
namespace {

void print_usage()
{
    std::cout << "Usage: EAWEngine compare-images <expected.png> <actual.png> [options]\n\n"
              << "Compares two captures (sandbox --capture) pixel by pixel.\n\n"
              << "  --threshold <0-255>        A pixel counts as changed when a colour channel differs by more (default 8)\n"
              << "  --max-changed <fraction>   Largest share of changed pixels that still passes (default 0.001)\n"
              << "  --diff <out.png>           Write the first image dimmed, with changed pixels in red\n\n"
              << "Exit codes: 0 within the limit, 5 over it, 1 an image could not be read, 2 a bad command line.\n";
}

template <class T> std::optional<T> number(const std::string_view text)
{
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

} // namespace

int run_image_compare(const int argc, const char* const* argv)
{
    std::vector<std::string> files;
    std::uint32_t threshold = 8;
    double max_changed = 0.001;
    std::optional<std::filesystem::path> diff;
    for (int index = 2; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        const auto value = [&]() -> std::optional<std::string_view>
        {
            if (index + 1 >= argc)
            {
                std::cerr << "error: " << argument << " needs a value\n";
                return std::nullopt;
            }
            return std::string_view(argv[++index]);
        };
        if (argument == "--help" || argument == "-h")
        {
            print_usage();
            return 0;
        }
        if (argument == "--threshold")
        {
            const auto text = value();
            const auto parsed = text ? number<std::uint32_t>(*text) : std::nullopt;
            if (!parsed || *parsed > 255)
            {
                std::cerr << "error: --threshold needs a number from 0 to 255\n";
                return 2;
            }
            threshold = *parsed;
        }
        else if (argument == "--max-changed")
        {
            const auto text = value();
            const auto parsed = text ? number<double>(*text) : std::nullopt;
            if (!parsed || !(*parsed >= 0.0 && *parsed <= 1.0))
            {
                std::cerr << "error: --max-changed needs a fraction from 0 to 1\n";
                return 2;
            }
            max_changed = *parsed;
        }
        else if (argument == "--diff")
        {
            const auto text = value();
            if (!text)
            {
                return 2;
            }
            diff = std::filesystem::path(std::string(*text));
        }
        else if (argument.starts_with("--"))
        {
            std::cerr << "error: unknown option " << argument << '\n';
            return 2;
        }
        else
        {
            files.emplace_back(argument);
        }
    }
    if (files.size() != 2)
    {
        print_usage();
        return 2;
    }
    const auto read_image = [](const std::filesystem::path& path) -> Result<RgbaImage>
    {
        EAW_TRY_ASSIGN(const auto bytes, read_file(path));
        auto image = decode_png(bytes);
        if (!image)
        {
            return with_context(path.string(), std::move(image).error());
        }
        return image;
    };
    const auto expected = read_image(files[0]);
    const auto actual = read_image(files[1]);
    if (!expected || !actual)
    {
        std::cerr << "error: " << (!expected ? expected.error().message : actual.error().message) << '\n';
        return 1;
    }
    const auto difference = compare_images(*expected, *actual, static_cast<std::uint8_t>(threshold));
    if (!difference)
    {
        std::cerr << "error: " << difference.error().message << '\n';
        return 1;
    }
    const auto changed = static_cast<double>(difference->changed_pixels) / static_cast<double>(difference->pixels);
    std::cout << "Size: " << expected->width << "x" << expected->height << '\n'
              << "Mean channel difference: " << std::fixed << std::setprecision(3) << difference->mean_error << '\n'
              << "Largest channel difference: " << difference->max_error << '\n'
              << "Changed pixels (difference above " << threshold << "): " << difference->changed_pixels << " (" << std::setprecision(4) << changed * 100.0 << "%)\n";
    if (diff)
    {
        const auto encoded = encode_png(difference->visualization);
        if (const auto written = encoded ? write_file(*diff, *encoded, WriteMode::replace) : Status(encoded.error()); !written)
        {
            std::cerr << "error: " << written.error().message << '\n';
            return 1;
        }
        std::cout << "Difference image: " << diff->string() << '\n';
    }
    const bool passed = changed <= max_changed;
    std::cout << (passed ? "Within the limit" : "Over the limit") << " (" << std::setprecision(4) << max_changed * 100.0 << "% changed pixels allowed)\n";
    return passed ? 0 : 5;
}

} // namespace eaw
