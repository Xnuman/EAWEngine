#include "formats/xml_document.hpp"

#include "core/string_utils.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>

namespace eaw {
namespace {

Result<std::string> decode_entities(const std::string_view input)
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
                return make_error("invalid XML character reference");
            }
            if (!append_utf8(output, static_cast<char32_t>(code_point)))
            {
                return make_error("XML character reference is out of range");
            }
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

Result<std::string> text_from_bytes(const std::span<const std::byte> bytes)
{
    if (bytes.size() >= 3 && std::to_integer<unsigned char>(bytes[0]) == 0xefU && std::to_integer<unsigned char>(bytes[1]) == 0xbbU && std::to_integer<unsigned char>(bytes[2]) == 0xbfU)
    {
        return std::string(reinterpret_cast<const char*>(bytes.data() + 3), bytes.size() - 3);
    }
    if (bytes.size() >= 2)
    {
        const auto first = std::to_integer<unsigned char>(bytes[0]);
        const auto second = std::to_integer<unsigned char>(bytes[1]);
        if ((first == 0xffU && second == 0xfeU) || (first == 0xfeU && second == 0xffU))
        {
            if ((bytes.size() - 2) % 2 != 0)
            {
                return make_error("UTF-16 XML has an odd byte count");
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
            auto converted = utf16_to_utf8(text);
            if (!converted)
            {
                return make_error("UTF-16 XML is not valid UTF-16");
            }
            return std::move(*converted);
        }
    }
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// Recursive-descent parser. The first failure is recorded and moves the cursor to the end of the
// source, so every loop that stops at the end terminates; loops that do not consume input on their
// own check failed() explicitly.
class Parser final
{
public:
    explicit Parser(const std::string_view source) : source_(source)
    {}

    Result<XmlData> parse()
    {
        XmlData document;
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
        if (error_)
        {
            return std::move(*error_);
        }
        return document;
    }

private:
    std::string_view source_;
    std::size_t position_{};
    std::size_t line_{1};
    std::optional<Error> error_;

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

    [[nodiscard]] bool failed() const noexcept
    {
        return error_.has_value();
    }

    // Records the first failure and ends parsing.
    void fail(const std::string_view message)
    {
        if (!error_)
        {
            error_ = make_error("XML line " + std::to_string(line_) + ": " + std::string(message));
        }
        position_ = source_.size();
    }

    // Decodes entities, recording a failure (and returning nothing) for an invalid reference.
    std::string decode(const std::string_view input)
    {
        auto decoded = decode_entities(input);
        if (!decoded)
        {
            fail(decoded.error().message);
            return {};
        }
        return std::move(decoded).value();
    }

    char take()
    {
        if (at_end())
        {
            fail("unexpected end of file");
            return '\0';
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
        while (!at_end() && ascii_is_space(peek()))
        {
            take();
        }
    }

    [[nodiscard]] static bool name_character(const char value) noexcept
    {
        const auto character = static_cast<unsigned char>(value);
        return ascii_is_alnum(static_cast<char>(character)) || value == '_' || value == '-' || value == ':' || value == '.';
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
            return;
        }
        while (position_ < end + terminator.size())
        {
            take();
        }
    }

    void skip_processing_instruction()
    {
        const auto target_begin = position_ + 2;
        const bool xml_declaration = target_begin + 3 <= source_.size() && ascii_equal_ignoring_case(source_.substr(target_begin, 3), "xml") &&
                                     (target_begin + 3 == source_.size() || ascii_is_space(source_[target_begin + 3]) || source_[target_begin + 3] == '?');
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
            return attribute;
        }
        const auto begin = position_;
        while (!at_end() && peek() != quote)
        {
            take();
        }
        if (at_end())
        {
            fail("unterminated attribute value");
            return attribute;
        }
        attribute.value = decode(source_.substr(begin, position_ - begin));
        expect(quote);
        return attribute;
    }

    XmlNode parse_node()
    {
        expect('<');
        XmlNode node;
        if (peek() == '/')
        {
            fail("unexpected closing element");
            return node;
        }
        node.line = line_;
        node.name = parse_name();
        while (true)
        {
            if (failed())
            {
                return node;
            }
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
            if (failed())
            {
                return node;
            }
            if (at_end())
            {
                fail("unterminated element <" + node.name + ">");
                return node;
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
                    return node;
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
            node.text += decode(source_.substr(begin, position_ - begin));
        }
    }
};

} // namespace

Result<XmlData> read_xml(const std::string_view text)
{
    auto source = text;
    if (source.starts_with("\xef\xbb\xbf"))
    {
        source.remove_prefix(3);
    }
    return Parser(source).parse();
}

Result<XmlData> read_xml(const std::span<const std::byte> bytes)
{
    EAW_TRY_ASSIGN(const auto text, text_from_bytes(bytes));
    return read_xml(std::string_view(text));
}

} // namespace eaw
