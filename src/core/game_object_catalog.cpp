#include "eaw/game_object_catalog.hpp"

#include "eaw/asset_path.hpp"
#include "eaw/virtual_file_system.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace eaw {
namespace {

// A stock variant chain is a handful of links; this only stops runaway or cyclic mod data.
constexpr std::size_t maximum_variant_depth = 64;

std::string fold_ascii(const std::string_view value)
{
    std::string result(value);
    std::ranges::transform(result, result.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return result;
}

bool equals_ascii(const std::string_view left, const std::string_view right) noexcept
{
    return std::ranges::equal(left, right,
                              [](const unsigned char a, const unsigned char b)
                              {
                                  return std::tolower(a) == std::tolower(b);
                              });
}

std::string_view trim(const std::string_view value) noexcept
{
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
    {
        ++first;
    }
    auto last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
    {
        --last;
    }
    return value.substr(first, last - first);
}

const XmlNode* direct_field(const XmlDefinition& definition, const std::string_view field_name) noexcept
{
    // The last occurrence wins, matching ordinary XML override expectations for repeated tags.
    const XmlNode* found = nullptr;
    for (const auto& field : definition.fields)
    {
        if (equals_ascii(field.name, field_name))
        {
            found = &field;
        }
    }
    return found;
}

std::string location(const XmlDefinition& definition)
{
    return definition.source_path + ":" + std::to_string(definition.line) + " " + definition.type + " '" + definition.name + "'";
}

std::span<const std::string_view> model_fields(const GameModelContext context) noexcept
{
    static constexpr std::array<std::string_view, 1> space{"Space_Model_Name"};
    static constexpr std::array<std::string_view, 1> land{"Land_Model_Name"};
    static constexpr std::array<std::string_view, 1> galactic{"Galactic_Model_Name"};
    switch (context)
    {
    case GameModelContext::space:
        return space;
    case GameModelContext::land:
        return land;
    case GameModelContext::galactic:
        return galactic;
    }
    return space;
}

} // namespace

std::uint32_t game_object_name_crc(const std::string_view name) noexcept
{
    std::uint32_t crc = 0xffffffffU;
    for (const unsigned char character : name)
    {
        crc ^= static_cast<unsigned char>(std::toupper(character));
        for (unsigned bit = 0; bit < 8; ++bit)
        {
            const auto mask = static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return crc ^ 0xffffffffU;
}

GameObjectCatalog GameObjectCatalog::build(const XmlAssetDatabase& database)
{
    GameObjectCatalog catalog;
    for (const auto& definition : database.definitions())
    {
        catalog.definitions_.push_back(&definition);
        catalog.by_name_[fold_ascii(definition.name)].push_back(&definition);
        catalog.by_crc_[game_object_name_crc(definition.name)].push_back(&definition);
    }
    return catalog;
}

std::size_t GameObjectCatalog::size() const noexcept
{
    return definitions_.size();
}

std::span<const XmlDefinition* const> GameObjectCatalog::find(const std::string_view name) const
{
    const auto found = by_name_.find(fold_ascii(trim(name)));
    return found == by_name_.end() ? std::span<const XmlDefinition* const>{} : std::span<const XmlDefinition* const>{found->second};
}

std::span<const XmlDefinition* const> GameObjectCatalog::find_crc(const std::uint32_t crc) const
{
    const auto found = by_crc_.find(crc);
    return found == by_crc_.end() ? std::span<const XmlDefinition* const>{} : std::span<const XmlDefinition* const>{found->second};
}

GameObjectFieldLookup GameObjectCatalog::field(const XmlDefinition& definition, const std::string_view field_name, std::vector<std::string>* diagnostics) const
{
    std::vector<const XmlDefinition*> visited;
    const XmlDefinition* current = &definition;
    while (current != nullptr)
    {
        if (const auto* found = direct_field(*current, field_name))
        {
            return {found, current};
        }
        if (visited.size() >= maximum_variant_depth)
        {
            if (diagnostics != nullptr)
            {
                diagnostics->push_back(location(definition) + ": Variant_Of_Existing_Type chain exceeds " + std::to_string(maximum_variant_depth) + " links");
            }
            break;
        }
        visited.push_back(current);

        const auto* variant = direct_field(*current, "Variant_Of_Existing_Type");
        const auto parent_name = variant == nullptr ? std::string_view{} : trim(variant->text);
        if (parent_name.empty())
        {
            break;
        }
        const auto candidates = find(parent_name);
        const XmlDefinition* parent = nullptr;
        for (const auto* candidate : candidates)
        {
            if (candidate->type == current->type)
            {
                parent = candidate;
                break;
            }
        }
        if (parent == nullptr && !candidates.empty())
        {
            parent = candidates.front();
        }
        if (parent == nullptr)
        {
            if (diagnostics != nullptr)
            {
                diagnostics->push_back(location(*current) + ": variant parent '" + std::string(parent_name) + "' is not defined");
            }
            break;
        }
        if (std::ranges::find(visited, parent) != visited.end())
        {
            if (diagnostics != nullptr)
            {
                diagnostics->push_back(location(*current) + ": Variant_Of_Existing_Type cycle through '" + parent->name + "'");
            }
            break;
        }
        current = parent;
    }
    return {};
}

GameObjectTypeMatch GameObjectCatalog::resolve(const std::uint32_t crc, const GameModelContext context) const
{
    const auto candidates = find_crc(crc);
    if (candidates.empty())
    {
        return {};
    }
    for (const auto* candidate : candidates)
    {
        if (model(*candidate, context))
        {
            return {candidate, candidates.size()};
        }
    }
    return {candidates.front(), candidates.size()};
}

std::optional<GameObjectModelReference> GameObjectCatalog::model(const XmlDefinition& definition, const GameModelContext context, std::vector<std::string>* diagnostics) const
{
    for (const auto field_name : model_fields(context))
    {
        const auto lookup = field(definition, field_name, diagnostics);
        if (lookup.field == nullptr)
        {
            continue;
        }
        const auto value = trim(lookup.field->text);
        if (!value.empty())
        {
            return GameObjectModelReference{std::string(field_name), std::string(value), lookup.declared_by};
        }
    }
    return std::nullopt;
}

std::optional<std::string> resolve_model_path(const VirtualFileSystem& vfs, const std::string_view model_reference)
{
    const auto reference = trim(model_reference);
    if (reference.empty())
    {
        return std::nullopt;
    }
    auto filename = std::filesystem::path(std::string(reference)).filename();
    if (filename.empty())
    {
        return std::nullopt;
    }
    if (!filename.has_extension())
    {
        filename += ".ALO";
    }
    try
    {
        if (const auto resolved = vfs.resolve(normalize_asset_path("Data/Art/Models/" + filename.generic_string())))
        {
            return resolved->virtual_path;
        }
    }
    catch (const std::invalid_argument&)
    {
        // A malformed reference cannot enter the normalized VFS namespace; report it as missing.
    }
    return std::nullopt;
}

std::string_view to_string(const GameModelContext context) noexcept
{
    switch (context)
    {
    case GameModelContext::space:
        return "space";
    case GameModelContext::land:
        return "land";
    case GameModelContext::galactic:
        return "galactic";
    }
    return "space";
}

} // namespace eaw
