#pragma once

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

    [[nodiscard]] static XmlDocument parse(std::string_view text);
    [[nodiscard]] static XmlDocument parse(std::span<const std::byte> bytes);
};

} // namespace eaw
