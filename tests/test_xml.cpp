#include "test_support.hpp"
#include "formats/xml_helpers.hpp"
#include "assets/type_hardpoints.hpp"

namespace {

using namespace eaw_test;

void test_xml_document()
{
    const auto document = expect_ok(eaw::read_xml("\xef\xbb\xbf<?xml version=\"1.0\"?>\n"
                                                  "<!-- a Petroglyph file may contain several roots -->\n"
                                                  "<SpaceUnit Name='Unit_A' Enabled=\"Yes\">\n"
                                                  "  <Text_ID>TEXT_A &amp; B</Text_ID>\n"
                                                  "  <Payload><![CDATA[x < y]]></Payload>\n"
                                                  "</SpaceUnit>\n"
                                                  "<SpaceUnit Name=\"Unit_B\"><Value>&#x41;&#66;</Value></SpaceUnit>"),
                                    "XML parse");
    require(document.roots.size() == 2, "XML fragment root parsing failed");
    const auto& first = document.roots.front();
    require(first.name == "SpaceUnit" && xml_attribute(first, "name") != nullptr && xml_attribute(first, "NAME")->value == "Unit_A", "XML case-insensitive attribute lookup failed");
    require(xml_child(first, "text_id") != nullptr && xml_child(first, "Text_ID")->text == "TEXT_A & B", "XML entity decoding failed");
    require(xml_child(first, "Payload") != nullptr && xml_child(first, "Payload")->text == "x < y", "XML CDATA parsing failed");
    require(xml_child(document.roots[1], "Value")->text == "AB", "XML numeric character decoding failed");
    require(expect_ok(eaw::read_xml("<?xml version=\"1.0\"><A />"), "XML declaration parse").roots.size() == 1, "Petroglyph XML declaration compatibility failed");

    const auto mismatch = eaw::read_xml("<A><B /></C>");
    require(!mismatch.ok() && mismatch.error().message.find("closing element </C> does not match <A>") != std::string::npos, "mismatched XML closing element was accepted");
    // Failures end parsing (no hang) and report the first problem with its line.
    const auto unterminated = eaw::read_xml("<A>\n<B attribute=\"x\"");
    require(!unterminated.ok() && unterminated.error().message.starts_with("XML line 2:"), "unterminated XML was accepted");
    require(!eaw::read_xml("<A>&#xd800;</A>").ok(), "an invalid XML character reference was accepted");
}

void test_xml_asset_database()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-xml-database-test";
    const auto xml_root = root / "Data" / "XML";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(xml_root);
    {
        std::ofstream file(xml_root / "GameObjectFiles.xml", std::ios::binary);
        file << "<GameObjectFiles><File>Units.xml</File>"
                "<File>Missing.xml</File></GameObjectFiles>";
    }
    {
        std::ofstream file(xml_root / "Units.xml", std::ios::binary);
        file << "<SpaceUnit Name=\"Unit_A\"><Text_ID>TEXT_A</Text_ID>"
                "<Build_Cost_Credits>100</Build_Cost_Credits></SpaceUnit>"
                "<SpaceUnit Name=\"Unit_B\" />";
    }
    {
        std::ofstream file(xml_root / "Overrides.xml", std::ios::binary);
        file << "<SpaceUnit Name=\"Unit_A\"><Build_Cost_Credits>200"
                "</Build_Cost_Credits></SpaceUnit>";
    }

    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    const auto database = eaw::XmlAssetDatabase::build(vfs);
    require(database.document_count() == 3 && database.parsed_document_count() == 3 && database.failures().empty(), "XML database document accounting failed");
    require(database.definitions().size() == 3, "XML database named definition discovery failed");
    const auto matches = database.find("spaceunit", "unit_a");
    const auto ordered_fields = std::ranges::find_if(matches,
                                                     [](const auto* match)
                                                     {
                                                         return match->fields.size() == 2 && match->fields[0].name == "Text_ID" && match->fields[1].name == "Build_Cost_Credits";
                                                     });
    require(matches.size() == 2 && ordered_fields != matches.end(), "XML database typed lookup or field order failed");
    require(database.duplicates().size() == 1, "XML duplicate definition diagnostics failed");
    require(database.includes().size() == 2 && database.includes()[0].resolved && !database.includes()[1].resolved, "XML include resolution diagnostics failed");
    std::filesystem::remove_all(root, ignored);
}

