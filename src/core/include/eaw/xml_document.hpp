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

    [[nodiscard]] const XmlAttribute* attribute(std::string_view name) const noexcept;
    [[nodiscard]] const XmlNode* child(std::string_view name) const noexcept;
};

// Petroglyph XML files are often fragments containing several top-level object
// elements rather than one standards-compliant document root. The parser keeps
// those roots in source order while still validating every element subtree.
struct XmlDocument
{
    std::vector<XmlNode> roots;

    // Errors name the line of the first malformed construct ("XML line 12: ...").
    [[nodiscard]] static Result<XmlDocument> parse(std::string_view text);
    [[nodiscard]] static Result<XmlDocument> parse(std::span<const std::byte> bytes);
};

} // namespace eaw
