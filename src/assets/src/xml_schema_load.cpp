// The optional public eaw-schema data: a small reader for its YAML subset, the base and FoC layers
// merged by name, and the schema's accessors.

#include "assets/xml_schema.hpp"

#include "xml_schema_internal.hpp"

#include "vfs/asset_path.hpp"
#include "core/string_utils.hpp"
#include "core/file_io.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <iterator>
#include <system_error>

namespace eaw {
namespace {

struct YamlLine
{
    std::size_t indentation{};
    std::string text;
};

struct SchemaLayer
{
    std::vector<XmlTypeSchema> types;
    std::vector<XmlEnumSchema> enums;
    std::vector<XmlObjectKindSchema> kinds;
    std::vector<XmlMetafileSchema> metafiles;
};

std::string strip_yaml_comment(const std::string_view value)
{
    char quote = '\0';
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        const auto character = value[index];
        if (quote != '\0')
        {
            if (character == quote && (index == 0 || value[index - 1] != '\\'))
            {
                quote = '\0';
            }
        }
        else if (character == '\'' || character == '"')
        {
            quote = character;
        }
        else if (character == '#')
        {
            return std::string(value.substr(0, index));
        }
    }
    return std::string(value);
}

std::string yaml_scalar(const std::string_view value)
{
    auto result = std::string(ascii_trim(value));
    if (result.size() >= 2 && ((result.front() == '"' && result.back() == '"') || (result.front() == '\'' && result.back() == '\'')))
    {
        result = result.substr(1, result.size() - 2);
    }
    return result;
}

bool starts_key(const std::string_view text, const std::string_view key)
{
    return text.starts_with(key) && text.size() > key.size() && text[key.size()] == ':';
}

std::string key_value(const std::string_view text, const std::string_view key)
{
    if (!starts_key(text, key))
    {
        return {};
    }
    return yaml_scalar(text.substr(key.size() + 1));
}

Result<std::vector<YamlLine>> read_yaml(const std::filesystem::path& path)
{
    auto file = read_text_file(path);
    if (!file)
    {
        return with_context("XML schema file", std::move(file).error());
    }
    std::vector<YamlLine> lines;
    bool first_line = true;
    std::string_view rest = *file;
    while (!rest.empty())
    {
        const auto line_end = rest.find('\n');
        std::string raw(rest.substr(0, line_end));
        rest = line_end == std::string_view::npos ? std::string_view{} : rest.substr(line_end + 1);
        if (!raw.empty() && raw.back() == '\r')
        {
            raw.pop_back();
        }
        if (first_line && raw.starts_with("\xef\xbb\xbf"))
        {
            raw.erase(0, 3);
        }
        first_line = false;
        std::size_t indentation = 0;
        while (indentation < raw.size() && raw[indentation] == ' ')
        {
            ++indentation;
        }
        auto content = std::string(ascii_trim(strip_yaml_comment(std::string_view(raw).substr(indentation))));
        if (!content.empty())
        {
            lines.push_back({indentation, std::move(content)});
        }
    }
    return lines;
}

Result<std::vector<XmlTypeSchema>> read_types(const std::filesystem::path& path)
{
    std::vector<XmlTypeSchema> types;
    EAW_TRY_ASSIGN(const auto lines, read_yaml(path));
    for (const auto& line : lines)
    {
        if (line.indentation == 2 && line.text.starts_with("- typeName:"))
        {
            types.push_back({yaml_scalar(line.text.substr(11)), {}, {}});
        }
        else if (!types.empty() && line.indentation == 4 && starts_key(line.text, "nameTag"))
        {
            types.back().name_tag = key_value(line.text, "nameTag");
        }
    }
    return types;
}

Result<std::vector<XmlTagSchema>> read_tags(const std::filesystem::path& path)
{
    std::vector<XmlTagSchema> tags;
    EAW_TRY_ASSIGN(const auto lines, read_yaml(path));
    for (const auto& line : lines)
    {
        if (line.indentation == 2 && line.text.starts_with("- tag:"))
        {
            tags.push_back({yaml_scalar(line.text.substr(6)), {}, {}, {}, {}, {}, false});
            continue;
        }
        if (tags.empty() || line.indentation != 4)
        {
            continue;
        }
        auto& tag = tags.back();
        if (starts_key(line.text, "type"))
        {
            tag.value_type = key_value(line.text, "type");
        }
        else if (starts_key(line.text, "referenceKind"))
        {
            tag.reference_kind = key_value(line.text, "referenceKind");
        }
        else if (starts_key(line.text, "referenceType"))
        {
            tag.reference_type = key_value(line.text, "referenceType");
        }
        else if (starts_key(line.text, "enumName"))
        {
            tag.enum_name = key_value(line.text, "enumName");
        }
        else if (starts_key(line.text, "semanticType"))
        {
            tag.semantic_type = key_value(line.text, "semanticType");
        }
        else if (starts_key(line.text, "multipleAllowed"))
        {
            tag.multiple_allowed = ascii_lower(key_value(line.text, "multipleAllowed")) == "true";
        }
    }
    return tags;
}

Result<XmlEnumSchema> read_enum(const std::filesystem::path& path)
{
    XmlEnumSchema result;
    bool reading_values = false;
    EAW_TRY_ASSIGN(const auto lines, read_yaml(path));
    for (const auto& line : lines)
    {
        if (line.indentation == 0)
        {
            reading_values = line.text == "values:";
            if (starts_key(line.text, "name"))
            {
                result.name = key_value(line.text, "name");
            }
            else if (starts_key(line.text, "kind"))
            {
                result.kind = key_value(line.text, "kind");
            }
            else if (starts_key(line.text, "sourceFile"))
            {
                result.source_file = key_value(line.text, "sourceFile");
            }
            else if (starts_key(line.text, "isBitfield"))
            {
                result.bitfield = ascii_lower(key_value(line.text, "isBitfield")) == "true";
            }
        }
        else if (reading_values && line.indentation == 2 && line.text.starts_with("- name:"))
        {
            result.values.push_back(yaml_scalar(line.text.substr(7)));
        }
    }
    return result;
}

std::vector<std::string> yaml_inline_list(const std::string_view value)
{
    auto text = std::string(ascii_trim(value));
    if (text.size() < 2 || text.front() != '[' || text.back() != ']')
    {
        return {};
    }
    text = text.substr(1, text.size() - 2);
    std::vector<std::string> result;
    std::size_t begin = 0;
    char quote = '\0';
    for (std::size_t index = 0; index <= text.size(); ++index)
    {
        const auto character = index < text.size() ? text[index] : ',';
        if (quote != '\0')
        {
            if (character == quote && (index == 0 || text[index - 1] != '\\'))
            {
                quote = '\0';
            }
            continue;
        }
        if (character == '\'' || character == '"')
        {
            quote = character;
        }
        else if (character == ',')
        {
            auto item = yaml_scalar(std::string_view(text).substr(begin, index - begin));
            if (!item.empty())
            {
                result.push_back(std::move(item));
            }
            begin = index + 1;
        }
    }
    return result;
}

Result<std::vector<XmlObjectKindSchema>> read_kinds(const std::filesystem::path& path)
{
    std::vector<XmlObjectKindSchema> kinds;
    EAW_TRY_ASSIGN(const auto lines, read_yaml(path));
    for (const auto& line : lines)
    {
        if (line.indentation == 2 && line.text.starts_with("- kind:"))
        {
            kinds.push_back({yaml_scalar(line.text.substr(7)), {}, {}, {}});
            continue;
        }
        if (kinds.empty() || line.indentation != 4)
        {
            continue;
        }
        if (starts_key(line.text, "behaviors"))
        {
            kinds.back().behaviors = yaml_inline_list(key_value(line.text, "behaviors"));
        }
        else if (starts_key(line.text, "flags"))
        {
            kinds.back().flags = yaml_inline_list(key_value(line.text, "flags"));
        }
        else if (starts_key(line.text, "memberOf"))
        {
            kinds.back().member_of = yaml_inline_list(key_value(line.text, "memberOf"));
        }
    }
    return kinds;
}

XmlMetafileKind parse_metafile_kind(const std::string_view value)
{
    const auto folded = ascii_lower(value);
    if (folded == "fileregistry")
    {
        return XmlMetafileKind::file_registry;
    }
    if (folded == "directcontent")
    {
        return XmlMetafileKind::direct_content;
    }
    if (folded == "singleton")
    {
        return XmlMetafileKind::singleton;
    }
    if (folded == "special")
    {
        return XmlMetafileKind::special;
    }
    return XmlMetafileKind::unknown;
}

Result<std::vector<XmlMetafileSchema>> read_metafiles(const std::filesystem::path& path)
{
    std::vector<XmlMetafileSchema> metafiles;
    bool reading_metafiles = false;
    bool reading_types = false;
    EAW_TRY_ASSIGN(const auto lines, read_yaml(path));
    for (const auto& line : lines)
    {
        if (line.indentation == 0)
        {
            reading_metafiles = line.text == "metafiles:";
            reading_types = false;
            continue;
        }
        if (!reading_metafiles)
        {
            continue;
        }
        if (line.indentation == 2 && line.text.starts_with("- path:"))
        {
            const auto path_value = yaml_scalar(line.text.substr(7));
            auto normalized = normalize_asset_path(path_value);
            if (!normalized)
            {
                return make_error("invalid metafile path in XML schema: " + path_value);
            }
            metafiles.push_back({std::move(normalized).value(), XmlMetafileKind::unknown, {}});
            reading_types = false;
        }
        else if (!metafiles.empty() && line.indentation == 4 && starts_key(line.text, "metaFileType"))
        {
            metafiles.back().kind = parse_metafile_kind(key_value(line.text, "metaFileType"));
            reading_types = false;
        }
        else if (!metafiles.empty() && line.indentation == 4 && line.text == "types:")
        {
            reading_types = true;
        }
        else if (!metafiles.empty() && reading_types && line.indentation == 6 && line.text.starts_with("- "))
        {
            metafiles.back().types.push_back(yaml_scalar(line.text.substr(2)));
        }
        else if (line.indentation <= 4)
        {
            reading_types = false;
        }
    }
    return metafiles;
}

Result<std::vector<std::filesystem::path>> yaml_files(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> files;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
    {
        return files;
    }
    for (std::filesystem::directory_iterator item(directory, error), end; item != end; item.increment(error))
    {
        if (error)
        {
            return make_error("could not enumerate XML schema directory: " + directory.string());
        }
        if (item->is_regular_file(error) && !error && ascii_lower(item->path().extension().string()) == ".yaml")
        {
            files.push_back(item->path());
        }
        error.clear();
    }
    std::ranges::sort(files);
    return files;
}

Result<SchemaLayer> load_layer(const std::filesystem::path& root)
{
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(root / "types.yaml", filesystem_error))
    {
        return make_error("XML schema folder has no types.yaml: " + root.string());
    }
    SchemaLayer layer;
    EAW_TRY_ASSIGN(layer.types, read_types(root / "types.yaml"));
    EAW_TRY_ASSIGN(const auto tag_files, yaml_files(root / "tags"));
    for (const auto& path : tag_files)
    {
        const auto type_name = path.stem().string();
        auto type = std::ranges::find_if(layer.types,
                                         [&](const auto& candidate)
                                         {
                                             return ascii_lower(candidate.name) == ascii_lower(type_name);
                                         });
        if (type == layer.types.end())
        {
            layer.types.push_back({type_name, {}, {}});
            type = std::prev(layer.types.end());
        }
        EAW_TRY_ASSIGN(type->tags, read_tags(path));
    }
    EAW_TRY_ASSIGN(const auto enum_files, yaml_files(root / "enums"));
    for (const auto& path : enum_files)
    {
        EAW_TRY_ASSIGN(auto item, read_enum(path));
        if (!item.name.empty())
        {
            layer.enums.push_back(std::move(item));
        }
    }
    EAW_TRY_ASSIGN(const auto hardcoded_files, yaml_files(root / "hardcoded"));
    for (const auto& path : hardcoded_files)
    {
        EAW_TRY_ASSIGN(auto item, read_enum(path));
        if (!item.name.empty())
        {
            layer.enums.push_back(std::move(item));
        }
    }
    if (std::filesystem::is_regular_file(root / "kinds.yaml", filesystem_error))
    {
        EAW_TRY_ASSIGN(layer.kinds, read_kinds(root / "kinds.yaml"));
    }
    if (std::filesystem::is_regular_file(root / "meta" / "metafiles.yaml", filesystem_error))
    {
        EAW_TRY_ASSIGN(layer.metafiles, read_metafiles(root / "meta" / "metafiles.yaml"));
    }
    return layer;
}

