// Helpers shared by the inspector commands.

#include "asset_inspector/inspector.hpp"

namespace eaw::inspector {

std::string extension_of(const std::string_view path)
{
    auto extension = std::filesystem::path(path).extension().string();
    extension = ascii_lower(extension);
    return extension.empty() ? "<none>" : extension;
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
