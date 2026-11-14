// type-tags: which XML types set the given tags, and to what. For every named definition where any
// of the tags resolves (through Variant_Of_Existing_Type), one row: the type, its element, its land
// model, and each tag's value ("-" when absent). For surveys such as which types use a walker tag
// without another, instead of reading the XML by hand.

#include "core/string_utils.hpp"
#include "assets/game_object_catalog.hpp"
#include "assets/xml_asset_database.hpp"
#include "asset_inspector/inspector.hpp"

#include <iostream>

namespace eaw::asset_inspector {

eaw::Status type_tags(const eaw::VirtualFileSystem& vfs, const std::string& tags)
{
    std::vector<std::string> names;
    for (const auto tag : split_list(tags))
    {
        names.emplace_back(tag);
    }
    if (names.empty())
    {
        return eaw::make_error("type-tags: no tags given (a comma-separated list such as Walk_Transition,Has_Pre_Turn_Anim)");
    }
    const auto database = eaw::XmlAssetDatabase::build(vfs);
    const auto catalog = eaw::GameObjectCatalog::build(database);
    std::size_t rows = 0;
    std::cout << "type\telement\tland model";
    for (const auto& name : names)
    {
        std::cout << '\t' << name;
    }
    std::cout << '\n';
    for (const auto& definition : database.definitions())
    {
        if (definition.name.empty())
        {
            continue;
        }
        std::vector<std::string> values;
        bool any = false;
        for (const auto& name : names)
        {
            const auto* field = catalog.field(definition, name).field;
            any = any || field != nullptr;
            values.push_back(field != nullptr ? std::string(eaw::ascii_trim(field->text)) : std::string("-"));
        }
        if (!any)
        {
            continue;
        }
        const auto model = catalog.model(definition, eaw::GameModelContext::land);
        std::cout << definition.name << '\t' << definition.type << '\t' << (model ? model->value : std::string("-"));
        for (const auto& value : values)
        {
            std::cout << '\t' << value;
        }
        std::cout << '\n';
        ++rows;
    }
    std::cerr << rows << " types set at least one of the tags\n";
    return {};
}

} // namespace eaw::asset_inspector
