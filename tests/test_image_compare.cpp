// Image regression: comparing RGBA images.
#include "test_support.hpp"

#include "image_compare/image_compare.hpp"

namespace {

using namespace eaw_test;

// Comparison measures and marks differences, finds none between identical images, and refuses
// images of different sizes.
void test_compare_images()
{
    eaw::RgbaImage image;
    image.width = 200;
    image.height = 120;
    image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * 4U);
    for (std::size_t index = 0; index < image.pixels.size(); ++index)
    {
        image.pixels[index] = static_cast<std::uint8_t>((index * 7U + index / 997U) & 0xFFU);
    }
    auto changed = image;
    changed.pixels[0] = static_cast<std::uint8_t>(changed.pixels[0] + 100U);
    changed.pixels[4 * 50 + 1] = static_cast<std::uint8_t>(changed.pixels[4 * 50 + 1] + 3U);
    const auto difference = expect_ok(eaw::compare_images(image, changed), "image compare");
    require(difference.max_error == 100 && difference.changed_pixels == 1 && difference.pixels == 24000 && difference.mean_error > 0.0 && difference.visualization.valid() &&
                difference.visualization.pixels[0] == 255 && difference.visualization.pixels[1] == 0,
            "image comparison must measure and mark differences");
    const auto same = expect_ok(eaw::compare_images(image, image), "image compare");
    require(same.max_error == 0 && same.changed_pixels == 0 && same.mean_error == 0.0, "identical images must not differ");
    auto smaller = image;
    smaller.height = 10;
    smaller.pixels.resize(static_cast<std::size_t>(smaller.width) * smaller.height * 4U);
    require(!eaw::compare_images(image, smaller), "images of different sizes must not compare");
}

const ModuleRegistrar registrar("image_compare", {
                                                     {"test_compare_images", &test_compare_images},
                                                 });

} // namespace
