#include "test_support.hpp"

namespace {

using namespace eaw_test;

void test_fatal_error_report()
{
    // fatal_error itself ends the process, so only the report format is testable in-process.
    const auto location = std::source_location::current();
    const auto report = eaw::format_fatal_error("Bad state", location);
    require(report.starts_with("Bad state\n\nFile: ") && report.find(location.file_name()) != std::string::npos && report.find("Line: " + std::to_string(location.line())) != std::string::npos &&
                report.find("Function: ") != std::string::npos,
            "a fatal error report must name the message, file, line, and function");
    EAW_ASSERT(report.size() > 10);
    EAW_ASSERT_MSG(!report.empty(), "passing assertions must not end the process");
}

eaw::Result<int> parse_positive(const int value)
{
    if (value <= 0)
    {
        return eaw::make_error("value " + std::to_string(value) + " is not positive");
    }
    return value;
}

eaw::Result<int> doubled_positive(const int value)
{
    EAW_TRY_ASSIGN(const auto checked, parse_positive(value));
    return checked * 2;
}

eaw::Status require_positive(const int value)
{
    EAW_TRY(parse_positive(value));
    return eaw::Status::success();
}

void test_status_and_log()
{
    const auto good = doubled_positive(4);
    require(good.ok() && *good == 8 && good.value_or(0) == 8, "a successful Result must carry its value");
    const auto bad = doubled_positive(-1);
    require(!bad && bad.error().message == "value -1 is not positive" && bad.value_or(7) == 7, "a failure must propagate unchanged through EAW_TRY_ASSIGN");
    require(require_positive(3).ok() && !require_positive(0).ok() && require_positive(0).error().message.find("not positive") != std::string::npos, "EAW_TRY must return the failed status");
    require(eaw::with_context("reading X.ALO", eaw::make_error("truncated")).message == "reading X.ALO: truncated", "context must prefix the message");

    // The log hands records to every sink and counts them by level.
    std::vector<std::string> captured;
    eaw::clear_log_sinks();
    eaw::add_log_sink(
        [&captured](const eaw::LogRecord& record)
        {
            captured.push_back(std::string(eaw::to_string(record.level)) + "|" + std::string(record.category) + "|" + std::string(record.message));
        });
    const auto warnings_before = eaw::log_count(eaw::LogLevel::warning);
    eaw::log_warning("test", "first");
    eaw::log_warning("test", eaw::make_error("second"));
    eaw::log_info("test", "third");
    eaw::clear_log_sinks();
    require(captured == std::vector<std::string>{"warning|test|first", "warning|test|second", "info|test|third"} && eaw::log_count(eaw::LogLevel::warning) == warnings_before + 2,
            "log records must reach sinks in order and be counted");
    const auto line = eaw::format_log_record({eaw::LogLevel::warning, "map", "bad placement"});
    require(line.starts_with("[") && line.ends_with("] [warning] [map] bad placement"), "log lines must carry time, level, category, and message");
}

void test_string_utils()
{
    require(eaw::ascii_lower("Data/ART\\Models_01.ALO") == "data/art\\models_01.alo" && eaw::ascii_upper("Gas_Cloud-1") == "GAS_CLOUD-1", "ASCII case folding must change only letters");
    require(eaw::ascii_lower("\xC4\xD6") == "\xC4\xD6" && eaw::ascii_upper('q') == 'Q' && eaw::ascii_lower('7') == '7', "bytes outside A-Z and a-z must stay as they are");
    require(eaw::ascii_trim(" \t\r\n\v\fvalue \t") == "value" && eaw::ascii_trim(" \t ").empty() && eaw::ascii_trim("a b") == "a b", "trimming must remove only outer ASCII whitespace");
    require(eaw::ascii_equal_ignoring_case("TIE_Fighter", "tie_FIGHTER") && !eaw::ascii_equal_ignoring_case("TIE", "TIES") && !eaw::ascii_equal_ignoring_case("a", "b"),
            "case-insensitive equality must compare every character and the length");
    require(eaw::ascii_find_ignoring_case("Data/Path=x; PATH=y", "path", 6) == 13 && eaw::ascii_find_ignoring_case("abc", "") == 0 &&
                eaw::ascii_find_ignoring_case("ab", "abc") == std::string_view::npos && eaw::ascii_is_space('\v') && !eaw::ascii_is_space('_'),
            "case-insensitive search and the whitespace test");
    std::string encoded;
    require(eaw::append_utf8(encoded, U'\x20AC') && encoded == "\xE2\x82\xAC" && !eaw::append_utf8(encoded, U'\xD800') && !eaw::append_utf8(encoded, 0x110000U) && encoded.size() == 3,
            "append_utf8 must encode valid code points and refuse surrogates and values past U+10FFFF");
    const auto utf16 = eaw::utf8_to_utf16("A\xC3\xA9\xF0\x9F\x9A\x80");
    require(utf16 && utf16->size() == 4 && eaw::utf16_to_utf8(*utf16) == "A\xC3\xA9\xF0\x9F\x9A\x80", "UTF-8 and UTF-16 must round-trip, surrogate pairs included");
    require(!eaw::utf8_to_utf16("\xC0\x80") && !eaw::utf8_to_utf16("\xED\xA0\x80") && !eaw::utf8_to_utf16("\xE2\x82") && !eaw::utf16_to_utf8(std::u16string(1, u'\xDC00')),
            "overlong forms, encoded surrogates, truncated sequences, and lone low surrogates must give no result");
}

const ModuleRegistrar registrar("foundation", {
                                                  {"test_fatal_error_report", &test_fatal_error_report},
                                                  {"test_status_and_log", &test_status_and_log},
                                                  {"test_string_utils", &test_string_utils},
                                              });

} // namespace
