#include "eaw/localization_validation.hpp"

#include "core/string_utils.hpp"
#include "formats/dat_string_table_helpers.hpp"

#include <algorithm>

namespace eaw {
namespace {

bool is_digit(const char character) noexcept
{
    return character >= '0' && character <= '9';
}

bool is_ascii_letter(const char character) noexcept
{
    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
}

std::size_t skip_digits(const std::string& text, std::size_t position) noexcept
{
    while (position < text.size() && is_digit(text[position]))
    {
        ++position;
    }
    return position;
}

// Length of a printf conversion "%[n$][flags][width|*][.precision]letter" (or "%%") starting at
// `start`, or zero when none starts there.
std::size_t printf_token_length(const std::string& text, const std::size_t start) noexcept
{
    std::size_t position = start + 1;
    // Positional argument "n$". When digits are not followed by '$' they are flags/width instead;
    // when they are, no conversion without the position can match either ('$' is not a letter).
    if (const auto digits_end = skip_digits(text, position); digits_end > position && digits_end < text.size() && text[digits_end] == '$')
    {
        position = digits_end + 1;
    }
    while (position < text.size() && (text[position] == '-' || text[position] == '+' || text[position] == '0' || text[position] == '#' || text[position] == ' '))
    {
        ++position;
    }
    if (position < text.size() && text[position] == '*')
    {
        ++position;
    }
    else
    {
        position = skip_digits(text, position);
    }
    if (position + 1 < text.size() && text[position] == '.' && is_digit(text[position + 1]))
    {
        position = skip_digits(text, position + 1);
    }
    if (position < text.size() && (is_ascii_letter(text[position]) || text[position] == '%'))
    {
        return position + 1 - start;
    }
    return 0;
}

// Length of a bracketed placeholder "[x]", "<x>", or "{x}" (non-empty, single line) at `start`.
std::size_t bracket_token_length(const std::string& text, const std::size_t start, const char close) noexcept
{
    std::size_t position = start + 1;
    while (position < text.size() && text[position] != close && text[position] != '\r' && text[position] != '\n')
    {
        ++position;
    }
    if (position < text.size() && text[position] == close && position > start + 1)
    {
        return position + 1 - start;
    }
    return 0;
}

bool edge_whitespace_matches(const std::u16string& reference, const std::u16string& translation)
{
    const auto whitespace = [](const char16_t character)
    {
        return character == u' ' || character == u'\t' || character == u'\r' || character == u'\n';
    };
    const auto leading = [&](const std::u16string& value)
    {
        return static_cast<std::size_t>(std::ranges::find_if_not(value, whitespace) - value.begin());
    };
    const auto trailing = [&](const std::u16string& value)
    {
        return static_cast<std::size_t>(std::ranges::find_if_not(value.rbegin(), value.rend(), whitespace) - value.rbegin());
    };
    return leading(reference) == leading(translation) && trailing(reference) == trailing(translation);
}

} // namespace

Result<std::multiset<std::string>> localization_formatting_tokens(const std::u16string_view text)
{
    const auto converted = utf16_to_utf8(text);
    if (!converted)
    {
        return make_error("localization text is not valid UTF-16");
    }
    const auto& utf8 = *converted;
    std::multiset<std::string> result;
    std::size_t position = 0;
    while (position < utf8.size())
    {
        std::size_t length = 0;
        switch (utf8[position])
        {
        case '%':
            length = printf_token_length(utf8, position);
            break;
        case '\\':
            length = position + 1 < utf8.size() && (utf8[position + 1] == 'n' || utf8[position + 1] == 'r') ? 2 : 0;
            break;
        case '[':
            length = bracket_token_length(utf8, position, ']');
            break;
        case '<':
            length = bracket_token_length(utf8, position, '>');
            break;
        case '{':
            length = bracket_token_length(utf8, position, '}');
            break;
        default:
            break;
        }
        if (length == 0)
        {
            ++position;
            continue;
        }
        result.insert(utf8.substr(position, length));
        position += length;
    }
    return result;
}

Result<std::vector<TranslationIssue>> compare_translation(const DatStringTableData& translation, const DatStringTableData& reference)
{
    std::vector<TranslationIssue> issues;
    for (const auto& original : reference.entries)
    {
        const auto* translated = dat_find(translation, original.key);
        if (translated == nullptr)
        {
            issues.push_back({DatIssueSeverity::error, original.key, "key is missing from translation"});
            continue;
        }
        EAW_TRY_ASSIGN(const auto original_tokens, localization_formatting_tokens(original.value));
        EAW_TRY_ASSIGN(const auto translated_tokens, localization_formatting_tokens(translated->value));
        if (original_tokens != translated_tokens)
        {
            issues.push_back({DatIssueSeverity::error, original.key, "formatting tokens differ from reference"});
        }
        if (!edge_whitespace_matches(original.value, translated->value))
        {
            issues.push_back({DatIssueSeverity::warning, original.key, "leading/trailing whitespace differs from reference"});
        }
    }
    for (const auto& translated : translation.entries)
    {
        if (dat_find(reference, translated.key) == nullptr)
        {
            issues.push_back({DatIssueSeverity::warning, translated.key, "key is not present in reference"});
        }
    }
    return issues;
}

} // namespace eaw
