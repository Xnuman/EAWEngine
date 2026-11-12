#include "formats/lua_bytecode.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <optional>
#include <set>

namespace eaw {
namespace {

enum class TokenKind
{
    identifier,
    string,
    symbol,
};

struct Token
{
    TokenKind kind{};
    std::string text;
};

bool identifier_start(const unsigned char character) noexcept
{
    return ascii_is_alpha(static_cast<char>(character)) || character == '_';
}

bool identifier_continue(const unsigned char character) noexcept
{
    return ascii_is_alnum(static_cast<char>(character)) || character == '_';
}

std::size_t long_bracket_end(const std::string_view source, const std::size_t begin)
{
    if (begin >= source.size() || source[begin] != '[')
    {
        return std::string_view::npos;
    }
    std::size_t equals = 0;
    while (begin + 1U + equals < source.size() && source[begin + 1U + equals] == '=')
    {
        ++equals;
    }
    if (begin + 1U + equals >= source.size() || source[begin + 1U + equals] != '[')
    {
        return std::string_view::npos;
    }
    std::string close{"]"};
    close.append(equals, '=');
    close.push_back(']');
    return source.find(close, begin + equals + 2U);
}

std::vector<Token> tokenize(const std::string_view source, std::vector<std::string>& warnings)
{
    std::vector<Token> tokens;
    std::size_t cursor = 0;
    if (source.starts_with("\xef\xbb\xbf"))
    {
        cursor = 3;
    }
    while (cursor < source.size())
    {
        const auto character = static_cast<unsigned char>(source[cursor]);
        if (ascii_is_space(static_cast<char>(character)))
        {
            ++cursor;
            continue;
        }
        if (source[cursor] == '-' && cursor + 1U < source.size() && source[cursor + 1U] == '-')
        {
            cursor += 2;
            if (const auto close = long_bracket_end(source, cursor); close != std::string_view::npos)
            {
                std::size_t equals = 0;
                while (cursor + 1U + equals < source.size() && source[cursor + 1U + equals] == '=')
                {
                    ++equals;
                }
                cursor = close + equals + 2U;
            }
            else
            {
                const auto end = source.find('\n', cursor);
                cursor = end == std::string_view::npos ? source.size() : end + 1U;
            }
            continue;
        }
        if (source[cursor] == '[')
        {
            const auto close = long_bracket_end(source, cursor);
            if (close != std::string_view::npos)
            {
                std::size_t equals = 0;
                while (cursor + 1U + equals < source.size() && source[cursor + 1U + equals] == '=')
                {
                    ++equals;
                }
                const auto content_begin = cursor + equals + 2U;
                tokens.push_back({TokenKind::string, std::string(source.substr(content_begin, close - content_begin))});
                cursor = close + equals + 2U;
                continue;
            }
        }
        if (source[cursor] == '\'' || source[cursor] == '"')
        {
            const auto quote = source[cursor++];
            std::string value;
            bool terminated = false;
            while (cursor < source.size())
            {
                const auto current = source[cursor++];
                if (current == quote)
                {
                    terminated = true;
                    break;
                }
                if (current == '\\' && cursor < source.size())
                {
                    const auto escaped = source[cursor++];
                    switch (escaped)
                    {
                    case 'n':
                        value.push_back('\n');
                        break;
                    case 'r':
                        value.push_back('\r');
                        break;
                    case 't':
                        value.push_back('\t');
                        break;
                    default:
                        value.push_back(escaped);
                        break;
                    }
                }
                else
                {
                    value.push_back(current);
                }
            }
            if (!terminated)
            {
                warnings.push_back("unterminated quoted string");
            }
            tokens.push_back({TokenKind::string, std::move(value)});
            continue;
        }
        if (identifier_start(character))
        {
            const auto begin = cursor++;
            while (cursor < source.size() && identifier_continue(static_cast<unsigned char>(source[cursor])))
            {
                ++cursor;
            }
            tokens.push_back({TokenKind::identifier, std::string(source.substr(begin, cursor - begin))});
            continue;
        }
        tokens.push_back({TokenKind::symbol, std::string(1, source[cursor++])});
    }
    return tokens;
}

std::pair<std::string, std::size_t> qualified_name(const std::vector<Token>& tokens, const std::size_t begin)
{
    if (begin >= tokens.size() || tokens[begin].kind != TokenKind::identifier)
    {
        return {};
    }
    std::string name = tokens[begin].text;
    auto cursor = begin + 1U;
    while (cursor + 1U < tokens.size() && tokens[cursor].kind == TokenKind::symbol && (tokens[cursor].text == "." || tokens[cursor].text == ":") && tokens[cursor + 1U].kind == TokenKind::identifier)
    {
        name += tokens[cursor].text;
        name += tokens[cursor + 1U].text;
        cursor += 2U;
    }
    return {std::move(name), cursor};
}

void inspect_source(const std::span<const std::byte> bytes, LuaChunkData& result)
{
    const std::string_view source(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    result.lines = bytes.empty() ? 0U : static_cast<std::size_t>(std::ranges::count(source, '\n')) + (source.back() == '\n' ? 0U : 1U);
    const auto tokens = tokenize(source, result.warnings);
    std::set<std::string> dependencies;
    std::set<std::string> definitions;
    std::set<std::string> calls;

    for (std::size_t index = 0; index < tokens.size(); ++index)
    {
        if (tokens[index].kind != TokenKind::identifier)
        {
            continue;
        }
        if (ascii_lower(tokens[index].text) == "function")
        {
            const auto [name, end] = qualified_name(tokens, index + 1U);
            if (!name.empty())
            {
                definitions.insert(name);
                index = end - 1U;
            }
            continue;
        }

        const auto [name, end] = qualified_name(tokens, index);
        if (name.empty() || end >= tokens.size())
        {
            continue;
        }
        static const std::set<std::string> keywords{"and",   "break", "do",  "else", "elseif", "end",    "false", "for",  "function", "if",   "in",
                                                    "local", "nil",   "not", "or",   "repeat", "return", "then",  "true", "until",    "while"};
        if (keywords.contains(ascii_lower(name)))
        {
            index = end - 1U;
            continue;
        }
        const bool parenthesized = tokens[end].kind == TokenKind::symbol && tokens[end].text == "(";
        const bool string_argument = tokens[end].kind == TokenKind::string;
        if (!parenthesized && !string_argument)
        {
            index = end - 1U;
            continue;
        }
        calls.insert(name);
        const auto folded_name = ascii_lower(name);
        if (folded_name == "require" || folded_name == "script.load" || folded_name == "script.reload" || folded_name == "include")
        {
            auto argument = end + (parenthesized ? 1U : 0U);
            if (argument < tokens.size() && tokens[argument].kind == TokenKind::string && !tokens[argument].text.empty())
            {
                dependencies.insert(tokens[argument].text);
            }
        }
        index = end - 1U;
    }
    result.dependencies.assign(dependencies.begin(), dependencies.end());
    result.defined_functions.assign(definitions.begin(), definitions.end());
    result.called_functions.assign(calls.begin(), calls.end());
}

// Reads the bytecode structure. The first failure is recorded and moves the cursor to the end, so
// later reads fail too and return zero; loops driven by decoded counts stop on failed().
class BytecodeReader
{
public:
    BytecodeReader(const std::span<const std::byte> bytes, const bool petroglyph) : bytes_(bytes), petroglyph_(petroglyph)
    {}

    Result<LuaChunkData> read()
    {
        LuaChunkData result;
        result.kind = petroglyph_ ? LuaScriptKind::petroglyph_bytecode : LuaScriptKind::lua50_bytecode;
        cursor_ = 4;
        result.bytecode_header.version = read_byte();
        result.bytecode_header.little_endian = read_byte() != 0;
        little_endian_ = result.bytecode_header.little_endian;
        result.bytecode_header.integer_size = read_byte();
        result.bytecode_header.size_type_size = read_byte();
        result.bytecode_header.instruction_size = read_byte();
        result.bytecode_header.opcode_bits = read_byte();
        result.bytecode_header.argument_a_bits = read_byte();
        result.bytecode_header.argument_b_bits = read_byte();
        result.bytecode_header.argument_c_bits = read_byte();
        result.bytecode_header.number_size = read_byte();
        validate_size(result.bytecode_header.integer_size, "integer");
        validate_size(result.bytecode_header.size_type_size, "size_t");
        validate_size(result.bytecode_header.instruction_size, "instruction");
        validate_size(result.bytecode_header.number_size, "number");
        if (!failed() && result.bytecode_header.version != (petroglyph_ ? 0x51U : 0x50U))
        {
            fail("unsupported Lua bytecode version");
        }
        if (failed())
        {
            return std::move(*error_);
        }
        integer_size_ = result.bytecode_header.integer_size;
        size_type_size_ = result.bytecode_header.size_type_size;
        instruction_size_ = result.bytecode_header.instruction_size;
        number_size_ = result.bytecode_header.number_size;
        skip(number_size_);
        read_function(result, 0);
        if (failed())
        {
            return std::move(*error_);
        }
        if (cursor_ != bytes_.size())
        {
            result.warnings.push_back("bytecode has " + std::to_string(bytes_.size() - cursor_) + " trailing byte(s)");
        }
        return result;
    }

private:
    static constexpr std::uint64_t maximum_count = 100'000'000U;

    [[nodiscard]] bool failed() const noexcept
    {
        return error_.has_value();
    }

    // Records the first failure and ends reading.
    void fail(std::string message)
    {
        if (!error_)
        {
            error_ = make_error(std::move(message));
        }
        cursor_ = bytes_.size();
    }

    void validate_size(const std::uint8_t size, const std::string_view label)
    {
        if (size != 1 && size != 2 && size != 4 && size != 8)
        {
            fail("unsupported Lua " + std::string(label) + " size");
        }
    }

    std::uint8_t read_byte()
    {
        if (cursor_ >= bytes_.size())
        {
            fail("truncated Lua bytecode");
            return 0;
        }
        return std::to_integer<std::uint8_t>(bytes_[cursor_++]);
    }

    void skip(const std::uint64_t size)
    {
        if (size > bytes_.size() - std::min(cursor_, bytes_.size()))
        {
            fail("truncated Lua bytecode");
            return;
        }
        cursor_ += static_cast<std::size_t>(size);
    }

    std::uint64_t read_unsigned(const std::uint8_t size)
    {
        if (size > bytes_.size() - std::min(cursor_, bytes_.size()))
        {
            fail("truncated Lua bytecode");
            return 0;
        }
        std::uint64_t value = 0;
        for (std::uint8_t index = 0; index < size; ++index)
        {
            const auto source = little_endian_ ? index : static_cast<std::uint8_t>(size - index - 1U);
            value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes_[cursor_ + source])) << (index * 8U);
        }
        cursor_ += size;
        return value;
    }

