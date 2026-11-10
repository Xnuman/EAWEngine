#include "core/string_utils.hpp"

#include <cstddef>

namespace eaw {
namespace {

constexpr char32_t last_ascii = 0x7fU;
constexpr char32_t last_two_byte = 0x7ffU;
constexpr char32_t last_basic_plane = 0xffffU;
constexpr char32_t last_code_point = 0x10ffffU;
constexpr char32_t first_supplementary = 0x10000U;
constexpr char32_t first_high_surrogate = 0xd800U;
constexpr char32_t last_high_surrogate = 0xdbffU;
constexpr char32_t first_low_surrogate = 0xdc00U;
constexpr char32_t last_low_surrogate = 0xdfffU;
// Bits of the code point each surrogate carries.
constexpr unsigned surrogate_bits = 10U;
constexpr char32_t surrogate_mask = 0x3ffU;
// A UTF-8 continuation byte is 10xxxxxx and carries six bits.
constexpr unsigned continuation_bits = 6U;
constexpr unsigned char continuation_mask = 0x3fU;
constexpr unsigned char continuation_tag_mask = 0xc0U;
constexpr unsigned char continuation_tag = 0x80U;

bool append_utf16(std::u16string& output, const char32_t code_point)
{
    if (code_point <= last_basic_plane)
    {
        if (code_point >= first_high_surrogate && code_point <= last_low_surrogate)
        {
            return false;
        }
        output.push_back(static_cast<char16_t>(code_point));
        return true;
    }
    if (code_point > last_code_point)
    {
        return false;
    }
    const auto adjusted = code_point - first_supplementary;
    output.push_back(static_cast<char16_t>(first_high_surrogate + (adjusted >> surrogate_bits)));
    output.push_back(static_cast<char16_t>(first_low_surrogate + (adjusted & surrogate_mask)));
    return true;
}

// The UTF-8 form of a code point already known to be valid.
void encode_utf8(std::string& output, const char32_t code_point)
{
    if (code_point <= last_ascii)
    {
        output.push_back(static_cast<char>(code_point));
    }
    else if (code_point <= last_two_byte)
    {
        output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(continuation_tag | (code_point & continuation_mask)));
    }
    else if (code_point <= last_basic_plane)
    {
        output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(continuation_tag | ((code_point >> 6U) & continuation_mask)));
        output.push_back(static_cast<char>(continuation_tag | (code_point & continuation_mask)));
    }
    else
    {
        output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(continuation_tag | ((code_point >> 12U) & continuation_mask)));
        output.push_back(static_cast<char>(continuation_tag | ((code_point >> 6U) & continuation_mask)));
        output.push_back(static_cast<char>(continuation_tag | (code_point & continuation_mask)));
    }
}

} // namespace

std::string ascii_lower(const std::string_view text)
{
    std::string result(text);
    for (auto& character : result)
    {
        character = ascii_lower(character);
    }
    return result;
}

std::string ascii_upper(const std::string_view text)
{
    std::string result(text);
    for (auto& character : result)
    {
        character = ascii_upper(character);
    }
    return result;
}

bool ascii_equal_ignoring_case(const std::string_view left, const std::string_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (ascii_lower(left[index]) != ascii_lower(right[index]))
        {
            return false;
        }
    }
    return true;
}

std::size_t ascii_find_ignoring_case(const std::string_view text, const std::string_view needle, const std::size_t from) noexcept
{
    if (from > text.size() || needle.size() > text.size() - from)
    {
        return std::string_view::npos;
    }
    for (auto position = from; position + needle.size() <= text.size(); ++position)
    {
        if (ascii_equal_ignoring_case(text.substr(position, needle.size()), needle))
        {
            return position;
        }
    }
    return std::string_view::npos;
}

std::string_view ascii_trim(std::string_view text) noexcept
{
    while (!text.empty() && ascii_is_space(text.front()))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && ascii_is_space(text.back()))
    {
        text.remove_suffix(1);
    }
    return text;
}

bool append_utf8(std::string& output, const char32_t code_point)
{
    if (code_point > last_code_point || (code_point >= first_high_surrogate && code_point <= last_low_surrogate))
    {
        return false;
    }
    encode_utf8(output, code_point);
    return true;
}

std::optional<std::u16string> utf8_to_utf16(const std::string_view text)
{
    std::u16string output;
    output.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size())
    {
        // The leading byte gives the sequence length and the smallest code point it may encode
        // (anything below is an overlong form).
        const auto first = static_cast<unsigned char>(text[index++]);
        char32_t code_point = 0;
        std::size_t continuation_count = 0;
        char32_t minimum = 0;
        if (first <= last_ascii)
        {
            code_point = first;
        }
        else if ((first & 0xe0U) == 0xc0U)
        {
            code_point = first & 0x1fU;
            continuation_count = 1;
            minimum = last_ascii + 1U;
        }
        else if ((first & 0xf0U) == 0xe0U)
        {
            code_point = first & 0x0fU;
            continuation_count = 2;
            minimum = last_two_byte + 1U;
        }
        else if ((first & 0xf8U) == 0xf0U)
        {
            code_point = first & 0x07U;
            continuation_count = 3;
            minimum = first_supplementary;
        }
        else
        {
            return std::nullopt;
        }

        if (index + continuation_count > text.size())
        {
            return std::nullopt;
        }
        for (std::size_t continuation = 0; continuation < continuation_count; ++continuation)
        {
            const auto byte = static_cast<unsigned char>(text[index++]);
            if ((byte & continuation_tag_mask) != continuation_tag)
            {
                return std::nullopt;
            }
            code_point = (code_point << continuation_bits) | (byte & continuation_mask);
        }
        if ((continuation_count != 0 && code_point < minimum) || !append_utf16(output, code_point))
        {
            return std::nullopt;
        }
    }
    return output;
}

std::optional<std::string> utf16_to_utf8(const std::u16string_view text)
{
    std::string output;
    output.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        char32_t code_point = text[index];
        if (code_point >= first_high_surrogate && code_point <= last_high_surrogate)
        {
            if (index + 1 >= text.size())
            {
                return std::nullopt;
            }
            const char32_t low = text[++index];
            if (low < first_low_surrogate || low > last_low_surrogate)
            {
                return std::nullopt;
            }
            code_point = first_supplementary + ((code_point - first_high_surrogate) << surrogate_bits) + (low - first_low_surrogate);
        }
        else if (code_point >= first_low_surrogate && code_point <= last_low_surrogate)
        {
            return std::nullopt;
        }
        encode_utf8(output, code_point);
    }
    return output;
}

} // namespace eaw
