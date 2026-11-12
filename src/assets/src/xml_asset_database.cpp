#include "assets/xml_asset_database.hpp"

#include "core/string_utils.hpp"
#include "vfs/asset_path.hpp"
#include "formats/xml_helpers.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace eaw {
namespace {

// How the game loads its XML, from the public eaw-schema (Alamo Engine Tools, MIT,
// eaw/meta/metafiles.yaml; docs/compatibility-research.md, "XML loading"): registries list the files
// of one kind, loaded in list order, each with a rule for a name two of its files define; some files
// are opened by name; some folders are walked whole.
enum class DuplicateRule
{
    // The first definition keeps the name (firstWins, firstByName).
    first,
    // A later definition replaces it (lastWins).
    last,
    // A later definition is parsed onto the earlier one (merge; read here as first, see the docs).
    merge,
};

struct Registry
{
    std::string_view file;
    DuplicateRule rule;
};

constexpr std::array registries{
    Registry{"GameObjectFiles.xml", DuplicateRule::first},           Registry{"SFXEventFiles.xml", DuplicateRule::first},  Registry{"FactionFiles.xml", DuplicateRule::first},
    Registry{"CommandBarComponentFiles.xml", DuplicateRule::last},   Registry{"CampaignFiles.xml", DuplicateRule::last},   Registry{"HardPointDataFiles.xml", DuplicateRule::last},
    Registry{"TargetingPrioritySetFiles.xml", DuplicateRule::merge}, Registry{"TradeRouteFiles.xml", DuplicateRule::last}, Registry{"MousePointerFiles.xml", DuplicateRule::last},
};

// Files the game opens by name (the schema's singleton and directContent files).
constexpr std::array<std::string_view, 26> opened_by_name{
    "GameConstants.xml",         "HeroClash.xml",           "TerrainDecalFX.xml", "LightningEffectTypes.xml", "DynamicTrackFX.xml",         "MusicEvents.xml",  "SpeechEvents.xml",
    "ShadowBlobMaterials.xml",   "TradeRouteLines.xml",     "RadarMap.xml",       "WeatherScenarios.xml",     "TacticalCameras.xml",        "LightSources.xml", "Movies.xml",
    "DifficultyAdjustments.xml", "WeatherAudio.xml",        "Audio.xml",          "GraphicDetails.xml",       "AITerrainEffectiveness.xml", "GUIDialogs.xml",   "LensFlares.xml",
    "MovementClassTypeDefs.xml", "StarWars3DTextCrawl.xml", "SurfaceFX.xml",      "UnitAbilityTypes.xml",     "WeatherModifiers.xml",
};

// Folders the game walks, loading every file in them.
constexpr std::array<std::string_view, 8> walked_folders{
    "Enum/", "AI/GalacticMarkup/", "AI/GoalFunctions/", "AI/Goals/", "AI/HintSets/", "AI/PerceptualEquations/", "AI/Players/", "AI/Templates/",
};

bool xml_extension(const std::string_view path)
{
    return ascii_lower(std::filesystem::path(path).extension().string()) == ".xml";
}

Result<std::string> include_path(const std::string_view value)
{
    auto candidate = std::string(value);
    std::ranges::replace(candidate, '\\', '/');
    if (!ascii_lower(candidate).starts_with("data/"))
    {
        candidate = "Data/XML/" + candidate;
    }
    return normalize_asset_path(candidate);
}

// The case-folded normalized path of a file under Data/XML (the key files are matched by).
std::string xml_key(const std::string_view relative)
{
    auto normalized = normalize_asset_path("Data/XML/" + std::string(relative));
    return normalized ? ascii_lower(*normalized) : std::string{};
}

void collect_node(const AssetReader& vfs, const XmlNode& node, const std::string& source_path, std::vector<XmlDefinition>& definitions, std::vector<XmlIncludeReference>& includes,
                  const std::size_t depth)
{
    if (const auto* name = xml_attribute(node, "Name"); name != nullptr && !ascii_trim(name->value).empty())
    {
        definitions.push_back(XmlDefinition{node.name, std::string(ascii_trim(name->value)), source_path, node.line, depth, node.attributes, node.children});
    }

    const auto value = ascii_trim(node.text);
    if (node.children.empty() && xml_extension(value))
    {
        // A value ending in .xml can still be a non-path token. It is not an
        // include if it cannot enter the normalized VFS namespace.
        if (auto path = include_path(value))
        {
            const bool resolved = vfs.resolve(*path).has_value();
            includes.push_back(XmlIncludeReference{source_path, node.line, std::string(value), std::move(path).value(), resolved});
        }
    }
    for (const auto& child : node.children)
    {
        collect_node(vfs, child, source_path, definitions, includes, depth + 1);
    }
}

// One parsed file: its definitions in node order and the XML files it names.
struct ParsedFile
{
    std::string path;
    std::vector<XmlDefinition> definitions;
    std::vector<XmlIncludeReference> includes;
};

} // namespace

