#pragma once

#include "assets/xml_asset_database.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

struct XmlTagSchema
{
    std::string name;
    std::string value_type;
    std::string reference_kind;
    std::string reference_type;
    std::string enum_name;
    std::string semantic_type;
    bool multiple_allowed{};
};

struct XmlTypeSchema
{
    std::string name;
    std::string name_tag;
    std::vector<XmlTagSchema> tags;

    [[nodiscard]] const XmlTagSchema* find_tag(std::string_view name) const noexcept;
};

struct XmlEnumSchema
{
    std::string name;
    std::string kind;
    std::string source_file;
    bool bitfield{};
    std::vector<std::string> values;
};

struct XmlObjectKindSchema
{
    std::string name;
    std::vector<std::string> behaviors;
    std::vector<std::string> flags;
    std::vector<std::string> member_of;
};

enum class XmlMetafileKind
{
    file_registry,
    direct_content,
    singleton,
    special,
    unknown,
};

struct XmlMetafileSchema
{
    std::string path;
    XmlMetafileKind kind{XmlMetafileKind::unknown};
    std::vector<std::string> types;
};

enum class XmlSchemaDiagnosticSeverity
{
    warning,
    error,
};

enum class XmlSchemaDiagnosticKind
{
    unknown_field,
    duplicate_field,
    invalid_value,
    unresolved_reference,
    reference_type_mismatch,
};

struct XmlSchemaDiagnostic
{
    XmlSchemaDiagnosticSeverity severity{XmlSchemaDiagnosticSeverity::warning};
    XmlSchemaDiagnosticKind kind{XmlSchemaDiagnosticKind::unknown_field};
    std::string source_path;
    std::size_t line{1};
    std::string object_type;
    std::string object_name;
    std::string field;
    std::string message;
};

struct XmlSchemaValidationResult
{
    std::size_t definitions_considered{};
    std::size_t definitions_validated{};
    std::size_t canonical_type_definitions{};
    std::size_t metafile_type_definitions{};
    std::size_t structural_type_definitions{};
    std::size_t untyped_definitions{};
    std::size_t fields_validated{};
    std::size_t unknown_fields{};
    std::size_t duplicate_fields{};
    std::size_t invalid_values{};
    std::size_t unresolved_references{};
    std::size_t mismatched_references{};
    std::vector<XmlSchemaDiagnostic> diagnostics;
};

class XmlSchema final
{
public:
    // Accepts either the eaw-schema repository root or one game schema folder.
    // FoC layers its additions over EAW when both directories are available.
    // Errors name the schema file or folder that is missing or malformed.
    [[nodiscard]] static Result<XmlSchema> load(const std::filesystem::path& root, bool include_foc = true);

    [[nodiscard]] std::span<const XmlTypeSchema> types() const noexcept;
    [[nodiscard]] std::span<const XmlEnumSchema> enums() const noexcept;
    [[nodiscard]] std::span<const XmlObjectKindSchema> kinds() const noexcept;
    [[nodiscard]] std::span<const XmlMetafileSchema> metafiles() const noexcept;
    [[nodiscard]] const XmlTypeSchema* find_type(std::string_view name) const noexcept;
    [[nodiscard]] const XmlEnumSchema* find_enum(std::string_view name) const noexcept;
    [[nodiscard]] const XmlObjectKindSchema* find_kind(std::string_view name) const noexcept;
    [[nodiscard]] XmlSchemaValidationResult validate(const XmlAssetDatabase& database) const;

private:
    std::vector<XmlTypeSchema> types_;
    std::vector<XmlEnumSchema> enums_;
    std::vector<XmlObjectKindSchema> kinds_;
    std::vector<XmlMetafileSchema> metafiles_;
};

[[nodiscard]] std::string_view to_string(XmlSchemaDiagnosticSeverity severity) noexcept;
[[nodiscard]] std::string_view to_string(XmlSchemaDiagnosticKind kind) noexcept;

} // namespace eaw
