#include "vfs/game_installation.hpp"

#include "core/file_io.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <system_error>
#include <tuple>

#ifdef _WIN32
#include <windows.h>
#endif

namespace eaw {
namespace {

bool looks_like_installation(const std::filesystem::path& root)
{
    std::error_code error;
    return std::filesystem::is_directory(root / "GameData" / "Data", error) && std::filesystem::is_directory(root / "corruption" / "Data", error);
}

std::optional<std::string> environment_value(const char* name)
{
#ifdef _WIN32
    std::size_t required = 0;
    if (getenv_s(&required, nullptr, 0, name) != 0 || required == 0)
    {
        return std::nullopt;
    }
    std::string value(required, '\0');
    if (getenv_s(&required, value.data(), value.size(), name) != 0)
    {
        return std::nullopt;
    }
    while (!value.empty() && value.back() == '\0')
    {
        value.pop_back();
    }
    return value.empty() ? std::nullopt : std::optional<std::string>(std::move(value));
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::nullopt : std::optional<std::string>(value);
#endif
}

// Every `"path" <whitespace> "<value>"` pair (key matched case-insensitively, value non-empty and
// ending at the next quote), as written in Steam's libraryfolders.vdf.
std::vector<std::string> vdf_path_values(const std::string_view contents)
{
    constexpr std::string_view key = "\"path\"";
    std::vector<std::string> values;
    auto position = ascii_find_ignoring_case(contents, key, 0);
    while (position != std::string_view::npos)
    {
        auto cursor = position + key.size();
        const auto whitespace_begin = cursor;
        while (cursor < contents.size() && ascii_is_space(contents[cursor]))
        {
            ++cursor;
        }
        const auto value_end = cursor < contents.size() && cursor != whitespace_begin && contents[cursor] == '"' ? contents.find('"', cursor + 1U) : std::string_view::npos;
        if (value_end == std::string_view::npos || value_end == cursor + 1U)
        {
            position = ascii_find_ignoring_case(contents, key, position + 1U);
            continue;
        }
        values.emplace_back(contents.substr(cursor + 1U, value_end - cursor - 1U));
        position = ascii_find_ignoring_case(contents, key, value_end + 1U);
    }
    return values;
}

// The trimmed text of every `<File>...</File>` element (tags matched case-insensitively; the text
// contains no '<'), as written in megafiles.xml.
// This is a second, minimal XML parser beside eaw_formats' read_xml, kept so the VFS does not depend
// on the format library; it does not handle comments, CDATA, entities, or UTF-16 manifests. Revisit
// (read_xml, or a VFS-owned manifest reader) if a mod's megafiles.xml needs any of them.
std::vector<std::string> manifest_file_entries(const std::string_view contents)
{
    constexpr std::string_view open_tag = "<file>";
    constexpr std::string_view close_tag = "</file>";
    std::vector<std::string> entries;
    auto position = ascii_find_ignoring_case(contents, open_tag, 0);
    while (position != std::string_view::npos)
    {
        const auto text_begin = position + open_tag.size();
        const auto text_end = contents.find('<', text_begin);
        if (text_end == std::string_view::npos || text_end == text_begin || ascii_find_ignoring_case(contents.substr(0, text_end + close_tag.size()), close_tag, text_end) != text_end)
        {
            position = ascii_find_ignoring_case(contents, open_tag, position + 1U);
            continue;
        }
        const auto text = ascii_trim(contents.substr(text_begin, text_end - text_begin));
        if (!text.empty())
        {
            entries.emplace_back(text);
        }
        position = ascii_find_ignoring_case(contents, open_tag, text_end + close_tag.size());
    }
    return entries;
}

std::vector<std::filesystem::path> steam_libraries_from_vdf(const std::filesystem::path& vdf_path)
{
    const auto file = read_text_file(vdf_path);
    const auto contents = file ? *file : std::string{};
    std::vector<std::filesystem::path> libraries;
    for (const auto& value : vdf_path_values(contents))
    {
        std::string unescaped;
        unescaped.reserve(value.size());
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            if (value[index] == '\\' && index + 1 < value.size() && value[index + 1] == '\\')
            {
                ++index;
            }
            unescaped.push_back(value[index]);
        }
        libraries.emplace_back(unescaped);
    }
    return libraries;
}

#ifdef _WIN32
std::optional<std::filesystem::path> steam_path_from_registry()
{
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    std::wstring value(32768, L'\0');
    DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    DWORD type = 0;
    const auto status = RegQueryValueExW(key, L"SteamPath", nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
    {
        return std::nullopt;
    }
    value.resize(bytes / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0')
    {
        value.pop_back();
    }
    return value.empty() ? std::nullopt : std::optional<std::filesystem::path>(value);
}
#endif

void append_if_unique(std::vector<std::filesystem::path>& output, const std::filesystem::path& candidate)
{
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(candidate, error);
    const auto path = error ? candidate.lexically_normal() : normalized;
    const auto duplicate = std::ranges::any_of(output,
                                               [&](const auto& existing)
                                               {
                                                   return existing == path;
                                               });
    if (!duplicate)
    {
        output.push_back(path);
    }
}

std::vector<std::filesystem::path> parse_meg_manifest(const std::filesystem::path& runtime_root)
{
    const auto manifest = read_text_file(runtime_root / "Data" / "megafiles.xml");
    const auto contents = manifest ? *manifest : std::string{};
    std::vector<std::filesystem::path> result;
    for (auto relative : manifest_file_entries(contents))
    {
        std::replace(relative.begin(), relative.end(), '\\', '/');
        auto archive = runtime_root / relative;
        std::error_code error;
        if (std::filesystem::is_regular_file(archive, error))
        {
            append_if_unique(result, archive);
        }
    }
    return result;
}

// Data/Audio/SFX, the sound archives' folder (any case).
bool is_sound_archive_folder(const std::filesystem::path& data_root, const std::filesystem::path& folder)
{
    std::error_code error;
    const auto relative = std::filesystem::relative(folder, data_root, error);
    return !error && ascii_lower(relative.generic_string()) == "audio/sfx";
}

} // namespace

std::string_view to_string(const GameVariant variant) noexcept
{
    switch (variant)
    {
    case GameVariant::empire_at_war:
        return "eaw";
    case GameVariant::forces_of_corruption:
        return "foc";
    }
    return "unknown";
}

std::optional<GameVariant> parse_game_variant(std::string_view text)
{
    std::string lowered(text);
    lowered = ascii_lower(lowered);
    if (lowered == "eaw" || lowered == "empire-at-war")
    {
        return GameVariant::empire_at_war;
    }
    if (lowered == "foc" || lowered == "forces-of-corruption")
    {
        return GameVariant::forces_of_corruption;
    }
    return std::nullopt;
}

std::optional<GameProfile> make_game_profile(const std::filesystem::path& root, const GameVariant variant)
{
    if (!looks_like_installation(root))
    {
        return std::nullopt;
    }
    const auto runtime = root / (variant == GameVariant::empire_at_war ? "GameData" : "corruption");
    return GameProfile{root, runtime, runtime / "Data", variant};
}

Status check_installation_root(const std::filesystem::path& root)
{
    if (looks_like_installation(root))
    {
        return {};
    }
    std::error_code error;
    if (std::filesystem::is_directory(root / "Data", error))
    {
        return make_error(root.string() + " is not an Empire at War installation; it has a Data folder, so it looks like a mod: add it as a mod instead");
    }
    return make_error(root.string() + " is not an Empire at War installation (it needs GameData\\Data and corruption\\Data)");
}

std::vector<std::filesystem::path> InstallationLocator::discover(const std::optional<std::filesystem::path>& explicit_root) const
{
    std::vector<std::filesystem::path> results;
    if (explicit_root && looks_like_installation(*explicit_root))
    {
        append_if_unique(results, *explicit_root);
    }

    if (const auto environment = environment_value("EAW_GAME_ROOT"); environment && looks_like_installation(*environment))
    {
        append_if_unique(results, *environment);
    }

    std::vector<std::filesystem::path> steam_roots;
#ifdef _WIN32
    if (const auto registry_path = steam_path_from_registry())
    {
        append_if_unique(steam_roots, *registry_path);
    }
    if (const auto program_files_x86 = environment_value("ProgramFiles(x86)"))
    {
        append_if_unique(steam_roots, std::filesystem::path(*program_files_x86) / "Steam");
    }
#endif

    for (const auto& steam_root : steam_roots)
    {
        auto libraries = steam_libraries_from_vdf(steam_root / "steamapps" / "libraryfolders.vdf");
        libraries.insert(libraries.begin(), steam_root);
        for (const auto& library : libraries)
        {
            const auto candidate = library / "steamapps" / "common" / "Star Wars Empire at War";
            if (looks_like_installation(candidate))
            {
                append_if_unique(results, candidate);
            }
        }
    }
    return results;
}

std::vector<std::filesystem::path> discover_meg_archives(const std::filesystem::path& runtime_root)
{
    auto archives = parse_meg_manifest(runtime_root);
    std::vector<std::filesystem::path> unlisted;
    std::vector<std::filesystem::path> patches;
    const auto data_root = runtime_root / "Data";
    std::error_code error;
    if (!std::filesystem::is_directory(data_root, error))
    {
        return archives;
    }

    // An unreadable directory ends the scan with what was found so far.
    for (std::filesystem::recursive_directory_iterator iterator(data_root, error), end; !error && iterator != end; iterator.increment(error))
    {
        const auto& item = *iterator;
        std::error_code type_error;
        // The sound archives store bare names and mount under their folder with the chosen
        // language (mount_game_assets), not as data archives.
        if (item.is_directory(type_error) && is_sound_archive_folder(data_root, item.path()))
        {
            iterator.disable_recursion_pending();
            continue;
        }
        if (!item.is_regular_file(type_error))
        {
            continue;
        }
        auto extension = item.path().extension().string();
        extension = ascii_lower(extension);
        if (extension != ".meg")
        {
            continue;
        }

        auto filename = item.path().filename().string();
        filename = ascii_lower(filename);
        const bool is_patch = filename.starts_with("patch") || filename.starts_with("64patch");
        (is_patch ? patches : unlisted).push_back(item.path());
    }

    const auto lexical = [](const auto& left, const auto& right)
    {
        return left.generic_string() < right.generic_string();
    };
    std::ranges::sort(unlisted, lexical);
    const auto patch_key = [](const std::filesystem::path& path)
    {
        auto stem = path.stem().string();
        stem = ascii_lower(stem);
        const auto is_64_bit = stem.starts_with("64patch");
        const std::string_view prefix = is_64_bit ? "64patch" : "patch";
        const auto suffix = std::string_view(stem).substr(prefix.size());
        std::uint64_t number = 0;
        if (!suffix.empty())
        {
            for (const char character : suffix)
            {
                if (character < '0' || character > '9')
                {
                    number = std::numeric_limits<std::uint64_t>::max();
                    break;
                }
                const auto digit = static_cast<std::uint64_t>(character - '0');
                if (number > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U)
                {
                    number = std::numeric_limits<std::uint64_t>::max();
                    break;
                }
                number = number * 10U + digit;
            }
        }
        return std::tuple{is_64_bit ? 1 : 0, number, stem};
    };
    std::ranges::sort(patches,
                      [&](const auto& left, const auto& right)
                      {
                          const auto left_key = patch_key(left);
                          const auto right_key = patch_key(right);
                          return left_key == right_key ? lexical(left, right) : left_key < right_key;
                      });
    for (const auto& archive : unlisted)
    {
        append_if_unique(archives, archive);
    }
    for (const auto& archive : patches)
    {
        append_if_unique(archives, archive);
    }
    return archives;
}

} // namespace eaw
