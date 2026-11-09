#include "eaw/log_sinks.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>
#include <system_error>
#include <vector>

#include <windows.h>

namespace eaw {
namespace {

std::wstring to_wide(const std::string_view text)
{
    if (text.empty())
    {
        return {};
    }
    const auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(std::max(size, 0)), L'\0');
    if (size > 0)
    {
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    }
    return result;
}

std::filesystem::path executable_directory()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;)
    {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return std::filesystem::current_path();
        }
        if (length < buffer.size())
        {
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        }
        buffer.resize(buffer.size() * 2U);
    }
}

std::string timestamp()
{
    const auto seconds = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &seconds);
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%04d%02d%02d-%02d%02d%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    return text.data();
}

// Deletes this program's oldest log files so at most `keep` remain (names sort by timestamp).
void prune_logs(const std::filesystem::path& directory, const std::string& prefix, const std::size_t keep)
{
    std::error_code error;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator entry(directory, error), end; !error && entry != end; entry.increment(error))
    {
        const auto name = entry->path().filename().string();
        if (name.starts_with(prefix) && entry->path().extension() == ".log")
        {
            files.push_back(entry->path());
        }
    }
    if (files.size() <= keep)
    {
        return;
    }
    std::ranges::sort(files);
    for (std::size_t index = 0; index + keep < files.size(); ++index)
    {
        std::filesystem::remove(files[index], error);
    }
}

} // namespace

Result<std::filesystem::path> install_log_sinks(const LogSinkOptions& options)
{
    if (options.console)
    {
        add_log_sink(
            [](const LogRecord& record)
            {
                if (record.level != LogLevel::info)
                {
                    const auto text = format_log_record(record);
                    std::fprintf(stderr, "%s\n", text.c_str());
                }
            });
    }
    add_log_sink(
        [](const LogRecord& record)
        {
            const auto text = to_wide(format_log_record(record) + "\n");
            OutputDebugStringW(text.c_str());
        });

    const auto directory = options.directory.empty() ? executable_directory() / "logs" : options.directory;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        return make_error("cannot create the log folder " + directory.string() + ": " + error.message());
    }
    const auto prefix = options.application_name + "-";
    prune_logs(directory, prefix, options.keep_files > 0 ? options.keep_files - 1U : 0U);
    // Timestamp plus process id: runs started within the same second get separate files.
    const auto path = directory / (prefix + timestamp() + "-" + std::to_string(GetCurrentProcessId()) + ".log");
    std::FILE* raw_file = nullptr;
    // "x": never truncate an existing file.
    if (_wfopen_s(&raw_file, path.c_str(), L"wbx") != 0 || raw_file == nullptr)
    {
        return make_error("cannot create the log file " + path.string());
    }
    // Shared by the sink; closed when the sinks are cleared or the process ends.
    const std::shared_ptr<std::FILE> file(raw_file,
                                          [](std::FILE* handle)
                                          {
                                              std::fclose(handle);
                                          });
    add_log_sink(
        [file](const LogRecord& record)
        {
            const auto text = format_log_record(record);
            std::fprintf(file.get(), "%s\n", text.c_str());
            // Flushed per record so the file is complete even when the process crashes.
            std::fflush(file.get());
        });
    return path;
}

} // namespace eaw
