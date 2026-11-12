#pragma once

// Typed reading of a game object type's XML fields, through its variant chain
// (GameObjectCatalog::field), with one parsing rule per kind of value and diagnostics that name
// the type, the field, the value, and the variant that declared it.
//
// Numbers follow the stock data: a value is read like C's atof, its leading number taken and the
// rest ignored; 577 stock values carry text after the number, nearly all the C float suffix
// ("2.0f", Spin_Away_On_Death_Time, Select_Box_Z_Adjust), which is accepted silently. Other
// trailing text is reported. Lists are comma-separated.

#include "assets/game_object_catalog.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

// The leading number of an XML value (whitespace trimmed, an optional sign), or nothing when the
// value does not start with a finite number. `trailing`, when given, receives what follows the
// number other than a float suffix ('f' or 'F'): empty for a clean value.
[[nodiscard]] std::optional<float> parse_xml_number(std::string_view text, std::string_view* trailing = nullptr) noexcept;

class TypeFields final
{
public:
    // Diagnostics go to `warnings` when given; without it bad values are only skipped.
    TypeFields(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::vector<std::string>* warnings = nullptr) noexcept;

    [[nodiscard]] const XmlDefinition& definition() const noexcept
    {
        return definition_;
    }
    [[nodiscard]] const GameObjectCatalog& catalog() const noexcept
    {
        return catalog_;
    }

    // The field's node from the nearest definition in the variant chain that has it, or null.
    [[nodiscard]] const XmlNode* node(std::string_view field) const;
    // Every occurrence of a repeated field, from the nearest definition that declares it.
    [[nodiscard]] std::vector<const XmlNode*> nodes(std::string_view field) const;
    // The field's text, trimmed; empty when absent.
    [[nodiscard]] std::string_view text(std::string_view field) const;

    // The field as a number; nothing when absent or not a number (reported).
    [[nodiscard]] std::optional<float> number(std::string_view field) const;
    // A non-negative number, or `fallback` when the field is absent; a bad or negative value is
    // reported and gives 0.
    [[nodiscard]] float amount(std::string_view field, float fallback = 0.0F) const;
    // A number of either sign, 0 when absent; a bad value is reported and gives 0.
    [[nodiscard]] float signed_value(std::string_view field) const;
    // A yes/no value (parse_xml_bool), `fallback` when absent; a bad value is reported and gives
    // `fallback`.
    [[nodiscard]] bool flag(std::string_view field, bool fallback) const;
    // The field's list items (split_list); empty when absent.
    [[nodiscard]] std::vector<std::string_view> list(std::string_view field) const;

    // Reports `value` of `field` as not being `expected` ("a non-negative number").
    void warn(std::string_view field, std::string_view value, std::string_view expected) const;

private:
    const GameObjectCatalog& catalog_;
    const XmlDefinition& definition_;
    std::vector<std::string>* warnings_;
};

} // namespace eaw