void test_game_object_catalog()
{
    // CRC-32 of the uppercased name; reference values from an independent CRC-32 implementation.
    require(eaw::game_object_name_crc("Unit_A") == 0xbb137f89U && eaw::game_object_name_crc("UNIT_A") == 0xbb137f89U, "object-name CRC is not case-insensitive CRC-32");

    const auto root = std::filesystem::temp_directory_path() / "eawengine-object-catalog-test";
    const auto xml_root = root / "Data" / "XML";
    const auto model_root = root / "Data" / "Art" / "Models";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(xml_root);
    std::filesystem::create_directories(model_root);
    {
        std::ofstream file(xml_root / "Units.xml", std::ios::binary);
        file << "<SpaceUnits>"
                "<SpaceUnit Name=\"Unit_A\"><Space_Model_Name> EV_Unit.alo </Space_Model_Name><Land_Model_Name>EI_Unit</Land_Model_Name></SpaceUnit>"
                "<SpaceUnit Name=\"Unit_A_Variant\"><Variant_Of_Existing_Type>unit_a</Variant_Of_Existing_Type>"
                "<Land_Model_Name>EI_Variant.alo</Land_Model_Name></SpaceUnit>"
                "<SpaceUnit Name=\"Orphan\"><Variant_Of_Existing_Type>Not_Defined</Variant_Of_Existing_Type></SpaceUnit>"
                "<SpaceUnit Name=\"Loop_A\"><Variant_Of_Existing_Type>Loop_B</Variant_Of_Existing_Type></SpaceUnit>"
                "<SpaceUnit Name=\"Loop_B\"><Variant_Of_Existing_Type>Loop_A</Variant_Of_Existing_Type></SpaceUnit>"
                "<SFXEvent Name=\"Shared_Name\" />"
                "<SpaceProp Name=\"Shared_Name\"><Space_Model_Name>Missing_Model</Space_Model_Name></SpaceProp>"
                "<Squadron Name=\"Test_Squadron\"><Squadron_Units>Unit_A</Squadron_Units><Squadron_Units>Unit_A, Unit_A_Variant, Missing_Member</Squadron_Units>"
                "<Squadron_Offsets>10.0, 0.0, 0.0</Squadron_Offsets><Squadron_Offsets>bad</Squadron_Offsets></Squadron>"
                "<Container Name=\"Test_Team\" />"
                "<Squadron Name=\"Team_Squadron\"><Squadron_Units>Unit_A, Unit_A</Squadron_Units><Create_Team_Type>Offset_Team</Create_Team_Type></Squadron>"
                "<Container Name=\"Offset_Team\"><Squadron_Offsets>0, 25, 0</Squadron_Offsets></Container>"
                "<GroundCompany Name=\"Test_Company\"><Land_Model_Name>EI_Unit</Land_Model_Name><Company_Units>Unit_A Unit_A, Missing_Member</Company_Units></GroundCompany>"
                "<Faction Name=\"Test_Empire\"><Color> 54, 134, 242, 255 </Color></Faction>"
                "<Faction Name=\"Broken_Faction\"><Color>red</Color></Faction>"
                "<SpaceUnit Name=\"Imperial_Unit\"><Affiliation>Test_Empire, Rebel</Affiliation></SpaceUnit>"
                "<SpaceUnit Name=\"Broken_Unit\"><Affiliation>Broken_Faction</Affiliation></SpaceUnit>"
                "<SpaceUnit Name=\"Gunship\"><HardPoints> HP_Gun, Shared_HP Missing_HP </HardPoints></SpaceUnit>"
                "<SpaceUnit Name=\"Gunship_Copy\"><Variant_Of_Existing_Type>Gunship</Variant_Of_Existing_Type></SpaceUnit>"
                "<HardPoint Name=\"HP_Gun\" /><SFXEvent Name=\"Shared_HP\" /><HardPoint Name=\"Shared_HP\" />"
                "</SpaceUnits>";
    }
    std::ofstream(model_root / "EV_UNIT.ALO", std::ios::binary) << "x";
    std::ofstream(model_root / "EI_Unit.alo", std::ios::binary) << "x";

    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    const auto database = eaw::XmlAssetDatabase::build(vfs);
    const auto catalog = eaw::GameObjectCatalog::build(database);

    // A type's hardpoints come from its (inherited) HardPoints list, preferring HardPoint definitions.
    const auto hardpoints = eaw::type_hardpoints(catalog, *catalog.find("Gunship_Copy").front());
    require(hardpoints.size() == 3 && hardpoints[0].name == "HP_Gun" && hardpoints[0].definition != nullptr && hardpoints[1].definition != nullptr && hardpoints[1].definition->type == "HardPoint" &&
                hardpoints[2].name == "Missing_HP" && hardpoints[2].definition == nullptr,
            "a type's hardpoint list was not resolved");
    require(eaw::type_hardpoints(catalog, *catalog.find("Unit_A").front()).empty(), "a type without hardpoints must list none");

    const auto unit = catalog.resolve(0xbb137f89U, eaw::GameModelContext::space);
    require(unit.definition != nullptr && unit.definition->name == "Unit_A" && unit.candidates == 1, "object CRC did not resolve to its definition");
    const auto space_model = catalog.model(*unit.definition, eaw::GameModelContext::space);
    require(space_model && space_model->value == "EV_Unit.alo" && space_model->field == "Space_Model_Name", "space model reference was not read and trimmed");

    // A variant inherits fields it does not override and keeps the ones it does.
    const auto variant = catalog.find("UNIT_A_VARIANT");
    require(variant.size() == 1, "case-insensitive name lookup failed");
    const auto inherited = catalog.model(*variant.front(), eaw::GameModelContext::space);
    const auto overridden = catalog.model(*variant.front(), eaw::GameModelContext::land);
    require(inherited && inherited->value == "EV_Unit.alo" && inherited->declared_by == unit.definition && overridden && overridden->value == "EI_Variant.alo", "variant field inheritance failed");
    require(!catalog.model(*variant.front(), eaw::GameModelContext::galactic), "absent model field was reported");

    std::vector<std::string> diagnostics;
    require(!catalog.model(*catalog.find("Orphan").front(), eaw::GameModelContext::space, &diagnostics) && diagnostics.size() == 1 && diagnostics.front().find("Not_Defined") != std::string::npos,
            "missing variant parent was not diagnosed");
    diagnostics.clear();
    require(!catalog.model(*catalog.find("Loop_A").front(), eaw::GameModelContext::space, &diagnostics) && diagnostics.size() == 1 && diagnostics.front().find("cycle") != std::string::npos,
            "variant cycle was not diagnosed");

    // When names collide across element types, the definition with a model wins the CRC.
    const auto shared = catalog.resolve(eaw::game_object_name_crc("Shared_Name"), eaw::GameModelContext::space);
    require(shared.definition != nullptr && shared.definition->type == "SpaceProp" && shared.candidates == 2, "ambiguous CRC did not prefer the modeled definition");
    require(catalog.resolve(0x12345678U, eaw::GameModelContext::space).definition == nullptr, "unknown CRC resolved");

    require(eaw::resolve_model_path(vfs, "ev_unit.alo").has_value() && eaw::resolve_model_path(vfs, "Art\\Models\\EI_Unit").has_value(), "model reference did not resolve through the VFS");
    require(!eaw::resolve_model_path(vfs, "Missing_Model") && !eaw::resolve_model_path(vfs, "  ") && !eaw::resolve_model_path(vfs, "..\\"), "missing model reference resolved");

    // Placed-object transforms: degrees, uniform scale, then translation (row-vector convention).
    eaw::TedPlacedObject placed;
    placed.crc = 0xbb137f89U;
    placed.scale = 2.0F;
    placed.rotation = {0.0F, 0.0F, 90.0F};
    placed.position = {10.0F, 20.0F, 30.0F};
    placed.has_transform = true;
    // The Z rotation is the object's facing: a model facing -Y placed at 90 degrees faces +Y, and its
    // +X side ends up at -X.
    const auto forward = eaw::transform_point({0.0F, -1.0F, 0.0F}, eaw::placed_object_transform(placed));
    const auto side = eaw::transform_point({1.0F, 0.0F, 0.0F}, eaw::placed_object_transform(placed));
    require(std::fabs(forward.x - 10.0F) < 1.0e-4F && std::fabs(forward.y - 22.0F) < 1.0e-4F && std::fabs(forward.z - 30.0F) < 1.0e-4F && std::fabs(side.x - 8.0F) < 1.0e-4F &&
                std::fabs(side.y - 20.0F) < 1.0e-4F,
            "placed-object facing/scale/translation is wrong");
    auto degenerate = placed;
    degenerate.scale = 0.0F;
    degenerate.rotation = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    const auto safe = eaw::transform_point({1.0F, 0.0F, 0.0F}, eaw::placed_object_transform(degenerate));
    require(std::isfinite(safe.x) && std::fabs(safe.x - 10.0F) < 1.0e-4F && std::fabs(safe.y - 21.0F) < 1.0e-4F, "degenerate placed-object transform was not sanitized");

    eaw::TedMapData map;
    map.type = eaw::TedMapType::space;
    map.placed_objects.push_back(placed);
    auto unknown = placed;
    unknown.crc = 0x12345678U;
    map.placed_objects.push_back(unknown);
    auto unpositioned = placed;
    unpositioned.has_transform = false;
    map.placed_objects.push_back(unpositioned);
    auto prop = placed;
    prop.crc = eaw::game_object_name_crc("Shared_Name");
    map.placed_objects.push_back(prop);
    const auto layout = eaw::build_map_object_layout(map, catalog, vfs);
    require(layout.placements.size() == 3 && layout.untyped == 1 && layout.missing_model_files == 1 && layout.placements[0].model_path == "DATA/ART/MODELS/EV_UNIT.ALO" &&
                layout.placements[0].type == unit.definition && layout.placements[2].object_index == 3 && layout.placements[2].model_path.empty(),
            "map object layout resolution failed");

    // Faction colors come from the first affiliated faction's 0..255 Color.
    const auto imperial_color = catalog.affiliation_color(*catalog.find("Imperial_Unit").front());
    require(imperial_color && std::fabs((*imperial_color)[0] - 54.0F / 255.0F) < 1.0e-6F && std::fabs((*imperial_color)[2] - 242.0F / 255.0F) < 1.0e-6F && (*imperial_color)[3] == 1.0F,
            "affiliation faction color was not resolved");
    require(!catalog.affiliation_color(*catalog.find("Broken_Unit").front()) && !catalog.affiliation_color(*unit.definition), "missing or malformed faction colors were accepted");

    // Repeated fields keep source order; a single-valued lookup takes the last occurrence.
    const auto squadron_type = catalog.find("Test_Squadron");
    require(squadron_type.size() == 1 && catalog.fields(*squadron_type.front(), "Squadron_Offsets").fields.size() == 2 &&
                catalog.field(*squadron_type.front(), "Squadron_Units").field->text.find("Missing_Member") != std::string::npos,
            "repeated XML field lookup failed");

    // A placed space squadron expands into its members at their rotated Squadron_Offsets (a repeated
    // Squadron_Units continues the list: Unit_A, then Unit_A, Unit_A_Variant, Missing_Member); a land
    // team container is counted instead of guessed.
    eaw::TedMapData squadron_map;
    squadron_map.type = eaw::TedMapType::space;
    auto squadron_object = placed;
    squadron_object.scale = 1.0F;
    squadron_object.crc = eaw::game_object_name_crc("Test_Squadron");
    squadron_map.placed_objects.push_back(squadron_object);
    auto team = placed;
    team.crc = eaw::game_object_name_crc("Test_Team");
    squadron_map.placed_objects.push_back(team);
    const auto squadron_layout = eaw::build_map_object_layout(squadron_map, catalog, vfs);
    require(squadron_layout.expanded_members == 3 && squadron_layout.untyped == 1 && squadron_layout.unexpanded_containers == 1 && squadron_layout.placements.size() == 4 &&
                squadron_layout.placements[2].type != nullptr && squadron_layout.placements[2].type->name == "Unit_A_Variant",
            "squadron expansion counts are wrong");
    const auto& lead = squadron_layout.placements[0];
    const auto lead_origin = eaw::transform_point({}, lead.transform);
    require(lead.container == squadron_type.front() && lead.member == 0 && lead.type == unit.definition && std::fabs(lead_origin.x - 10.0F) < 1.0e-4F && std::fabs(lead_origin.y - 30.0F) < 1.0e-4F &&
                std::fabs(lead_origin.z - 30.0F) < 1.0e-4F,
            "squadron member offset was not rotated into the squadron frame");
    const auto wing_origin = eaw::transform_point({}, squadron_layout.placements[1].transform);
    require(squadron_layout.placements[1].member == 1 && std::fabs(wing_origin.x - 10.0F) < 1.0e-4F && std::fabs(wing_origin.y - 20.0F) < 1.0e-4F,
            "malformed squadron offset did not fall back to the squadron origin");
    // A squadron without offsets takes its team container's; members beyond the list line up astern.
    eaw::TedMapData team_map;
    team_map.type = eaw::TedMapType::space;
    auto team_squadron = squadron_object;
    team_squadron.crc = eaw::game_object_name_crc("Team_Squadron");
    team_map.placed_objects.push_back(team_squadron);
    const auto team_layout = eaw::build_map_object_layout(team_map, catalog, vfs);
    require(team_layout.placements.size() == 2, "the team squadron must expand into both members");
    const auto first = eaw::transform_point({}, team_layout.placements[0].transform);
    const auto second = eaw::transform_point({}, team_layout.placements[1].transform);
    require(std::fabs(eaw::distance(first, second) - std::sqrt(25.0F * 25.0F + 40.0F * 40.0F)) < 1.0e-3F, "squadron offsets must come from the team container, the rest astern");
    // A land company expands into its members (listed with commas or whitespace) at the company's
    // transform, flagged for arrangement, even though it names a model of its own; a container
    // without a member list is still counted.
    eaw::TedMapData land_map;
    land_map.type = eaw::TedMapType::land;
    auto company_object = placed;
    company_object.crc = eaw::game_object_name_crc("Test_Company");
    land_map.placed_objects.push_back(company_object);
    land_map.placed_objects.push_back(team);
    const auto company_layout = eaw::build_map_object_layout(land_map, catalog, vfs);
    require(company_layout.company_members == 2 && company_layout.untyped == 1 && company_layout.unexpanded_containers == 1 && company_layout.placements.size() == 3,
            "company expansion counts are wrong");
    const auto& soldier = company_layout.placements[0];
    require(soldier.needs_formation && soldier.member == 0 && company_layout.placements[1].member == 1 && soldier.type == unit.definition &&
                soldier.transform.values == eaw::placed_object_transform(company_object).values && !company_layout.placements[2].needs_formation,
            "company members must take the company transform and be flagged for arrangement");
    const auto mentions = [&](const std::string_view text)
    {
        return std::ranges::any_of(squadron_layout.diagnostics,
                                   [&](const std::string& message)
                                   {
                                       return message.find(text) != std::string::npos;
                                   });
    };
    require(mentions("malformed Squadron_Offsets 'bad'") && mentions("'Missing_Member' is not defined"), "squadron expansion diagnostics are missing");
    std::filesystem::remove_all(root, ignored);
}

