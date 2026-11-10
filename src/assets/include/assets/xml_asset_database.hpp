#pragma once

#include "vfs/virtual_file_system.hpp"
#include "formats/xml_document.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

struct XmlDefinition
{
    std::string type;
    std::string name;
    std::string source_path;
    std::size_t line{1};
    std::size_t depth{};
    std::vector<XmlAttribute> attributes;
    std::vector<XmlNode> fields;
};

struct XmlParseFailure
{
    std::string source_path;
    std::string message;
};

struct XmlIncludeReference
{
    std::string source_path;
    std::size_t line{1};
    std::string value;
    std::string resolved_path;
    bool resolved{};
};

struct XmlDefinitionLocation
{
    std::string source_path;
    std::size_t line{1};
};

struct XmlDuplicateDefinition
{
    std::string type;
    std::string name;
    std::vector<XmlDefinitionLocation> occurrences;
};

class XmlAssetDatabase final
{
public:
    [[nodiscard]] static XmlAssetDatabase build(const AssetReader& vfs, std::string_view prefix = "Data/XML");

    [[nodiscard]] std::size_t document_count() const noexcept;
    [[nodiscard]] std::size_t parsed_document_count() const noexcept;
    [[nodiscard]] std::span<const XmlDefinition> definitions() const noexcept;
    [[nodiscard]] std::span<const XmlParseFailure> failures() const noexcept;
    [[nodiscard]] std::span<const XmlIncludeReference> includes() const noexcept;
    [[nodiscard]] std::span<const XmlDuplicateDefinition> duplicates() const noexcept;
    [[nodiscard]] std::vector<const XmlDefinition*> find(std::string_view type, std::string_view name) const;

private:
    std::size_t document_count_{};
    std::size_t parsed_document_count_{};
    std::vector<XmlDefinition> definitions_;
    std::vector<XmlParseFailure> failures_;
    std::vector<XmlIncludeReference> includes_;
    std::vector<XmlDuplicateDefinition> duplicates_;
};

} // namespace eaw
