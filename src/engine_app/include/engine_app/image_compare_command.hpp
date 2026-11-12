#pragma once

// EAWEngine compare-images: image regression on the engine's own captures (sandbox --capture).

namespace eaw::engine_app {

// `EAWEngine compare-images <expected.png> <actual.png> [--threshold n] [--max-changed fraction]
// [--diff out.png]`. Returns 0 when the changed share of pixels is within the limit, 5 when it is
// not, 1 when an image cannot be read or compared, and 2 for a malformed command line.
[[nodiscard]] int run_image_compare(int argc, const char* const* argv);

} // namespace eaw::engine_app
