#include "eaw/game_object_catalog.hpp"

#include "vfs/asset_path.hpp"
#include "vfs/virtual_file_system.hpp"
#include "core/checksum.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cctype>
#include <filesystem>

namespace eaw {
namespace {

// A stock variant chain is a handful of links; this only stops runaway or cyclic mod data.
constexpr std::size_t maximum_variant_depth = 64;

const XmlNode* direct_field(const XmlDefinition& definition, const std::string_view field_name) noexcept
{
    // The last occurrence wins, matching ordinary XML override expectations for repeated tags.
    const XmlNode* found = nullptr;
    for (const auto& field : definition.fields)
    {
        if (ascii_equal_ignoring_case(field.name, field_name))
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
    // CRC-32 of the name in upper case, as the game hashes object type names.
    auto crc = crc32_initial;
    for (const char character : name)
    {
        const auto upper = static_cast<std::byte>(ascii_upper(character));
        crc = crc32_update(crc, std::span(&upper, 1));
    }
    return crc32_finish(crc);
}

GameObjectCatalog GameObjectCatalog::build(const XmlAssetDatabase& database)
{
    GameObjectCatalog catalog;
    for (const auto& definition : database.definitions())
    {
        catalog.definitions_.push_back(&definition);
        catalog.by_name_[ascii_lower(definition.name)].push_back(&definition);
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
    const auto found = by_name_.find(ascii_lower(ascii_trim(name)));
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
        const auto parent_name = variant == nullptr ? std::string_view{} : ascii_trim(variant->text);
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

GameObjectFieldList GameObjectCatalog::fields(const XmlDefinition& definition, const std::string_view field_name, std::vector<std::string>* diagnostics) const
{
    GameObjectFieldList result;
    const auto lookup = field(definition, field_name, diagnostics);
    if (lookup.declared_by == nullptr)
    {
        return result;
    }
    result.declared_by = lookup.declared_by;
    for (const auto& node : lookup.declared_by->fields)
    {
        if (ascii_equal_ignoring_case(node.name, field_name))
        {
            result.fields.push_back(&node);
        }
    }
    return result;
}

std::optional<std::array<float, 4>> GameObjectCatalog::affiliation_color(const XmlDefinition& definition) const
{
    const auto affiliation = field(definition, "Affiliation");
    if (affiliation.field == nullptr)
    {
        return std::nullopt;
    }
    const auto text = std::string_view(affiliation.field->text);
    const auto faction_name = ascii_trim(text.substr(0, text.find(',')));
    if (faction_name.empty())
    {
        return std::nullopt;
    }
    for (const auto* faction : find(faction_name))
    {
        if (faction->type != "Faction")
        {
            continue;
        }
        const auto color = field(*faction, "Color");
        if (color.field == nullptr)
        {
            return std::nullopt;
        }
        // "R, G, B[, A]" in 0..255; separators may be commas and/or spaces.
        std::array<float, 4> result{0.0F, 0.0F, 0.0F, 1.0F};
        std::size_t count = 0;
        const auto value = std::string_view(color.field->text);
        std::size_t position = 0;
        while (position < value.size() && count < result.size())
        {
            while (position < value.size() && (value[position] == ',' || std::isspace(static_cast<unsigned char>(value[position]))))
            {
                ++position;
            }
            if (position >= value.size())
            {
                break;
            }
            float component{};
            const auto [end, error] = std::from_chars(value.data() + position, value.data() + value.size(), component);
            if (error != std::errc{} || !std::isfinite(component))
            {
                return std::nullopt;
            }
            result[count++] = std::clamp(component / 255.0F, 0.0F, 1.0F);
            position = static_cast<std::size_t>(end - value.data());
        }
        return count >= 3 ? std::optional<std::array<float, 4>>{result} : std::nullopt;
    }
    return std::nullopt;
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
        const auto value = ascii_trim(lookup.field->text);
        if (!value.empty())
        {
            return GameObjectModelReference{std::string(field_name), std::string(value), lookup.declared_by};
        }
    }
    return std::nullopt;
}

std::optional<std::string> resolve_model_path(const AssetReader& vfs, const std::string_view model_reference)
{
    const auto reference = ascii_trim(model_reference);
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
    // A malformed reference cannot enter the normalized VFS namespace; report it as missing.
    const auto normalized = normalize_asset_path("Data/Art/Models/" + filename.generic_string());
    if (!normalized)
    {
        return std::nullopt;
    }
    if (const auto resolved = vfs.resolve(*normalized))
    {
        return resolved->virtual_path;
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
