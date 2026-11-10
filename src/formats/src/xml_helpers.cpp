#include "formats/xml_helpers.hpp"

#include "core/string_utils.hpp"

#include <algorithm>

namespace eaw {

const XmlAttribute* xml_attribute(const XmlNode& node, const std::string_view requested) noexcept
{
    const auto found = std::ranges::find_if(node.attributes,
                                            [&](const auto& candidate)
                                            {
                                                return ascii_equal_ignoring_case(candidate.name, requested);
                                            });
    return found == node.attributes.end() ? nullptr : &*found;
}

const XmlNode* xml_child(const XmlNode& node, const std::string_view requested) noexcept
{
    const auto found = std::ranges::find_if(node.children,
                                            [&](const auto& candidate)
                                            {
                                                return ascii_equal_ignoring_case(candidate.name, requested);
                                            });
    return found == node.children.end() ? nullptr : &*found;
}

} // namespace eaw
