#pragma once

// DATA/XML/ANIMATIONSFXMAPS.TXT: the table that ties moments of animations to sounds and surface
// effects. Each line holds fields separated by tabs or spaces; the stock rows are
// `SOUND <animation> <time> <sound event>` and `SURFACE <animation> <time> <trigger> <bone>`. The
// reader keeps every non-empty line's fields as written, in order, with its line number; what the
// rows mean is the asset layer's (assets/animation_events).

#include "core/status.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace eaw {

struct AnimationSfxMapRowData
{
    std::vector<std::string> fields;
    std::size_t line{1};
};

struct AnimationSfxMapData
{
    std::vector<AnimationSfxMapRowData> rows;
};

// Lines end at LF, CR LF, or CR; a UTF-8 byte-order mark is skipped. Errors: a NUL byte (not a text
// file), or more rows or fields than the reader's limits.
[[nodiscard]] Result<AnimationSfxMapData> read_animation_sfx_map(std::span<const std::byte> bytes);

} // namespace eaw