XmlAssetDatabase XmlAssetDatabase::build(const AssetReader& vfs, const std::string_view prefix)
{
    XmlAssetDatabase database;
    std::vector<ParsedFile> files;
    for (const auto& asset : vfs.list(prefix))
    {
        if (!xml_extension(asset.virtual_path))
        {
            continue;
        }
        ++database.document_count_;
        const auto bytes = vfs.read(asset.virtual_path);
        if (!bytes)
        {
            database.failures_.push_back({asset.virtual_path, bytes.error().message});
            continue;
        }
        const auto document = read_xml(std::span<const std::byte>(*bytes));
        if (!document)
        {
            database.failures_.push_back({asset.virtual_path, document.error().message});
            continue;
        }
        ++database.parsed_document_count_;
        ParsedFile file{asset.virtual_path, {}, {}};
        for (const auto& root : document->roots)
        {
            collect_node(vfs, root, asset.virtual_path, file.definitions, file.includes, 0);
        }
        database.includes_.insert(database.includes_.end(), file.includes.begin(), file.includes.end());
        files.push_back(std::move(file));
    }

    std::unordered_map<std::string, std::size_t> by_key;
    for (std::size_t index = 0; index < files.size(); ++index)
    {
        if (auto normalized = normalize_asset_path(files[index].path))
        {
            by_key.emplace(ascii_lower(*normalized), index);
        }
    }
    const auto find_file = [&](const std::string_view key) -> std::optional<std::size_t>
    {
        const auto found = by_key.find(std::string(key));
        return found == by_key.end() ? std::nullopt : std::optional<std::size_t>{found->second};
    };

    // Files in load order: each registry's entries (a last-wins registry's in reverse, so the first
    // definition a lookup finds is the winner either way), then every other file in path order.
    struct Loaded
    {
        std::size_t file{};
        bool reversed{};
    };
    std::vector<Loaded> order;
    std::vector<bool> emitted(files.size(), false);
    std::vector<bool> reached(files.size(), false);
    // Files reached through a registry entry or a reference (their element types are loaded only
    // through lists), as against files opened by name or walked.
    std::vector<bool> listed(files.size(), false);
    bool registries_present = false;
    for (const auto& registry : registries)
    {
        const auto registry_file = find_file(xml_key(registry.file));
        if (!registry_file)
        {
            continue;
        }
        registries_present = true;
        reached[*registry_file] = true;
        std::vector<std::size_t> entries;
        for (const auto& include : files[*registry_file].includes)
        {
            const auto entry = find_file(ascii_lower(include.resolved_path));
            if (!entry)
            {
                database.missing_registry_entries_.push_back(include);
                continue;
            }
            if (std::ranges::find(entries, *entry) == entries.end())
            {
                entries.push_back(*entry);
            }
        }
        if (registry.rule == DuplicateRule::last)
        {
            std::ranges::reverse(entries);
        }
        for (const auto entry : entries)
        {
            reached[entry] = listed[entry] = true;
            if (!emitted[entry])
            {
                emitted[entry] = true;
                order.push_back({entry, registry.rule == DuplicateRule::last});
            }
        }
    }

    if (registries_present)
    {
        for (const auto name : opened_by_name)
        {
            if (const auto file = find_file(xml_key(name)))
            {
                reached[*file] = true;
            }
        }
        for (std::size_t index = 0; index < files.size(); ++index)
        {
            const auto key = ascii_lower(files[index].path);
            for (const auto folder : walked_folders)
            {
                if (key.starts_with(xml_key(folder) + "/"))
                {
                    reached[index] = true;
                }
            }
        }
        // References (a campaign's story files, their plots) reach further files, transitively.
        std::vector<std::size_t> pending;
        for (std::size_t index = 0; index < files.size(); ++index)
        {
            if (reached[index])
            {
                pending.push_back(index);
            }
        }
        while (!pending.empty())
        {
            const auto current = pending.back();
            pending.pop_back();
            for (const auto& include : files[current].includes)
            {
                if (const auto target = find_file(ascii_lower(include.resolved_path)); target && !reached[*target])
                {
                    reached[*target] = listed[*target] = true;
                    pending.push_back(*target);
                }
            }
        }
    }

    // Element types the game loads through lists: a definition of one in a file nothing reaches is
    // not loaded (an EAW campaign under FoC, a mod's leftover unit file).
    std::unordered_set<std::string> listed_types;
    for (std::size_t index = 0; index < files.size(); ++index)
    {
        if (listed[index])
        {
            for (const auto& definition : files[index].definitions)
            {
                listed_types.insert(ascii_lower(definition.type));
            }
        }
    }
    for (std::size_t index = 0; index < files.size(); ++index)
    {
        if (!emitted[index])
        {
            order.push_back({index, false});
        }
        if (registries_present && !reached[index])
        {
            database.unreached_files_.push_back(files[index].path);
        }
    }

    for (const auto& [index, reversed] : order)
    {
        auto& definitions = files[index].definitions;
        if (reversed)
        {
            std::ranges::reverse(definitions);
        }
        for (auto& definition : definitions)
        {
            if (registries_present && !reached[index] && listed_types.contains(ascii_lower(definition.type)))
            {
                ++database.unreached_definition_count_;
                continue;
            }
            database.definitions_.push_back(std::move(definition));
        }
    }

    std::map<std::pair<std::string, std::string>, std::vector<const XmlDefinition*>> definitions_by_key;
    for (const auto& definition : database.definitions_)
    {
        definitions_by_key[{ascii_lower(definition.type), ascii_lower(definition.name)}].push_back(&definition);
    }
    for (const auto& [key, matches] : definitions_by_key)
    {
        (void)key;
        if (matches.size() < 2)
        {
            continue;
        }
        XmlDuplicateDefinition duplicate{matches.front()->type, matches.front()->name, {}};
        duplicate.occurrences.reserve(matches.size());
        for (const auto* definition : matches)
        {
            duplicate.occurrences.push_back({definition->source_path, definition->line});
        }
        database.duplicates_.push_back(std::move(duplicate));
    }
    return database;
}