    std::uint64_t read_count()
    {
        const auto count = read_unsigned(integer_size_);
        if (count > maximum_count)
        {
            fail("unreasonable Lua bytecode count");
            return 0;
        }
        return count;
    }

    std::string read_string()
    {
        const auto size = read_unsigned(size_type_size_);
        if (size == 0)
        {
            return {};
        }
        if (size > bytes_.size() - std::min(cursor_, bytes_.size()))
        {
            fail("truncated Lua bytecode string");
            return {};
        }
        if (std::to_integer<unsigned char>(bytes_[cursor_ + static_cast<std::size_t>(size) - 1U]) != 0)
        {
            fail("Lua bytecode string is not null terminated");
            return {};
        }
        std::string value(reinterpret_cast<const char*>(bytes_.data() + cursor_), static_cast<std::size_t>(size) - 1U);
        cursor_ += static_cast<std::size_t>(size);
        return value;
    }

    void read_function(LuaChunkData& result, const std::size_t depth)
    {
        if (depth > 1024)
        {
            fail("Lua bytecode prototype nesting is too deep");
            return;
        }
        if (++result.prototypes > maximum_count)
        {
            fail("unreasonable Lua prototype count");
            return;
        }
        static_cast<void>(read_string());
        static_cast<void>(read_unsigned(integer_size_));
        if (petroglyph_)
        {
            result.persistent_function_ids.push_back(read_unsigned(integer_size_));
        }
        skip(4);

        const auto line_count = read_count();
        skip(line_count * integer_size_);

        const auto local_count = read_count();
        for (std::uint64_t index = 0; index < local_count && !failed(); ++index)
        {
            static_cast<void>(read_string());
            skip(static_cast<std::uint64_t>(integer_size_) * 2U);
        }
        const auto upvalue_count = read_count();
        for (std::uint64_t index = 0; index < upvalue_count && !failed(); ++index)
        {
            static_cast<void>(read_string());
        }

        const auto constant_count = read_count();
        for (std::uint64_t index = 0; index < constant_count && !failed(); ++index)
        {
            switch (read_byte())
            {
            case 0:
                break;
            case 3:
                skip(number_size_);
                ++result.number_constants;
                break;
            case 4:
                static_cast<void>(read_string());
                ++result.string_constants;
                break;
            default:
                fail("unsupported Lua bytecode constant type");
                break;
            }
        }
        const auto child_count = read_count();
        for (std::uint64_t index = 0; index < child_count && !failed(); ++index)
        {
            read_function(result, depth + 1U);
        }
        if (failed())
        {
            return;
        }

        const auto instruction_count = read_count();
        if (instruction_count > maximum_count - result.instructions)
        {
            fail("unreasonable Lua instruction count");
            return;
        }
        result.instructions += static_cast<std::size_t>(instruction_count);
        skip(instruction_count * instruction_size_);
    }

