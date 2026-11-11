#include "formats/animation_sfx_map.hpp"

#include "core/string_utils.hpp"

#include <string_view>

namespace eaw {
namespace {

// Allocation limits (engine choices, far above the stock table's 1,639 lines and 5 fields).
constexpr std::size_t max_rows = 1'000'000;
constexpr std::size_t max_fields_per_row = 64;

} // namespace

Result<AnimationSfxMapData> read_animation_sfx_map(const std::span<const std::byte> bytes)
{
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (text.starts_with("\xEF\xBB\xBF"))
    {
        text.remove_prefix(3);
    }
    if (text.find('\0') != std::string_view::npos)
    {
        return make_error("animation SFX map contains a NUL byte; not a text file");
    }
    AnimationSfxMapData data;
    std::size_t line = 1;
    std::size_t start = 0;
    while (start <= text.size())
    {
        auto end = text.find_first_of("\r\n", start);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }
        const auto content = text.substr(start, end - start);
        AnimationSfxMapRowData row;
        row.line = line;
        std::size_t position = 0;
        while (position < content.size())
        {
            while (position < content.size() && ascii_is_space(content[position]))
            {
                ++position;
            }
            const auto field_start = position;
            while (position < content.size() && !ascii_is_space(content[position]))
            {
                ++position;
            }
            if (position > field_start)
            {
                if (row.fields.size() >= max_fields_per_row)
                {
                    return make_error("animation SFX map line " + std::to_string(line) + " has more than " + std::to_string(max_fields_per_row) + " fields");
                }
                row.fields.emplace_back(content.substr(field_start, position - field_start));
            }
        }
        if (!row.fields.empty())
        {
            if (data.rows.size() >= max_rows)
            {
                return make_error("animation SFX map has more than " + std::to_string(max_rows) + " rows");
            }
            data.rows.push_back(std::move(row));
        }
        if (end == text.size())
        {
            break;
        }
        // CR LF counts as one line end.
        start = end + (text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n' ? 2U : 1U);
        ++line;
    }
    return data;
}

} // namespace eaw
