#include "eaw/xml_asset_database.hpp"

#include "eaw/asset_path.hpp"
#include "core/string_utils.hpp"
#include "formats/xml_helpers.hpp"

#include <algorithm>
#include <filesystem>
#include <map>

namespace eaw {
namespace {

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

} // namespace

XmlAssetDatabase XmlAssetDatabase::build(const AssetReader& vfs, const std::string_view prefix)
{
    XmlAssetDatabase database;
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
        for (const auto& root : document->roots)
        {
            collect_node(vfs, root, asset.virtual_path, database.definitions_, database.includes_, 0);
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