template <typename Item> void merge_named(std::vector<Item>& target, std::vector<Item> source)
{
    for (auto& item : source)
    {
        const auto found = std::ranges::find_if(target,
                                                [&](const auto& candidate)
                                                {
                                                    return ascii_lower(candidate.name) == ascii_lower(item.name);
                                                });
        if (found == target.end())
        {
            target.push_back(std::move(item));
        }
        else
        {
            *found = std::move(item);
        }
    }
}

} // namespace

const XmlTagSchema* XmlTypeSchema::find_tag(const std::string_view requested) const noexcept
{
    const auto folded = ascii_lower(requested);
    const auto found = std::ranges::find_if(tags,
                                            [&](const auto& tag)
                                            {
                                                return ascii_lower(tag.name) == folded;
                                            });
    return found == tags.end() ? nullptr : &*found;
}

Result<XmlSchema> XmlSchema::load(const std::filesystem::path& root, const bool include_foc)
{
    std::vector<std::filesystem::path> roots;
    std::error_code filesystem_error;
    if (std::filesystem::is_directory(root / "eaw", filesystem_error))
    {
        roots.push_back(root / "eaw");
        if (include_foc && std::filesystem::is_directory(root / "foc", filesystem_error))
        {
            roots.push_back(root / "foc");
        }
    }
    else
    {
        roots.push_back(root);
    }

    XmlSchema schema;
    for (const auto& layer_root : roots)
    {
        EAW_TRY_ASSIGN(auto layer, load_layer(layer_root));
        merge_named(schema.types_, std::move(layer.types));
        merge_named(schema.enums_, std::move(layer.enums));
        merge_named(schema.kinds_, std::move(layer.kinds));
        schema.metafiles_.insert(schema.metafiles_.end(), std::make_move_iterator(layer.metafiles.begin()), std::make_move_iterator(layer.metafiles.end()));
    }
    if (schema.types_.empty())
    {
        return make_error("XML schema contains no object types: " + root.string());
    }
    return schema;
}

