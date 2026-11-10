#pragma once

// Checks of a translated DAT against the DAT it translates: every key present, the same formatting
// tokens (printf conversions, "\\n"/"\\r" escapes, and [..], <..>, {..} placeholders), and the same
// leading and trailing whitespace.

#include "formats/dat_string_table.hpp"
#include "core/status.hpp"

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

struct TranslationIssue
{
    DatIssueSeverity severity{};
    std::string key;
    std::string message;
};

// The formatting tokens a translation must preserve, scanned left to right without overlap: printf
// conversions "%[n$][flags][width|*][.precision]letter" and "%%", the escapes "\\n" and "\\r", and
// non-empty single-line [..], <..>, and {..} placeholders. Errors report text that is not valid
// UTF-16.
[[nodiscard]] Result<std::multiset<std::string>> localization_formatting_tokens(std::u16string_view text);

// Issues of `translation` against `reference`, in reference order then translation order: a missing
// key or different formatting tokens are errors; different edge whitespace and keys the reference
// lacks are warnings.
[[nodiscard]] Result<std::vector<TranslationIssue>> compare_translation(const DatFile& translation, const DatFile& reference);

} // namespace eaw
