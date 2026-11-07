#include "eaw/xml_document.hpp"

#include "eaw/text_encoding.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <stdexcept>

namespace eaw {
namespace {

bool equal_ascii(const std::string_view left, const std::string_view right) noexcept
{
    return left.size() == right.size() && std::ranges::equal(left, right,
                                                             [](const char a, const char b)
                                                             {
                                                                 return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                                                             });
}

void append_utf8(std::string& output, const std::uint32_t code_point)
{
    if (code_point > 0x10ffffU || (code_point >= 0xd800U && code_point <= 0xdfffU))
    {
        throw std::runtime_error("XML character reference is out of range");
    }
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

std::string decode_entities(const std::string_view input)
{
    std::string output;
    output.reserve(input.size());
    for (std::size_t index = 0; index < input.size();)
    {
        if (input[index] != '&')
        {
            output.push_back(input[index++]);
            continue;
        }
        const auto end = input.find(';', index + 1);
        if (end == std::string_view::npos)
        {
            output.push_back(input[index++]);
            continue;
        }
        const auto entity = input.substr(index + 1, end - index - 1);
        if (entity == "amp")
        {
            output.push_back('&');
        }
        else if (entity == "lt")
        {
            output.push_back('<');
        }
        else if (entity == "gt")
        {
            output.push_back('>');
        }
        else if (entity == "quot")
        {
            output.push_back('"');
        }
        else if (entity == "apos")
        {
            output.push_back('\'');
        }
        else if (entity.starts_with('#'))
        {
            const bool hexadecimal = entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X');
            const auto digits = entity.substr(hexadecimal ? 2 : 1);
            std::uint32_t code_point = 0;
            const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), code_point, hexadecimal ? 16 : 10);
            if (digits.empty() || result.ec != std::errc{} || result.ptr != digits.data() + digits.size())
            {
                throw std::runtime_error("invalid XML character reference");
            }
            append_utf8(output, code_point);
        }
        else
        {
            // The stock parser is permissive and mods sometimes use custom
            // entity-looking tokens without a DTD. Preserve them losslessly.
            output.append(input.substr(index, end - index + 1));
        }
        index = end + 1;
    }
    return output;
}

std::string text_from_bytes(const std::span<const std::byte> bytes)
{
    if (bytes.size() >= 3 && std::to_integer<unsigned char>(bytes[0]) == 0xefU && std::to_integer<unsigned char>(bytes[1]) == 0xbbU && std::to_integer<unsigned char>(bytes[2]) == 0xbfU)
    {
        return {reinterpret_cast<const char*>(bytes.data() + 3), bytes.size() - 3};
    }
    if (bytes.size() >= 2)
    {
        const auto first = std::to_integer<unsigned char>(bytes[0]);
        const auto second = std::to_integer<unsigned char>(bytes[1]);
        if ((first == 0xffU && second == 0xfeU) || (first == 0xfeU && second == 0xffU))
        {
            if ((bytes.size() - 2) % 2 != 0)
            {
                throw std::runtime_error("UTF-16 XML has an odd byte count");
            }
            const bool little_endian = first == 0xffU;
            std::u16string text;
            text.reserve((bytes.size() - 2) / 2);
            for (std::size_t index = 2; index < bytes.size(); index += 2)
            {
                const auto low = std::to_integer<unsigned char>(bytes[index]);
                const auto high = std::to_integer<unsigned char>(bytes[index + 1]);
                const auto value = little_endian ? static_cast<std::uint16_t>(low | (high << 8U)) : static_cast<std::uint16_t>((low << 8U) | high);
                text.push_back(static_cast<char16_t>(value));
            }
            return utf16_to_utf8(text);
        }
    }
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

class Parser final
{
public:
    explicit Parser(const std::string_view source) : source_(source)
    {}

    XmlDocument parse()
    {
        XmlDocument document;
        while (true)
        {
            skip_space();
            if (at_end())
            {
                break;
            }
            if (starts_with("<?"))
            {
                skip_processing_instruction();
            }
            else if (starts_with("<!--"))
            {
                skip_comment();
            }
            else if (starts_with("<!"))
            {
                skip_declaration();
            }
            else if (peek() == '<')
            {
                document.roots.push_back(parse_node());
            }
            else
            {
                fail("text is not contained by an XML element");
            }
        }
        return document;
    }

private:
    std::string_view source_;
    std::size_t position_{};
    std::size_t line_{1};

    [[nodiscard]] bool at_end() const noexcept
    {
        return position_ >= source_.size();
    }
    [[nodiscard]] char peek() const noexcept
    {
        return at_end() ? '\0' : source_[position_];
    }
    [[nodiscard]] bool starts_with(const std::string_view value) const noexcept
    {
        return source_.substr(position_).starts_with(value);
    }

    [[noreturn]] void fail(const std::string_view message) const
    {
        throw std::runtime_error("XML line " + std::to_string(line_) + ": " + std::string(message));
    }

    char take()
    {
        if (at_end())
        {
            fail("unexpected end of file");
        }
        const auto value = source_[position_++];
        if (value == '\n')
        {
            ++line_;
        }
        return value;
    }

    void expect(const char expected)
    {
        if (take() != expected)
        {
            fail(std::string("expected '") + expected + "'");
        }
    }

    void skip_space()
    {
        while (!at_end() && std::isspace(static_cast<unsigned char>(peek())))
        {
            take();
        }
    }

    [[nodiscard]] static bool name_character(const char value) noexcept
    {
        const auto character = static_cast<unsigned char>(value);
        return std::isalnum(character) || value == '_' || value == '-' || value == ':' || value == '.';
    }

    std::string parse_name()
    {
        const auto begin = position_;
        while (!at_end() && name_character(peek()))
        {
            take();
        }
        if (position_ == begin)
        {
            fail("expected a name");
        }
        return std::string(source_.substr(begin, position_ - begin));
    }

    void skip_until(const std::string_view terminator, const std::string_view description)
    {
        const auto end = source_.find(terminator, position_);
        if (end == std::string_view::npos)
        {
            fail(std::string("unterminated ") + std::string(description));
        }
        while (position_ < end + terminator.size())
        {
            take();
        }
    }

    void skip_processing_instruction()
    {
        const auto target_begin = position_ + 2;
        const bool xml_declaration = target_begin + 3 <= source_.size() && equal_ascii(source_.substr(target_begin, 3), "xml") &&
                                     (target_begin + 3 == source_.size() || std::isspace(static_cast<unsigned char>(source_[target_begin + 3])) || source_[target_begin + 3] == '?');
        char quote = '\0';
        while (!at_end())
        {
            const auto value = take();
            if (quote != '\0')
            {
                if (value == quote)
                {
                    quote = '\0';
                }
                continue;
            }
            if (value == '\'' || value == '"')
            {
                quote = value;
            }
            else if (value == '?' && peek() == '>')
            {
                take();
                return;
            }
            else if (xml_declaration && value == '>')
            {
                // Three shipped FoC AI template files use the historical
                // Petroglyph form <?xml version="1.0"> without the final '?'.
                return;
            }
        }
        fail("unterminated processing instruction");
    }

    void skip_comment()
    {
        skip_until("-->", "comment");
    }

    void skip_declaration()
    {
        // Handles DOCTYPE internal subsets as well as simple declarations.
        std::size_t bracket_depth = 0;
        char quote = '\0';
        while (!at_end())
        {
            const auto value = take();
            if (quote != '\0')
            {
                if (value == quote)
                {
                    quote = '\0';
                }
                continue;
            }
            if (value == '\'' || value == '"')
            {
                quote = value;
            }
            else if (value == '[')
            {
                ++bracket_depth;
            }
            else if (value == ']' && bracket_depth != 0)
            {
                --bracket_depth;
            }
            else if (value == '>' && bracket_depth == 0)
            {
                return;
            }
        }
        fail("unterminated declaration");
    }

    XmlAttribute parse_attribute()
    {
        XmlAttribute attribute;
        attribute.line = line_;
        attribute.name = parse_name();
        skip_space();
        expect('=');
        skip_space();
        const auto quote = take();
        if (quote != '\'' && quote != '"')
        {
            fail("attribute value must be quoted");
        }
        const auto begin = position_;
        while (!at_end() && peek() != quote)
        {
            take();
        }
        if (at_end())
        {
            fail("unterminated attribute value");
        }
        attribute.value = decode_entities(source_.substr(begin, position_ - begin));
        expect(quote);
        return attribute;
    }

    XmlNode parse_node()
    {
        expect('<');
        if (peek() == '/')
        {
            fail("unexpected closing element");
        }
        XmlNode node;
        node.line = line_;
        node.name = parse_name();
        while (true)
        {
            skip_space();
            if (starts_with("/>"))
            {
                take();
                take();
                return node;
            }
            if (peek() == '>')
            {
                take();
                break;
            }
            node.attributes.push_back(parse_attribute());
        }

        while (true)
        {
            if (at_end())
            {
                fail("unterminated element <" + node.name + ">");
            }
            if (starts_with("</"))
            {
                take();
                take();
                const auto closing_name = parse_name();
                skip_space();
                expect('>');
                if (closing_name != node.name)
                {
                    fail("closing element </" + closing_name + "> does not match <" + node.name + ">");
                }
                return node;
            }
            if (starts_with("<!--"))
            {
                skip_comment();
                continue;
            }
            if (starts_with("<?"))
            {
                skip_processing_instruction();
                continue;
            }
            if (starts_with("<![CDATA["))
            {
                const auto begin = position_ + 9;
                const auto end = source_.find("]]>", begin);
                if (end == std::string_view::npos)
                {
                    fail("unterminated CDATA section");
                }
                while (position_ < begin)
                {
                    take();
                }
                node.text.append(source_.substr(begin, end - begin));
                while (position_ < end + 3)
                {
                    take();
                }
                continue;
            }
            if (starts_with("<!"))
            {
                skip_declaration();
                continue;
            }
            if (peek() == '<')
            {
                node.children.push_back(parse_node());
                continue;
            }
            const auto begin = position_;
            while (!at_end() && peek() != '<')
            {
                take();
            }
            node.text += decode_entities(source_.substr(begin, position_ - begin));
        }
    }
};

} // namespace

const XmlAttribute* XmlNode::attribute(const std::string_view requested) const noexcept
{
    const auto found = std::ranges::find_if(attributes,
                                            [&](const auto& candidate)
                                            {
                                                return equal_ascii(candidate.name, requested);
                                            });
    return found == attributes.end() ? nullptr : &*found;
}

const XmlNode* XmlNode::child(const std::string_view requested) const noexcept
{
    const auto found = std::ranges::find_if(children,
                                            [&](const auto& candidate)
                                            {
                                                return equal_ascii(candidate.name, requested);
                                            });
    return found == children.end() ? nullptr : &*found;
}

XmlDocument XmlDocument::parse(const std::string_view text)
{
    auto source = text;
    if (source.starts_with("\xef\xbb\xbf"))
    {
        source.remove_prefix(3);
    }
    return Parser(source).parse();
}

XmlDocument XmlDocument::parse(const std::span<const std::byte> bytes)
{
    const auto text = text_from_bytes(bytes);
    return parse(text);
}

} // namespace eaw