std::span<const XmlTypeSchema> XmlSchema::types() const noexcept
{
    return types_;
}

std::span<const XmlEnumSchema> XmlSchema::enums() const noexcept
{
    return enums_;
}

std::span<const XmlObjectKindSchema> XmlSchema::kinds() const noexcept
{
    return kinds_;
}

std::span<const XmlMetafileSchema> XmlSchema::metafiles() const noexcept
{
    return metafiles_;
}

const XmlTypeSchema* XmlSchema::find_type(const std::string_view name) const noexcept
{
    const auto folded = ascii_lower(name);
    const auto found = std::ranges::find_if(types_,
                                            [&](const auto& type)
                                            {
                                                return ascii_lower(type.name) == folded;
                                            });
    return found == types_.end() ? nullptr : &*found;
}

const XmlEnumSchema* XmlSchema::find_enum(const std::string_view name) const noexcept
{
    const auto folded = ascii_lower(name);
    const auto found = std::ranges::find_if(enums_,
                                            [&](const auto& enumeration)
                                            {
                                                return ascii_lower(enumeration.name) == folded;
                                            });
    return found == enums_.end() ? nullptr : &*found;
}

const XmlObjectKindSchema* XmlSchema::find_kind(const std::string_view name) const noexcept
{
    const auto folded = ascii_lower(name);
    const auto found = std::ranges::find_if(kinds_,
                                            [&](const auto& kind)
                                            {
                                                return ascii_lower(kind.name) == folded;
                                            });
    return found == kinds_.end() ? nullptr : &*found;
}

} // namespace eaw
