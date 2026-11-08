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

const ModuleRegistrar registrar("foundation", {
                                                  {"test_fatal_error_report", &test_fatal_error_report},
                                                  {"test_status_and_log", &test_status_and_log},
                                              });

} // namespace
