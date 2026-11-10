#include "core/file_io.hpp"

#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace eaw {
namespace {

// The size of `path`, checked against `maximum_bytes` before anything is allocated.
Result<std::size_t> checked_size(const std::filesystem::path& path, const std::uint64_t maximum_bytes)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
    {
        return make_error("could not read " + path.string() + ": " + error.message());
    }
    if (size > maximum_bytes)
    {
        return make_error(path.string() + " is larger than " + std::to_string(maximum_bytes) + " bytes");
    }
    return static_cast<std::size_t>(size);
}

template <class Buffer> Result<Buffer> read_whole(const std::filesystem::path& path, const std::uint64_t maximum_bytes)
{
    EAW_TRY_ASSIGN(const auto size, checked_size(path, maximum_bytes));
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return make_error("could not open " + path.string());
    }
    Buffer buffer(size, typename Buffer::value_type{});
    stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    if (static_cast<std::size_t>(stream.gcount()) != buffer.size())
    {
        return make_error("could not read " + path.string());
    }
    return buffer;
}

} // namespace

Result<std::vector<std::byte>> read_file(const std::filesystem::path& path, const std::uint64_t maximum_bytes)
{
    return read_whole<std::vector<std::byte>>(path, maximum_bytes);
}

Result<std::string> read_text_file(const std::filesystem::path& path, const std::uint64_t maximum_bytes)
{
    return read_whole<std::string>(path, maximum_bytes);
}

Status write_file(const std::filesystem::path& path, const std::span<const std::byte> bytes, const WriteMode mode)
{
    std::error_code filesystem_error;
    if (mode == WriteMode::create_only && std::filesystem::exists(path, filesystem_error))
    {
        return make_error("output already exists: " + path.string());
    }
    if (const auto parent = path.parent_path(); !parent.empty())
    {
        std::filesystem::create_directories(parent, filesystem_error);
        if (filesystem_error)
        {
            return make_error("could not create the directory " + parent.string() + ": " + filesystem_error.message());
        }
    }

    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return make_error("could not create " + temporary.string());
        }
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream)
        {
            std::error_code ignored;
            stream.close();
            std::filesystem::remove(temporary, ignored);
            return make_error("could not write " + temporary.string());
        }
    }
#ifdef _WIN32
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (mode == WriteMode::replace ? MOVEFILE_REPLACE_EXISTING : 0U);
    if (!MoveFileExW(temporary.c_str(), path.c_str(), flags))
    {
        const auto error = GetLastError();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return make_error("could not replace " + path.string() + " (Windows error " + std::to_string(error) + ")");
    }
#else
    std::filesystem::rename(temporary, path, filesystem_error);
    if (filesystem_error)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return make_error("could not replace " + path.string() + ": " + filesystem_error.message());
    }
#endif
    return Status::success();
}

Status write_text_file(const std::filesystem::path& path, const std::string_view text, const WriteMode mode)
{
    return write_file(path, std::as_bytes(std::span(text.data(), text.size())), mode);
}

} // namespace eaw