void test_xml_schema_validation()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-xml-schema-test";
    const auto game_root = root / "game";
    const auto xml_root = game_root / "Data" / "XML";
    const auto schema_root = root / "schema" / "eaw";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(xml_root);
    std::filesystem::create_directories(schema_root / "tags");
    std::filesystem::create_directories(schema_root / "enums");
    std::filesystem::create_directories(schema_root / "meta");

    {
        std::ofstream file(schema_root / "types.yaml", std::ios::binary);
        file << "types:\n"
                "  - typeName: GameObjectType\n"
                "    nameTag: Name\n"
                "  - typeName: StoryParser\n"
                "    nameTag: Name\n";
    }
    {
        std::ofstream file(schema_root / "tags" / "GameObjectType.yaml", std::ios::binary);
        file << "tags:\n"
                "  - tag: Is_Buildable\n"
                "    type: Boolean\n"
                "  - tag: Build_Cost_Credits\n"
                "    type: UInt\n"
                "  - tag: Faction\n"
                "    type: DynamicEnumValue\n"
                "    enumName: FactionName\n"
                "  - tag: Target\n"
                "    type: NameReference\n"
                "    referenceKind: xmlObject\n"
                "    referenceType: GameObjectType\n"
                "  - tag: Buildable_Target\n"
                "    type: NameReference\n"
                "    referenceKind: xmlObject\n"
                "    referenceType: Buildable\n"
                "  - tag: Inherited_Buildable_Target\n"
                "    type: NameReference\n"
                "    referenceKind: xmlObject\n"
                "    referenceType: Buildable\n"
                "  - tag: Variant_Of_Existing_Type\n"
                "    type: NameReference\n"
                "    referenceKind: xmlObject\n"
                "    referenceType: GameObjectType\n"
                "  - tag: Repeated\n"
                "    type: Float\n";
    }
    {
        std::ofstream file(schema_root / "tags" / "StoryParser.yaml", std::ios::binary);
        file << "tags:\n"
                "  - tag: Event_Type\n"
                "    type: NameReference\n"
                "  - tag: Reward_Type\n"
                "    type: NameReference\n";
    }
    {
        std::ofstream file(schema_root / "enums" / "FactionName.yaml", std::ios::binary);
        file << "name: FactionName\n"
                "values:\n"
                "  - name: Empire\n"
                "  - name: Rebel\n";
    }
    {
        std::ofstream file(schema_root / "kinds.yaml", std::ios::binary);
        file << "kinds:\n"
                "  - kind: Buildable\n"
                "    flags: [Is_Buildable]\n";
    }
    {
        std::ofstream file(schema_root / "meta" / "metafiles.yaml", std::ios::binary);
        file << "metafiles:\n"
                "  - path: data/xml/gameobjectfiles.xml\n"
                "    metaFileType: fileRegistry\n"
                "    types:\n"
                "      - GameObjectType\n";
    }
    {
        std::ofstream file(xml_root / "GameObjectFiles.xml", std::ios::binary);
        file << "<Game_Object_Files><File>Units.xml</File></Game_Object_Files>";
    }
    {
        std::ofstream file(xml_root / "Units.xml", std::ios::binary);
        file << "<Units><SpaceUnit Name=\"Unit_A\">"
                "<Is_Buildable>Maybe</Is_Buildable>"
                "<Build_Cost_Credits>-1</Build_Cost_Credits>"
                "<Faction>Hutts</Faction>"
                "<Target>Missing_Unit</Target>"
                "<Buildable_Target>Unit_B</Buildable_Target>"
                "<Inherited_Buildable_Target>Unit_Variant</Inherited_Buildable_Target>"
                "<Repeated>1</Repeated><Repeated>2</Repeated>"
                "<Unknown_Field>value</Unknown_Field>"
                "</SpaceUnit><SpaceUnit Name=\"Unit_B\" />"
                "<SpaceUnit Name=\"Unit_Base\"><Is_Buildable>Yes</Is_Buildable></SpaceUnit>"
                "<SpaceUnit Name=\"Unit_Variant\"><Variant_Of_Existing_Type>Unit_Base</Variant_Of_Existing_Type></SpaceUnit>"
                "</Units>";
    }
    {
        std::ofstream file(xml_root / "Story.xml", std::ios::binary);
        file << "<Story><Event Name=\"Story_Event\"><Event_Type>Trigger</Event_Type>"
                "<Reward_Type>Reward</Reward_Type></Event></Story>";
    }

    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", game_root));
    const auto database = eaw::XmlAssetDatabase::build(vfs);
    const auto schema = expect_ok(eaw::XmlSchema::load(root / "schema"), "XML schema load");
    const auto validation = schema.validate(database);
    require(schema.types().size() == 2 && schema.enums().size() == 1 && schema.kinds().size() == 1 && schema.metafiles().size() == 1, "XML schema loading failed");
    require(validation.definitions_considered == 5 && validation.definitions_validated == 5 && validation.metafile_type_definitions == 4 && validation.structural_type_definitions == 1 &&
                validation.untyped_definitions == 0,
            "XML schema type inference failed");
    require(validation.fields_validated == 12 && validation.unknown_fields == 1 && validation.duplicate_fields == 1, "XML schema field validation failed");
    require(validation.invalid_values == 3 && validation.unresolved_references == 1 && validation.mismatched_references == 1, "XML schema value or reference validation failed");
    std::filesystem::remove_all(root, ignored);
}

const ModuleRegistrar registrar("xml", {
                                           {"test_xml_document", &test_xml_document},
                                           {"test_xml_asset_database", &test_xml_asset_database},
                                           {"test_game_object_catalog", &test_game_object_catalog},
                                           {"test_xml_schema_validation", &test_xml_schema_validation},
                                       });

} // namespace
