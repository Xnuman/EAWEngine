// Helpers shared by the inspector commands.

#include "asset_inspector/inspector.hpp"

namespace eaw::inspector {

std::string extension_of(const std::string_view path)
{
    auto extension = std::filesystem::path(path).extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return extension.empty() ? "<none>" : extension;
}

std::string lower_ascii(std::string value)
{
    std::ranges::transform(value, value.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return value;
}

std::uint32_t little_u32(const std::span<const std::byte> bytes, const std::size_t offset)
{
    EAW_ASSERT_MSG(offset + 4 <= bytes.size(), "asset is too short for the requested header");
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index)
    {
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + index])) << (index * 8U);
    }
    return value;
}

int report_error(const eaw::Error& error)
{
    std::cerr << "asset inspector error: " << error.message << '\n';
    return 1;
}

void record_example(std::map<std::string, AuditExample>& entries, const std::string& key, const std::string& map, const std::string& detail)
{
    auto& entry = entries[key];
    if (entry.count++ == 0)
    {
        entry.map = map;
        entry.detail = detail;
    }
}

} // namespace eaw::inspector
