#pragma once

// Navigation of parsed XML: attribute and child lookup by name (ASCII case-insensitive, as the game
// matches tags and attributes).

#include "formats/xml_document.hpp"

#include <string_view>

namespace eaw {

// The first attribute or child element of that name, or null.
[[nodiscard]] const XmlAttribute* xml_attribute(const XmlNode& node, std::string_view name) noexcept;
[[nodiscard]] const XmlNode* xml_child(const XmlNode& node, std::string_view name) noexcept;

} // namespace eaw
