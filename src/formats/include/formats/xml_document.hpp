#pragma once

#include "core/status.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

struct XmlAttribute
{
    std::string name;
    std::string value;
    std::size_t line{1};
};

struct XmlNode
{
    std::string name;
    std::vector<XmlAttribute> attributes;
    std::string text;
    std::vector<XmlNode> children;
    std::size_t line{1};
};

// Petroglyph XML files are often fragments containing several top-level object
// elements rather than one standards-compliant document root. The parser keeps
// those roots in source order while still validating every element subtree.
struct XmlData
{
    std::vector<XmlNode> roots;
};

// Errors name the line of the first malformed construct ("XML line 12: ..."). The byte form
// detects a UTF-8 or UTF-16 byte-order mark.
[[nodiscard]] Result<XmlData> read_xml(std::string_view text);
[[nodiscard]] Result<XmlData> read_xml(std::span<const std::byte> bytes);

} // namespace eaw
