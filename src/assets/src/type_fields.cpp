#include "assets/type_fields.hpp"

#include "core/string_utils.hpp"
#include "formats/xml_document.hpp"

#include <charconv>
#include <cmath>
#include <utility>

namespace eaw {

std::optional<float> parse_xml_number(std::string_view text, std::string_view* trailing) noexcept
{
    text = ascii_trim(text);
    if (!text.empty() && text.front() == '+')
    {
        text.remove_prefix(1);
    }
    float value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end == text.data() || !std::isfinite(value))
    {
        return std::nullopt;
    }
    if (trailing != nullptr)
    {
        auto rest = text.substr(static_cast<std::size_t>(end - text.data()));
        if (rest == "f" || rest == "F")
        {
            rest = {};
        }
        *trailing = rest;
    }
    return value;
}

TypeFields::TypeFields(const GameObjectCatalog& catalog, const XmlDefinition& definition, std::vector<std::string>* warnings) noexcept : catalog_(catalog), definition_(definition), warnings_(warnings)
{}

const XmlNode* TypeFields::node(const std::string_view field) const
{
    return catalog_.field(definition_, field).field;
}

std::vector<const XmlNode*> TypeFields::nodes(const std::string_view field) const
{
    return catalog_.fields(definition_, field).fields;
}

std::string_view TypeFields::text(const std::string_view field) const
{
    const auto* found = node(field);
    return found != nullptr ? ascii_trim(found->text) : std::string_view{};
}

std::optional<float> TypeFields::number(const std::string_view field) const
{
    const auto* found = node(field);
    if (found == nullptr)
    {
        return std::nullopt;
    }
    std::string_view trailing;
    const auto value = parse_xml_number(found->text, &trailing);
    if (!value)
    {
        warn(field, found->text, "a number");
    }
    else if (!trailing.empty())
    {
        warn(field, found->text, "a number alone (the text after it is ignored)");
    }
    return value;
}

float TypeFields::amount(const std::string_view field, const float fallback) const
{
    const auto* found = node(field);
    if (found == nullptr)
    {
        return fallback;
    }
    std::string_view trailing;
    const auto value = parse_xml_number(found->text, &trailing);
    if (!value || *value < 0.0F)
    {
        warn(field, found->text, "a non-negative number");
        return 0.0F;
    }
    if (!trailing.empty())
    {
        warn(field, found->text, "a number alone (the text after it is ignored)");
    }
    return *value;
}

float TypeFields::signed_value(const std::string_view field) const
{
    return node(field) != nullptr ? number(field).value_or(0.0F) : 0.0F;
}

bool TypeFields::flag(const std::string_view field, const bool fallback) const
{
    const auto* found = node(field);
    if (found == nullptr)
    {
        return fallback;
    }
    if (const auto value = parse_xml_bool(found->text))
    {
        return *value;
    }
    warn(field, found->text, "a yes/no value");
    return fallback;
}

std::vector<std::string_view> TypeFields::list(const std::string_view field) const
{
    const auto* found = node(field);
    return found != nullptr ? split_list(found->text) : std::vector<std::string_view>{};
}

void TypeFields::warn(const std::string_view field, const std::string_view value, const std::string_view expected) const
{
    if (warnings_ == nullptr)
    {
        return;
    }
    std::string message = definition_.name + ": " + std::string(field) + " '" + std::string(value) + "' is not " + std::string(expected);
    if (const auto* declared = catalog_.field(definition_, field).declared_by; declared != nullptr && declared != &definition_)
    {
        message += " (from " + declared->name + ")";
    }
    warnings_->push_back(std::move(message));
}

} // namespace eaw