std::size_t XmlAssetDatabase::document_count() const noexcept
{
    return document_count_;
}

std::size_t XmlAssetDatabase::parsed_document_count() const noexcept
{
    return parsed_document_count_;
}

std::span<const XmlDefinition> XmlAssetDatabase::definitions() const noexcept
{
    return definitions_;
}

std::span<const XmlParseFailure> XmlAssetDatabase::failures() const noexcept
{
    return failures_;
}

std::span<const XmlIncludeReference> XmlAssetDatabase::includes() const noexcept
{
    return includes_;
}

std::span<const XmlDuplicateDefinition> XmlAssetDatabase::duplicates() const noexcept
{
    return duplicates_;
}

std::span<const XmlIncludeReference> XmlAssetDatabase::missing_registry_entries() const noexcept
{
    return missing_registry_entries_;
}

std::span<const std::string> XmlAssetDatabase::unreached_files() const noexcept
{
    return unreached_files_;
}

std::size_t XmlAssetDatabase::unreached_definition_count() const noexcept
{
    return unreached_definition_count_;
}

std::vector<const XmlDefinition*> XmlAssetDatabase::find(const std::string_view type, const std::string_view name) const
{
    const auto folded_type = ascii_lower(type);
    const auto folded_name = ascii_lower(name);
    std::vector<const XmlDefinition*> result;
    for (const auto& definition : definitions_)
    {
        if (ascii_lower(definition.type) == folded_type && ascii_lower(definition.name) == folded_name)
        {
            result.push_back(&definition);
        }
    }
    return result;
}

} // namespace eaw
