#pragma once

#include "assets/xml_asset_database.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eaw {

class AssetReader;

// Hash used to reference object types by name from binary data such as TED placed objects:
// CRC-32 (IEEE, reflected) of the ASCII-uppercased type name. Established by the stock corpus
// audit (`EAWAssetInspector map-objects`); see docs/compatibility-research.md.
[[nodiscard]] std::uint32_t game_object_name_crc(std::string_view name) noexcept;

enum class GameModelContext
{
    space,
    land,
    galactic,
};

// Which XML field supplied a model reference and where it was found in the variant chain.
struct GameObjectModelReference
{
    std::string field;
    std::string value;
    const XmlDefinition* declared_by{};
};

struct GameObjectTypeMatch
{
    const XmlDefinition* definition{};
    std::size_t candidates{};
};

struct GameObjectFieldLookup
{
    const XmlNode* field{};
    const XmlDefinition* declared_by{};
};

// Every occurrence of a repeatable field (for example Squadron_Offsets), in source order, taken
// from the first definition in the variant chain that declares the field at all.
struct GameObjectFieldList
{
    std::vector<const XmlNode*> fields;
    const XmlDefinition* declared_by{};
};

// Read-only index of every named XML definition, keyed by name and by name CRC.
// The catalog borrows the database: the XmlAssetDatabase must outlive it.
class GameObjectCatalog final
{
public:
    [[nodiscard]] static GameObjectCatalog build(const XmlAssetDatabase& database);

    [[nodiscard]] std::size_t size() const noexcept;

    // All definitions whose name matches case-insensitively (several element types
    // may legitimately share a name).
    [[nodiscard]] std::span<const XmlDefinition* const> find(std::string_view name) const;
    [[nodiscard]] std::span<const XmlDefinition* const> find_crc(std::uint32_t crc) const;

    // Field lookup that follows Variant_Of_Existing_Type to the parent definition, preferring a
    // parent of the same element type. Cycles and chains deeper than a fixed budget stop the
    // walk; `diagnostics` (when supplied) receives a contextual message for them.
    [[nodiscard]] GameObjectFieldLookup field(const XmlDefinition& definition, std::string_view field_name, std::vector<std::string>* diagnostics = nullptr) const;

    [[nodiscard]] GameObjectFieldList fields(const XmlDefinition& definition, std::string_view field_name, std::vector<std::string>* diagnostics = nullptr) const;

    // Chooses the definition a CRC reference most plausibly names. Several element types can
    // share a name; a candidate with a model for the requested context is preferred, otherwise
    // the first in database order. This tie-break is corpus inference, not documented behavior.
    [[nodiscard]] GameObjectTypeMatch resolve(std::uint32_t crc, GameModelContext context) const;

    // Display color (0..1 RGBA) of the first faction named in the type's Affiliation field, read
    // from that Faction definition's Color (0..255 components). Placed objects are owned by a
    // player slot at runtime; using the declared affiliation is an approximation for previews.
    [[nodiscard]] std::optional<std::array<float, 4>> affiliation_color(const XmlDefinition& definition) const;

    [[nodiscard]] std::optional<GameObjectModelReference> model(const XmlDefinition& definition, GameModelContext context, std::vector<std::string>* diagnostics = nullptr) const;

private:
    std::vector<const XmlDefinition*> definitions_;
    std::unordered_map<std::string, std::vector<const XmlDefinition*>> by_name_;
    std::unordered_map<std::uint32_t, std::vector<const XmlDefinition*>> by_crc_;
};

// Resolves an XML model reference (for example "EV_Acclamator.alo" or "EV_Acclamator") to the
// winning virtual path under Data/Art/Models, or nullopt when no layer provides it.
[[nodiscard]] std::optional<std::string> resolve_model_path(const AssetReader& vfs, std::string_view model_reference);

[[nodiscard]] std::string_view to_string(GameModelContext context) noexcept;

} // namespace eaw
