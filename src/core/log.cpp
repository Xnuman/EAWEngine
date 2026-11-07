#include "eaw/log.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace eaw {
namespace {

std::mutex& log_mutex() noexcept
{
    static std::mutex mutex;
    return mutex;
}

std::vector<LogSink>& sinks() noexcept
{
    static std::vector<LogSink> instance;
    return instance;
}

std::array<std::atomic<std::size_t>, 3> counts{};

void write_stderr(const LogRecord& record)
{
    if (record.level == LogLevel::info)
    {
        return;
    }
    const auto text = format_log_record(record);
    std::fprintf(stderr, "%s\n", text.c_str());
}

void dispatch(const LogRecord& record)
{
    auto& installed = sinks();
    if (installed.empty())
    {
        write_stderr(record);
        return;
    }
    for (const auto& sink : installed)
    {
        sink(record);
    }
}

} // namespace

std::string_view to_string(const LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::info:
        return "info";
    case LogLevel::warning:
        return "warning";
    case LogLevel::error:
        return "error";
    }
    return "unknown";
}

void add_log_sink(LogSink sink)
{
    std::scoped_lock lock(log_mutex());
    sinks().push_back(std::move(sink));
}

void clear_log_sinks()
{
    std::scoped_lock lock(log_mutex());
    sinks().clear();
}

void log(const LogLevel level, const std::string_view category, const std::string_view message)
{
    counts[static_cast<std::size_t>(level)].fetch_add(1, std::memory_order_relaxed);
    std::scoped_lock lock(log_mutex());
    dispatch({level, category, message});
}

std::size_t log_count(const LogLevel level) noexcept
{
    return counts[static_cast<std::size_t>(level)].load(std::memory_order_relaxed);
}

std::string format_log_record(const LogRecord& record)
{
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    std::array<char, 32> time{};
    std::snprintf(time.data(), time.size(), "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min, local.tm_sec, static_cast<int>(milliseconds));
    std::string text;
    text.reserve(record.message.size() + record.category.size() + 32);
    text += '[';
    text += time.data();
    text += "] [";
    text += to_string(record.level);
    text += "] ";
    if (!record.category.empty())
    {
        text += '[';
        text += record.category;
        text += "] ";
    }
    text += record.message;
    return text;
}

bool log_fatal_report(const std::string_view report) noexcept
{
    std::unique_lock lock(log_mutex(), std::try_to_lock);
    if (!lock.owns_lock() || sinks().empty())
    {
        return false;
    }
    for (const auto& sink : sinks())
    {
        sink({LogLevel::error, "fatal", report});
    }
    return true;
}

} // namespace eaw
