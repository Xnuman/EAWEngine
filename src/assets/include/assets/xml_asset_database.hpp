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
    // Reads every XML file under `prefix` the way the game loads them (when the prefix holds the
    // registries): the files GameObjectFiles.xml and the other registries list, in list order, then
    // the rest. A name two definitions share resolves to the first in definitions(), which is the
    // first registered, or for a last-wins registry (hardpoints, campaigns, trade routes, command bar
    // components, mouse pointers) the last. Files nothing reaches keep only definitions of types the
    // game never loads through a list. Without registries, every file loads in path order.
    [[nodiscard]] static XmlAssetDatabase build(const AssetReader& vfs, std::string_view prefix = "Data/XML");

    [[nodiscard]] std::size_t document_count() const noexcept;
    [[nodiscard]] std::size_t parsed_document_count() const noexcept;
    [[nodiscard]] std::span<const XmlDefinition> definitions() const noexcept;
    [[nodiscard]] std::span<const XmlParseFailure> failures() const noexcept;
    [[nodiscard]] std::span<const XmlIncludeReference> includes() const noexcept;
    [[nodiscard]] std::span<const XmlDuplicateDefinition> duplicates() const noexcept;
    // Registry entries (GameObjectFiles.xml and the others) naming a file that is missing or did not
    // parse.
    [[nodiscard]] std::span<const XmlIncludeReference> missing_registry_entries() const noexcept;
    // Files no registry, name the game opens, walked folder, or reference reaches (empty when the
    // prefix holds no registry), and how many of their definitions were left out: those of element
    // types the game loads only through lists.
    [[nodiscard]] std::span<const std::string> unreached_files() const noexcept;
    [[nodiscard]] std::size_t unreached_definition_count() const noexcept;
    [[nodiscard]] std::vector<const XmlDefinition*> find(std::string_view type, std::string_view name) const;

private:
    std::size_t document_count_{};
    std::size_t parsed_document_count_{};
    std::vector<XmlDefinition> definitions_;
    std::vector<XmlParseFailure> failures_;
    std::vector<XmlIncludeReference> includes_;
    std::vector<XmlDuplicateDefinition> duplicates_;
    std::vector<XmlIncludeReference> missing_registry_entries_;
    std::vector<std::string> unreached_files_;
    std::size_t unreached_definition_count_{};
};

} // namespace eaw
