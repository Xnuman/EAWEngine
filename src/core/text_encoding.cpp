#include "eaw/text_encoding.hpp"

#include <cstdint>
#include <stdexcept>

namespace eaw {
namespace {

void append_utf16(std::u16string& output, const std::uint32_t code_point)
{
    if (code_point <= 0xffffU)
    {
        if (code_point >= 0xd800U && code_point <= 0xdfffU)
        {
            throw std::invalid_argument("UTF-8 contains a surrogate code point");
        }
        output.push_back(static_cast<char16_t>(code_point));
        return;
    }
    if (code_point > 0x10ffffU)
    {
        throw std::invalid_argument("UTF-8 code point is out of range");
    }
    const auto adjusted = code_point - 0x10000U;
    output.push_back(static_cast<char16_t>(0xd800U + (adjusted >> 10U)));
    output.push_back(static_cast<char16_t>(0xdc00U + (adjusted & 0x3ffU)));
}

void append_utf8(std::string& output, const std::uint32_t code_point)
{
    if (code_point <= 0x7fU)
    {
        output.push_back(static_cast<char>(code_point));
    }
    else if (code_point <= 0x7ffU)
    {
        output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    }
    else if (code_point <= 0xffffU)
    {
        output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    }
    else
    {
        output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    }
}

} // namespace

std::u16string utf8_to_utf16(const std::string_view text)
{
    std::u16string output;
    output.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size())
    {
        const auto first = static_cast<unsigned char>(text[index++]);
        std::uint32_t code_point = 0;
        std::size_t continuation_count = 0;
        std::uint32_t minimum = 0;
        if (first <= 0x7fU)
        {
            code_point = first;
        }
        else if ((first & 0xe0U) == 0xc0U)
        {
            code_point = first & 0x1fU;
            continuation_count = 1;
            minimum = 0x80U;
        }
        else if ((first & 0xf0U) == 0xe0U)
        {
            code_point = first & 0x0fU;
            continuation_count = 2;
            minimum = 0x800U;
        }
        else if ((first & 0xf8U) == 0xf0U)
        {
            code_point = first & 0x07U;
            continuation_count = 3;
            minimum = 0x10000U;
        }
        else
        {
            throw std::invalid_argument("invalid UTF-8 leading byte");
        }

        if (index + continuation_count > text.size())
        {
            throw std::invalid_argument("truncated UTF-8 sequence");
        }
        for (std::size_t continuation = 0; continuation < continuation_count; ++continuation)
        {
            const auto byte = static_cast<unsigned char>(text[index++]);
            if ((byte & 0xc0U) != 0x80U)
            {
                throw std::invalid_argument("invalid UTF-8 continuation byte");
            }
            code_point = (code_point << 6U) | (byte & 0x3fU);
        }
        if (continuation_count != 0 && code_point < minimum)
        {
            throw std::invalid_argument("overlong UTF-8 sequence");
        }
        append_utf16(output, code_point);
    }
    return output;
}

std::string utf16_to_utf8(const std::u16string_view text)
{
    std::string output;
    output.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        std::uint32_t code_point = text[index];
        if (code_point >= 0xd800U && code_point <= 0xdbffU)
        {
            if (index + 1 >= text.size())
            {
                throw std::invalid_argument("unpaired UTF-16 high surrogate");
            }
            const std::uint32_t low = text[++index];
            if (low < 0xdc00U || low > 0xdfffU)
            {
                throw std::invalid_argument("unpaired UTF-16 high surrogate");
            }
            code_point = 0x10000U + ((code_point - 0xd800U) << 10U) + (low - 0xdc00U);
        }
        else if (code_point >= 0xdc00U && code_point <= 0xdfffU)
        {
            throw std::invalid_argument("unpaired UTF-16 low surrogate");
        }
        append_utf8(output, code_point);
    }
    return output;
}

} // namespace eaw
