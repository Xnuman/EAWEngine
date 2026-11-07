#include "eaw/xml_schema.hpp"

#include "eaw/asset_path.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
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

std::string fold_ascii(const std::string_view value)
{
    std::string result(value);
    std::ranges::transform(result, result.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return result;
}

std::string canonical_name(const std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const auto character : value)
    {
        if (std::isalnum(static_cast<unsigned char>(character)))
        {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
    }
    return result;
}

std::string trim(const std::string_view value)
{
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
    {
        ++first;
    }
    auto last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
    {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

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
    auto result = trim(value);
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

std::vector<YamlLine> read_yaml(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        throw std::runtime_error("could not open XML schema file: " + path.string());
    }
    std::vector<YamlLine> lines;
    std::string raw;
    bool first_line = true;
    while (std::getline(stream, raw))
    {
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
        auto content = trim(strip_yaml_comment(std::string_view(raw).substr(indentation)));
        if (!content.empty())
        {
            lines.push_back({indentation, std::move(content)});
        }
    }
    return lines;
}

std::vector<XmlTypeSchema> read_types(const std::filesystem::path& path)
{
    std::vector<XmlTypeSchema> types;
    for (const auto& line : read_yaml(path))
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

std::vector<XmlTagSchema> read_tags(const std::filesystem::path& path)
{
    std::vector<XmlTagSchema> tags;
    for (const auto& line : read_yaml(path))
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
            tag.multiple_allowed = fold_ascii(key_value(line.text, "multipleAllowed")) == "true";
        }
    }
    return tags;
}

XmlEnumSchema read_enum(const std::filesystem::path& path)
{
    XmlEnumSchema result;
    bool reading_values = false;
    for (const auto& line : read_yaml(path))
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
                result.bitfield = fold_ascii(key_value(line.text, "isBitfield")) == "true";
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
    auto text = trim(value);
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

std::vector<XmlObjectKindSchema> read_kinds(const std::filesystem::path& path)
{
    std::vector<XmlObjectKindSchema> kinds;
    for (const auto& line : read_yaml(path))
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
    const auto folded = fold_ascii(value);
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

std::vector<XmlMetafileSchema> read_metafiles(const std::filesystem::path& path)
{
    std::vector<XmlMetafileSchema> metafiles;
    bool reading_metafiles = false;
    bool reading_types = false;
    for (const auto& line : read_yaml(path))
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
            try
            {
                metafiles.push_back({normalize_asset_path(path_value), XmlMetafileKind::unknown, {}});
            }
            catch (const std::invalid_argument&)
            {
                throw std::runtime_error("invalid metafile path in XML schema: " + path_value);
            }
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

std::vector<std::filesystem::path> yaml_files(const std::filesystem::path& directory)
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
            throw std::runtime_error("could not enumerate XML schema directory: " + directory.string());
        }
        if (item->is_regular_file(error) && !error && fold_ascii(item->path().extension().string()) == ".yaml")
        {
            files.push_back(item->path());
        }
        error.clear();
    }
    std::ranges::sort(files);
    return files;
}

SchemaLayer load_layer(const std::filesystem::path& root)
{
    if (!std::filesystem::is_regular_file(root / "types.yaml"))
    {
        throw std::runtime_error("XML schema folder has no types.yaml: " + root.string());
    }
    SchemaLayer layer;
    layer.types = read_types(root / "types.yaml");
    for (const auto& path : yaml_files(root / "tags"))
    {
        const auto type_name = path.stem().string();
        auto type = std::ranges::find_if(layer.types,
                                         [&](const auto& candidate)
                                         {
                                             return fold_ascii(candidate.name) == fold_ascii(type_name);
                                         });
        if (type == layer.types.end())
        {
            layer.types.push_back({type_name, {}, {}});
            type = std::prev(layer.types.end());
        }
        type->tags = read_tags(path);
    }
    for (const auto& path : yaml_files(root / "enums"))
    {
        auto item = read_enum(path);
        if (!item.name.empty())
        {
            layer.enums.push_back(std::move(item));
        }
    }
    for (const auto& path : yaml_files(root / "hardcoded"))
    {
        auto item = read_enum(path);
        if (!item.name.empty())
        {
            layer.enums.push_back(std::move(item));
        }
    }
    if (std::filesystem::is_regular_file(root / "kinds.yaml"))
    {
        layer.kinds = read_kinds(root / "kinds.yaml");
    }
    if (std::filesystem::is_regular_file(root / "meta" / "metafiles.yaml"))
    {
        layer.metafiles = read_metafiles(root / "meta" / "metafiles.yaml");
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
                                                    return fold_ascii(candidate.name) == fold_ascii(item.name);
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

bool parse_integer(std::string_view value, const bool unsigned_value)
{
    auto text = trim(value);
    if (text.empty())
    {
        return false;
    }
    if (text.front() == '+')
    {
        text.erase(text.begin());
    }
    if (unsigned_value)
    {
        if (text.starts_with('-'))
        {
            return false;
        }
        unsigned long long parsed = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 10);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }
    long long parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 10);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse_hex_integer(std::string_view value)
{
    auto text = trim(value);
    if (text.starts_with("0x") || text.starts_with("0X"))
    {
        text.erase(0, 2);
    }
    if (text.empty())
    {
        return false;
    }
    unsigned long long parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 16);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse_float(std::string_view value, float* output = nullptr)
{
    auto text = trim(value);
    if (!text.empty() && (text.back() == 'f' || text.back() == 'F'))
    {
        text.pop_back();
    }
    if (text.empty())
    {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const auto parsed = std::strtof(text.c_str(), &end);
    if (errno == ERANGE || end != text.c_str() + text.size())
    {
        return false;
    }
    if (output != nullptr)
    {
        *output = parsed;
    }
    return true;
}

std::vector<std::string> split_values(const std::string_view value, const bool pipe_delimiter = false)
{
    std::vector<std::string> result;
    std::size_t begin = 0;
    for (std::size_t index = 0; index <= value.size(); ++index)
    {
        const bool delimiter = index == value.size() || std::isspace(static_cast<unsigned char>(value[index])) || value[index] == ',' || (pipe_delimiter && value[index] == '|');
        if (!delimiter)
        {
            continue;
        }
        if (index > begin)
        {
            auto token = trim(value.substr(begin, index - begin));
            if (!token.empty())
            {
                result.push_back(std::move(token));
            }
        }
        begin = index + 1;
    }
    return result;
}

bool validate_float_components(const std::string_view value, const std::size_t required)
{
    const auto components = split_values(value);
    return components.size() == required && std::ranges::all_of(components,
                                                                [](const auto& component)
                                                                {
                                                                    return parse_float(component);
                                                                });
}

bool contains_case_insensitive(const std::span<const std::string> values, const std::string_view value)
{
    const auto folded = fold_ascii(value);
    return std::ranges::any_of(values,
                               [&](const auto& candidate)
                               {
                                   return fold_ascii(candidate) == folded;
                               });
}

std::optional<std::string> validate_value(const XmlTagSchema& tag, const XmlNode& field, const XmlSchema& schema)
{
    const auto value = trim(field.text);
    if (value.empty() || !field.children.empty())
    {
        return std::nullopt;
    }
    const auto type = fold_ascii(tag.value_type);
    if (type == "boolean")
    {
        const auto folded = fold_ascii(value);
        if (folded != "yes" && folded != "no" && folded != "true" && folded != "false" && folded != "1" && folded != "0")
        {
            return "expected a boolean (Yes/No)";
        }
    }
    else if (type == "int" || type == "audioparamint" || type == "sfxcount")
    {
        if (!parse_integer(value, false))
        {
            return "expected a signed integer";
        }
    }
    else if (type == "uint" || type == "hardwareuint")
    {
        if (!parse_integer(value, true))
        {
            return "expected an unsigned integer";
        }
    }
    else if (type == "shaderversionhex" || type == "vendoridhex")
    {
        if (!parse_hex_integer(value))
        {
            return "expected a hexadecimal integer";
        }
    }
    else if (type == "float" || type == "sfxpercentage")
    {
        if (!parse_float(value))
        {
            return "expected a floating-point number";
        }
    }
    else if (type == "normalizedfloat")
    {
        float parsed = 0.0F;
        if (!parse_float(value, &parsed) || parsed < 0.0F || parsed > 1.0F)
        {
            return "expected a floating-point number in [0, 1]";
        }
    }
    else if (type == "floatvector2")
    {
        if (!validate_float_components(value, 2))
        {
            return "expected two floating-point components";
        }
    }
    else if (type == "floatvector3")
    {
        if (!validate_float_components(value, 3))
        {
            return "expected three floating-point components";
        }
    }
    else if (type == "floatvector4")
    {
        if (!validate_float_components(value, 4))
        {
            return "expected four floating-point components";
        }
    }
    else if (type == "rgba")
    {
        const auto components = split_values(value);
        if (components.size() != 4 || !std::ranges::all_of(components,
                                                           [](const auto& component)
                                                           {
                                                               return parse_integer(component, true);
                                                           }))
        {
            return "expected four unsigned RGBA components";
        }
    }
    else if (type == "intlist")
    {
        const auto components = split_values(value);
        if (components.empty() || !std::ranges::all_of(components,
                                                       [](const auto& component)
                                                       {
                                                           return parse_integer(component, false);
                                                       }))
        {
            return "expected a list of signed integers";
        }
    }
    else if (type == "floatlist" || type == "floatvector3list")
    {
        const auto components = split_values(value);
        if (components.empty() || !std::ranges::all_of(components,
                                                       [](const auto& component)
                                                       {
                                                           return parse_float(component);
                                                       }))
        {
            return "expected a list of floating-point numbers";
        }
    }
    else if (type == "uvslotindex")
    {
        if (!parse_integer(value, true) || value.size() != 1 || value.front() < '0' || value.front() > '3')
        {
            return "expected a UV slot index from 0 to 3";
        }
    }

    const auto semantic_type = fold_ascii(tag.semantic_type);
    const bool enum_value = type == "dynamicenumvalue" || type == "enumvaluelist" || type == "shipclasstype" || type == "projectilecategory" || type == "projectilecategorylist" ||
                            fold_ascii(tag.reference_kind) == "enum" || semantic_type == "flaglist";
    const auto* enumeration = enum_value ? (!tag.enum_name.empty() ? schema.find_enum(tag.enum_name) : schema.find_enum(tag.value_type)) : nullptr;
    if (enumeration != nullptr && !enumeration->values.empty())
    {
        const bool list = type == "enumvaluelist" || type == "projectilecategorylist" || enumeration->bitfield || semantic_type == "flaglist";
        const auto values = list ? split_values(value, true) : std::vector<std::string>{value};
        for (const auto& item : values)
        {
            if (!contains_case_insensitive(enumeration->values, item))
            {
                return "unknown " + enumeration->name + " value '" + item + "'";
            }
        }
    }
    return std::nullopt;
}

bool reference_sentinel(const std::string_view value)
{
    const auto folded = fold_ascii(value);
    return folded.empty() || folded == "none" || folded == "null" || folded == "nil" || folded == "default" || folded == "auto" || folded == "human";
}

const XmlTypeSchema* canonical_type(const XmlSchema& schema, const std::string_view element)
{
    const auto canonical = canonical_name(element);
    const auto found = std::ranges::find_if(schema.types(),
                                            [&](const auto& type)
                                            {
                                                return canonical_name(type.name) == canonical;
                                            });
    return found == schema.types().end() ? nullptr : &*found;
}

} // namespace

const XmlTagSchema* XmlTypeSchema::find_tag(const std::string_view requested) const noexcept
{
    const auto folded = fold_ascii(requested);
    const auto found = std::ranges::find_if(tags,
                                            [&](const auto& tag)
                                            {
                                                return fold_ascii(tag.name) == folded;
                                            });
    return found == tags.end() ? nullptr : &*found;
}

XmlSchema XmlSchema::load(const std::filesystem::path& root, const bool include_foc)
{
    std::vector<std::filesystem::path> roots;
    if (std::filesystem::is_directory(root / "eaw"))
    {
        roots.push_back(root / "eaw");
        if (include_foc && std::filesystem::is_directory(root / "foc"))
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
        auto layer = load_layer(layer_root);
        merge_named(schema.types_, std::move(layer.types));
        merge_named(schema.enums_, std::move(layer.enums));
        merge_named(schema.kinds_, std::move(layer.kinds));
        schema.metafiles_.insert(schema.metafiles_.end(), std::make_move_iterator(layer.metafiles.begin()), std::make_move_iterator(layer.metafiles.end()));
    }
    if (schema.types_.empty())
    {
        throw std::runtime_error("XML schema contains no object types: " + root.string());
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
    const auto folded = fold_ascii(name);
    const auto found = std::ranges::find_if(types_,
                                            [&](const auto& type)
                                            {
                                                return fold_ascii(type.name) == folded;
                                            });
    return found == types_.end() ? nullptr : &*found;
}

const XmlEnumSchema* XmlSchema::find_enum(const std::string_view name) const noexcept
{
    const auto folded = fold_ascii(name);
    const auto found = std::ranges::find_if(enums_,
                                            [&](const auto& enumeration)
                                            {
                                                return fold_ascii(enumeration.name) == folded;
                                            });
    return found == enums_.end() ? nullptr : &*found;
}

const XmlObjectKindSchema* XmlSchema::find_kind(const std::string_view name) const noexcept
{
    const auto folded = fold_ascii(name);
    const auto found = std::ranges::find_if(kinds_,
                                            [&](const auto& kind)
                                            {
                                                return fold_ascii(kind.name) == folded;
                                            });
    return found == kinds_.end() ? nullptr : &*found;
}

XmlSchemaValidationResult XmlSchema::validate(const XmlAssetDatabase& database) const
{
    XmlSchemaValidationResult result;
    std::map<const XmlTypeSchema*, std::map<std::string, const XmlTagSchema*>> tag_indices;
    for (const auto& type : types_)
    {
        auto& index = tag_indices[&type];
        for (const auto& tag : type.tags)
        {
            index[fold_ascii(tag.name)] = &tag;
        }
    }
    const auto find_tag = [&](const XmlTypeSchema* type, const std::string_view name)
    {
        const auto index = tag_indices.find(type);
        if (index == tag_indices.end())
        {
            return static_cast<const XmlTagSchema*>(nullptr);
        }
        const auto tag = index->second.find(fold_ascii(name));
        return tag == index->second.end() ? nullptr : tag->second;
    };

    std::map<std::string, std::vector<std::string>> source_types;
    for (const auto& metafile : metafiles_)
    {
        if (metafile.types.empty())
        {
            continue;
        }
        if (metafile.kind == XmlMetafileKind::direct_content || metafile.kind == XmlMetafileKind::singleton)
        {
            source_types[metafile.path] = metafile.types;
        }
        else if (metafile.kind == XmlMetafileKind::file_registry)
        {
            for (const auto& include : database.includes())
            {
                if (include.source_path == metafile.path)
                {
                    source_types[include.resolved_path] = metafile.types;
                }
            }
        }
    }

    enum class TypeOrigin
    {
        none,
        canonical,
        metafile,
        structural,
    };
    struct TypeAssignment
    {
        const XmlTypeSchema* type{};
        TypeOrigin origin{TypeOrigin::none};
    };

    std::map<const XmlDefinition*, TypeAssignment> type_assignments;
    const auto infer_structural_type = [&](const XmlDefinition& definition)
    {
        if (definition.fields.size() < 2)
        {
            return static_cast<const XmlTypeSchema*>(nullptr);
        }
        const XmlTypeSchema* best = nullptr;
        std::size_t best_matches = 0;
        bool tied = false;
        for (const auto& candidate : types_)
        {
            if (fold_ascii(candidate.name) == "gameobjecttype" || candidate.tags.empty())
            {
                continue;
            }
            const auto matches = static_cast<std::size_t>(std::ranges::count_if(definition.fields,
                                                                                [&](const auto& field)
                                                                                {
                                                                                    return find_tag(&candidate, field.name) != nullptr;
                                                                                }));
            if (matches > best_matches)
            {
                best = &candidate;
                best_matches = matches;
                tied = false;
            }
            else if (matches != 0 && matches == best_matches)
            {
                tied = true;
            }
        }
        if (best_matches < 2 || best_matches * 100 < definition.fields.size() * 60 || tied)
        {
            return static_cast<const XmlTypeSchema*>(nullptr);
        }
        return best;
    };

    for (const auto& definition : database.definitions())
    {
        ++result.definitions_considered;
        TypeAssignment assignment;
        assignment.type = canonical_type(*this, definition.type);
        if (assignment.type != nullptr)
        {
            assignment.origin = TypeOrigin::canonical;
            ++result.canonical_type_definitions;
        }
        else if (definition.depth <= 1)
        {
            const auto source = source_types.find(definition.source_path);
            if (source != source_types.end() && !source->second.empty())
            {
                assignment.type = find_type(source->second.front());
                if (assignment.type != nullptr)
                {
                    assignment.origin = TypeOrigin::metafile;
                    ++result.metafile_type_definitions;
                }
            }
        }
        if (assignment.type == nullptr)
        {
            assignment.type = infer_structural_type(definition);
            if (assignment.type != nullptr)
            {
                assignment.origin = TypeOrigin::structural;
                ++result.structural_type_definitions;
            }
        }
        if (assignment.type == nullptr)
        {
            ++result.untyped_definitions;
        }
        else
        {
            ++result.definitions_validated;
        }
        type_assignments[&definition] = assignment;
    }

    std::map<std::string, std::vector<const XmlDefinition*>> definitions_by_name;
    for (const auto& definition : database.definitions())
    {
        definitions_by_name[fold_ascii(definition.name)].push_back(&definition);
    }

    std::map<std::string, std::set<std::string>> member_kinds_by_name;
    for (const auto& kind : kinds_)
    {
        if (kind.member_of.empty())
        {
            continue;
        }
        for (const auto& owner : database.definitions())
        {
            for (const auto& field : owner.fields)
            {
                if (!contains_case_insensitive(kind.member_of, field.name))
                {
                    continue;
                }
                for (const auto& member : split_values(field.text))
                {
                    member_kinds_by_name[fold_ascii(member)].insert(fold_ascii(kind.name));
                }
            }
        }
    }

    const auto truthy = [](const std::string_view value)
    {
        const auto folded = fold_ascii(trim(value));
        return folded == "yes" || folded == "true" || folded == "1";
    };
    using EffectiveFieldKey = std::pair<const XmlDefinition*, std::string>;
    std::map<EffectiveFieldKey, std::vector<std::string>> effective_field_cache;
    std::set<EffectiveFieldKey> effective_field_stack;
    std::function<std::vector<std::string>(const XmlDefinition&, const std::string&)> effective_field_values;
    effective_field_values = [&](const XmlDefinition& definition, const std::string& folded_name)
    {
        const EffectiveFieldKey key{&definition, folded_name};
        if (const auto cached = effective_field_cache.find(key); cached != effective_field_cache.end())
        {
            return cached->second;
        }
        if (!effective_field_stack.insert(key).second)
        {
            return std::vector<std::string>{};
        }

        std::vector<std::string> values;
        for (const auto& field : definition.fields)
        {
            if (fold_ascii(field.name) == folded_name)
            {
                values.push_back(field.text);
            }
        }
        if (values.empty())
        {
            for (const auto& field : definition.fields)
            {
                if (fold_ascii(field.name) != "variant_of_existing_type")
                {
                    continue;
                }
                for (const auto& parent_name : split_values(field.text))
                {
                    if (reference_sentinel(parent_name))
                    {
                        continue;
                    }
                    const auto parents = definitions_by_name.find(fold_ascii(parent_name));
                    if (parents == definitions_by_name.end())
                    {
                        continue;
                    }
                    for (const auto* parent : parents->second)
                    {
                        auto inherited = effective_field_values(*parent, folded_name);
                        values.insert(values.end(), std::make_move_iterator(inherited.begin()), std::make_move_iterator(inherited.end()));
                    }
                }
            }
        }
        effective_field_stack.erase(key);
        effective_field_cache[key] = values;
        return values;
    };

    std::map<const XmlDefinition*, std::set<std::string>> definition_kinds;
    for (const auto& definition : database.definitions())
    {
        std::set<std::string> behaviors;
        for (const auto& value : effective_field_values(definition, "behavior"))
        {
            for (auto behavior : split_values(value, true))
            {
                behaviors.insert(fold_ascii(behavior));
            }
        }
        for (const auto& kind : kinds_)
        {
            const bool behavior_match = kind.behaviors.empty() || std::ranges::any_of(kind.behaviors,
                                                                                      [&](const auto& behavior)
                                                                                      {
                                                                                          return behaviors.contains(fold_ascii(behavior));
                                                                                      });
            const bool flag_match = kind.flags.empty() || std::ranges::any_of(kind.flags,
                                                                              [&](const auto& flag)
                                                                              {
                                                                                  const auto values = effective_field_values(definition, fold_ascii(flag));
                                                                                  return std::ranges::any_of(values, truthy);
                                                                              });
            const auto membership = member_kinds_by_name.find(fold_ascii(definition.name));
            const bool member_match = kind.member_of.empty() || (membership != member_kinds_by_name.end() && membership->second.contains(fold_ascii(kind.name)));
            if (behavior_match && flag_match && member_match)
            {
                definition_kinds[&definition].insert(fold_ascii(kind.name));
            }
        }
    }

    const auto add_diagnostic = [&](const XmlSchemaDiagnosticSeverity severity, const XmlSchemaDiagnosticKind kind, const XmlDefinition& definition, const XmlNode& field, std::string message)
    {
        result.diagnostics.push_back({severity, kind, definition.source_path, field.line, definition.type, definition.name, field.name, std::move(message)});
    };

    for (const auto& definition : database.definitions())
    {
        const auto type_assignment = type_assignments.find(&definition);
        const auto* type = type_assignment == type_assignments.end() ? nullptr : type_assignment->second.type;
        if (type == nullptr)
        {
            continue;
        }

        std::map<std::string, std::vector<const XmlNode*>> fields_by_name;
        for (const auto& field : definition.fields)
        {
            fields_by_name[fold_ascii(field.name)].push_back(&field);
            const auto* tag = find_tag(type, field.name);
            if (tag == nullptr)
            {
                ++result.unknown_fields;
                add_diagnostic(XmlSchemaDiagnosticSeverity::warning, XmlSchemaDiagnosticKind::unknown_field, definition, field, "field is not declared by schema type " + type->name);
                continue;
            }
            ++result.fields_validated;
            if (const auto error = validate_value(*tag, field, *this))
            {
                ++result.invalid_values;
                auto shown_value = trim(field.text);
                if (shown_value.size() > 100)
                {
                    shown_value.resize(97);
                    shown_value += "...";
                }
                add_diagnostic(XmlSchemaDiagnosticSeverity::error, XmlSchemaDiagnosticKind::invalid_value, definition, field, *error + " (got '" + shown_value + "')");
            }

            if (fold_ascii(tag->reference_kind) == "xmlobject")
            {
                const auto value_type = fold_ascii(tag->value_type);
                const auto semantic_type = fold_ascii(tag->semantic_type);
                if (semantic_type == "referencegroup")
                {
                    continue;
                }
                const bool list = value_type.ends_with("list") || semantic_type == "planetvaluepair";
                auto references = list ? split_values(field.text) : std::vector<std::string>{trim(field.text)};
                if (semantic_type == "factionaiplayerpairlist")
                {
                    std::vector<std::string> selected;
                    for (std::size_t index = 1; index < references.size(); index += 2)
                    {
                        selected.push_back(std::move(references[index]));
                    }
                    references = std::move(selected);
                }
                else if (semantic_type == "planetmodepairlist" || semantic_type == "planetvaluepair")
                {
                    std::vector<std::string> selected;
                    for (std::size_t index = 0; index < references.size(); index += 2)
                    {
                        selected.push_back(std::move(references[index]));
                    }
                    references = std::move(selected);
                }
                for (const auto& reference : references)
                {
                    if (reference_sentinel(reference))
                    {
                        continue;
                    }
                    const auto targets = definitions_by_name.find(fold_ascii(reference));
                    if (targets == definitions_by_name.end())
                    {
                        ++result.unresolved_references;
                        add_diagnostic(XmlSchemaDiagnosticSeverity::warning, XmlSchemaDiagnosticKind::unresolved_reference, definition, field, "unresolved XML object reference '" + reference + "'");
                        continue;
                    }
                    const auto* expected_type = tag->reference_type.empty() ? nullptr : find_type(tag->reference_type);
                    const auto* expected_kind = tag->reference_type.empty() ? nullptr : find_kind(tag->reference_type);
                    if (expected_type == nullptr && expected_kind == nullptr)
                    {
                        continue;
                    }
                    const bool type_match = std::ranges::any_of(targets->second,
                                                                [&](const auto* target)
                                                                {
                                                                    if (expected_type != nullptr)
                                                                    {
                                                                        const auto target_type = type_assignments.find(target);
                                                                        return target_type != type_assignments.end() && target_type->second.type == expected_type;
                                                                    }
                                                                    const auto target_kinds = definition_kinds.find(target);
                                                                    return target_kinds != definition_kinds.end() && target_kinds->second.contains(fold_ascii(expected_kind->name));
                                                                });
                    if (!type_match)
                    {
                        ++result.mismatched_references;
                        add_diagnostic(XmlSchemaDiagnosticSeverity::warning, XmlSchemaDiagnosticKind::reference_type_mismatch, definition, field,
                                       "XML object reference '" + reference + "' does not match expected type/kind " + tag->reference_type);
                    }
                }
            }
        }

        for (const auto& [name, matches] : fields_by_name)
        {
            (void)name;
            if (matches.size() < 2)
            {
                continue;
            }
            const auto* tag = find_tag(type, matches.front()->name);
            if (tag != nullptr && !tag->multiple_allowed)
            {
                ++result.duplicate_fields;
                add_diagnostic(XmlSchemaDiagnosticSeverity::warning, XmlSchemaDiagnosticKind::duplicate_field, definition, *matches[1],
                               "field occurs " + std::to_string(matches.size()) + " times but the schema does not allow repetition");
            }
        }
    }
    return result;
}

std::string_view to_string(const XmlSchemaDiagnosticSeverity severity) noexcept
{
    return severity == XmlSchemaDiagnosticSeverity::error ? "error" : "warning";
}

std::string_view to_string(const XmlSchemaDiagnosticKind kind) noexcept
{
    switch (kind)
    {
    case XmlSchemaDiagnosticKind::unknown_field:
        return "unknown-field";
    case XmlSchemaDiagnosticKind::duplicate_field:
        return "duplicate-field";
    case XmlSchemaDiagnosticKind::invalid_value:
        return "invalid-value";
    case XmlSchemaDiagnosticKind::unresolved_reference:
        return "unresolved-reference";
    case XmlSchemaDiagnosticKind::reference_type_mismatch:
        return "reference-type-mismatch";
    }
    return "unknown";
}

} // namespace eaw