    std::span<const std::byte> bytes_;
    bool petroglyph_{};
    bool little_endian_{};
    std::uint8_t integer_size_{};
    std::uint8_t size_type_size_{};
    std::uint8_t instruction_size_{};
    std::uint8_t number_size_{};
    std::size_t cursor_{};
    std::optional<Error> error_;
};

} // namespace

Result<LuaChunkData> read_lua(const std::span<const std::byte> bytes)
{
    const bool binary_signature = bytes.size() >= 4 && std::to_integer<unsigned char>(bytes[0]) == 0x1bU && std::to_integer<char>(bytes[1]) == 'L' && std::to_integer<char>(bytes[2]) == 'u';
    if (binary_signature && std::to_integer<char>(bytes[3]) == 'a')
    {
        return BytecodeReader(bytes, false).read();
    }
    if (binary_signature && std::to_integer<char>(bytes[3]) == 'p')
    {
        return BytecodeReader(bytes, true).read();
    }

    LuaChunkData result;
    if (std::ranges::find(bytes, std::byte{}) != bytes.end())
    {
        result.kind = LuaScriptKind::unknown_binary;
        result.warnings.push_back("script contains null bytes but has no recognized Lua signature");
        return result;
    }
    result.kind = LuaScriptKind::source;
    inspect_source(bytes, result);
    return result;
}

std::string_view to_string(const LuaScriptKind kind) noexcept
{
    switch (kind)
    {
    case LuaScriptKind::source:
        return "Lua source";
    case LuaScriptKind::lua50_bytecode:
        return "Lua 5.0 bytecode";
    case LuaScriptKind::petroglyph_bytecode:
        return "Petroglyph Lua 5.0 bytecode";
    case LuaScriptKind::unknown_binary:
        return "unknown binary Lua";
    }
    return "unknown Lua";
}

} // namespace eaw
