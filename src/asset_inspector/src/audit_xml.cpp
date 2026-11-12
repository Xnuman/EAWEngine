// `xml-audit`: XML definitions, references, and optional schema validation.

#include "asset_inspector/inspector.hpp"

namespace eaw::asset_inspector {

eaw::Status audit_xml(const eaw::VirtualFileSystem& vfs, const std::string_view prefix, const std::size_t limit, const std::optional<std::filesystem::path>& schema_root, const bool include_foc)
{
    const auto database = eaw::XmlAssetDatabase::build(vfs, prefix);
    std::map<std::string, std::size_t> definitions_by_type;
    for (const auto& definition : database.definitions())
    {
        ++definitions_by_type[definition.type];
    }
    const auto missing_includes = std::ranges::count_if(database.includes(),
                                                        [](const auto& reference)
                                                        {
                                                            return !reference.resolved;
                                                        });

    std::cout << "XML semantic audit\n"
              << "Documents: " << database.document_count() << '\n'
              << "Parsed: " << database.parsed_document_count() << '\n'
              << "Parse failures: " << database.failures().size() << '\n'
              << "Named definitions: " << database.definitions().size() << '\n'
              << "Definition types: " << definitions_by_type.size() << '\n'
              << "Duplicate type/name keys: " << database.duplicates().size() << '\n'
              << "XML file references: " << database.includes().size() << '\n'
              << "Missing XML file references: " << missing_includes << '\n';

    std::cout << "\nMost common definition types:\n";
    std::vector<std::pair<std::string, std::size_t>> sorted_types(definitions_by_type.begin(), definitions_by_type.end());
    std::ranges::sort(sorted_types,
                      [](const auto& left, const auto& right)
                      {
                          if (left.second != right.second)
                          {
                              return left.second > right.second;
                          }
                          return left.first < right.first;
                      });
    for (std::size_t index = 0; index < std::min(limit, sorted_types.size()); ++index)
    {
        std::cout << "  " << std::setw(7) << sorted_types[index].second << "  " << sorted_types[index].first << '\n';
    }

    if (!database.failures().empty())
    {
        std::cout << "\nParse failures:\n";
        for (std::size_t index = 0; index < std::min(limit, database.failures().size()); ++index)
        {
            std::cout << "  " << database.failures()[index].source_path << ": " << database.failures()[index].message << '\n';
        }
    }
    if (!database.duplicates().empty())
    {
        std::cout << "\nDuplicate definitions:\n";
        for (std::size_t index = 0; index < std::min(limit, database.duplicates().size()); ++index)
        {
            const auto& duplicate = database.duplicates()[index];
            std::cout << "  " << duplicate.type << " Name=\"" << duplicate.name << "\"  (" << duplicate.occurrences.size() << " occurrence(s))\n";
            for (const auto& occurrence : duplicate.occurrences)
            {
                std::cout << "    " << occurrence.source_path << ':' << occurrence.line << '\n';
            }
        }
    }
    if (missing_includes != 0)
    {
        std::cout << "\nMissing XML file references:\n";
        std::size_t shown = 0;
        for (const auto& reference : database.includes())
        {
            if (reference.resolved || shown++ >= limit)
            {
                continue;
            }
            std::cout << "  " << reference.resolved_path << "  from " << reference.source_path << ':' << reference.line << '\n';
        }
    }

    if (schema_root)
    {
        EAW_TRY_ASSIGN(const auto schema, eaw::XmlSchema::load(*schema_root, include_foc));
        const auto validation = schema.validate(database);
        const auto errors = std::ranges::count_if(validation.diagnostics,
                                                  [](const auto& diagnostic)
                                                  {
                                                      return diagnostic.severity == eaw::XmlSchemaDiagnosticSeverity::error;
                                                  });
        std::cout << "\nExternal schema validation\n"
                  << "Schema types: " << schema.types().size() << '\n'
                  << "Schema enums/token sets: " << schema.enums().size() << '\n'
                  << "Schema object kinds: " << schema.kinds().size() << '\n'
                  << "Metafile rules: " << schema.metafiles().size() << '\n'
                  << "Definitions validated: " << validation.definitions_validated << '/' << validation.definitions_considered << '\n'
                  << "  Canonical element types: " << validation.canonical_type_definitions << '\n'
                  << "  Metafile-inferred types: " << validation.metafile_type_definitions << '\n'
                  << "  Structurally inferred types: " << validation.structural_type_definitions << '\n'
                  << "Untyped definitions: " << validation.untyped_definitions << '\n'
                  << "Fields validated: " << validation.fields_validated << '\n'
                  << "Unknown fields: " << validation.unknown_fields << '\n'
                  << "Duplicate non-repeatable fields: " << validation.duplicate_fields << '\n'
                  << "Invalid scalar/enum values: " << validation.invalid_values << '\n'
                  << "Unresolved XML references: " << validation.unresolved_references << '\n'
                  << "Wrong-type XML references: " << validation.mismatched_references << '\n'
                  << "Diagnostics: " << validation.diagnostics.size() << " (" << errors << " error(s))\n";
        for (std::size_t index = 0; index < std::min(limit, validation.diagnostics.size()); ++index)
        {
            const auto& diagnostic = validation.diagnostics[index];
            std::cout << "  " << eaw::to_string(diagnostic.severity) << '[' << eaw::to_string(diagnostic.kind) << "] " << diagnostic.source_path << ':' << diagnostic.line << "  "
                      << diagnostic.object_type << " Name=\"" << diagnostic.object_name << "\" / " << diagnostic.field << ": " << diagnostic.message << '\n';
        }
    }
    return eaw::Status::success();
}

} // namespace eaw::asset_inspector
