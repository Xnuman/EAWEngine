#include "test_support.hpp"

#include "launcher/child_process.hpp"
#include "launcher/sandbox_scenario.hpp"
#include "assets/spawn_catalog.hpp"

namespace {

using namespace eaw_test;

void test_scenario_arguments()
{
    eaw::SandboxScenario scenario;
    scenario.name = "Bespin skirmish";
    scenario.game_root = "C:\\Games\\EaW";
    scenario.mods = {"D:\\Mods\\First", "D:\\Mods\\Second"};
    scenario.map = "DATA/ART/MAPS/_LAND_PLANET_BESPIN_01.TED";
    scenario.spawns = {{"T2B_Tank", 6, 1U}, {"AT_ST_Walker", 4, std::nullopt}};
    scenario.show_passability = true;
    scenario.vsync = false;
    scenario.frame_limit = 300;
    scenario.upload_budget = 0U;
    scenario.extra_arguments = R"(--pick 640 360 --log-dir "C:\My Logs")";
    const auto arguments = expect_ok(eaw::sandbox_arguments(scenario), "building sandbox arguments");
    const std::vector<std::string> expected{"sandbox",
                                            "--game",
                                            "foc",
                                            "--game-root",
                                            "C:\\Games\\EaW",
                                            "--mod",
                                            "D:\\Mods\\First",
                                            "--mod",
                                            "D:\\Mods\\Second",
                                            "--map",
                                            "DATA/ART/MAPS/_LAND_PLANET_BESPIN_01.TED",
                                            "--spawn",
                                            "T2B_Tank:6@1",
                                            "--spawn",
                                            "AT_ST_Walker:4",
                                            "--show-passability",
                                            "--vsync",
                                            "off",
                                            "--frame-limit",
                                            "300",
                                            "--upload-budget",
                                            "0",
                                            "--pick",
                                            "640",
                                            "360",
                                            "--log-dir",
                                            "C:\\My Logs"};
    require(arguments == expected, "a scenario must turn into the sandbox command line");

    auto model = eaw::SandboxScenario{};
    model.scene = eaw::SandboxScene::model;
    require(!eaw::sandbox_arguments(model), "a model scenario without a model must not launch");
    model.model = "DATA/ART/MODELS/EI_DARKTROOPER_ONE.ALO";
    const auto model_arguments = expect_ok(eaw::sandbox_arguments(model), "building model arguments");
    require(model_arguments.size() == 5 && model_arguments[3] == "--model", "a model scenario must pass its model");
    auto unclosed = scenario;
    unclosed.extra_arguments = "--log-dir \"C:\\open";
    require(!eaw::sandbox_arguments(unclosed), "an unclosed quote in the extra arguments must be reported");

    require(expect_ok(eaw::parse_spawn(" AT_AT_Walker:2@6 "), "parsing a spawn") == eaw::ScenarioSpawn{"AT_AT_Walker", 2, 6U} &&
                expect_ok(eaw::parse_spawn("Snowspeeder"), "parsing a bare spawn") == eaw::ScenarioSpawn{"Snowspeeder", 1, std::nullopt} && !eaw::parse_spawn("Tank:x") && !eaw::parse_spawn(":3"),
            "spawns must parse like --spawn");
}

void test_scenario_presets()
{
    eaw::SandboxScenario first;
    first.name = "Bespin skirmish";
    first.mods = {"D:\\Mods\\First"};
    first.map = "DATA/ART/MAPS/_LAND_PLANET_BESPIN_01.TED";
    first.spawns = {{"T2B_Tank", 6, 1U}};
    first.warp = true;
    first.gpu_validation = true;
    first.width = 1280;
    first.upload_budget = 32U;
    first.extra_arguments = "--pick 10 20";
    eaw::SandboxScenario second;
    second.name = "Dark trooper";
    second.game = eaw::GameVariant::empire_at_war;
    second.scene = eaw::SandboxScene::model;
    second.model = "DATA/ART/MODELS/EI_DARKTROOPER_ONE.ALO";
    second.vsync = false;
    const std::vector<eaw::SandboxScenario> scenarios{first, second};
    const auto text = eaw::serialize_scenarios(scenarios);
    const auto parsed = expect_ok(eaw::parse_scenarios(text), "parsing saved presets");
    require(parsed.scenarios == scenarios && parsed.warnings.empty(), "presets must survive a save and load unchanged");

    // A newer build's keys and options load with warnings (options that became defaults, such as
    // no-overlay, silently); structural damage is an error.
    const auto future =
        expect_ok(eaw::parse_scenarios("# comment\n[scenario]\nname=Future\noption=no-overlay\noption=time-travel\ncolor=blue\nframe-limit=abc\nspawn=:3\n"), "parsing a newer preset file");
    require(future.scenarios.size() == 1 && future.scenarios[0].name == "Future" && future.warnings.size() == 4, "unknown preset content must become warnings");
    require(!eaw::parse_scenarios("name=Orphan\n") && !eaw::parse_scenarios("[scenario]\nnot a pair\n"), "malformed preset structure must be reported");
}

void test_command_line_quoting()
{
    // The rules CommandLineToArgvW applies: backslashes double only before a quote.
    require(eaw::quote_command_argument("plain") == "\"plain\"" && eaw::quote_command_argument("C:\\Program Files\\") == "\"C:\\Program Files\\\\\"" &&
                eaw::quote_command_argument("say \"hi\"") == "\"say \\\"hi\\\"\"" && eaw::quote_command_argument("") == "\"\"",
            "arguments must be quoted for Windows command lines");
    const std::vector<std::string> arguments{"sandbox", "--map", "A B"};
    require(eaw::build_command_line("C:\\Build\\EAWEngine.exe", arguments) == "\"C:\\Build\\EAWEngine.exe\" \"sandbox\" \"--map\" \"A B\"", "a command line must quote the program and arguments");
}

void test_spawn_catalog()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-spawn-catalog-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "Data" / "XML");
    {
        std::ofstream file(root / "Data" / "XML" / "Units.xml", std::ios::binary);
        file << "<Units>"
                "<GroundVehicle Name=\"Tank\"><Land_Model_Name>EV_Tank.alo</Land_Model_Name></GroundVehicle>"
                "<SpaceUnit Name=\"Frigate\"><Space_Model_Name>EV_Frigate.alo</Space_Model_Name></SpaceUnit>"
                "<SpaceUnit Name=\"Frigate_Variant\"><Variant_Of_Existing_Type>Frigate</Variant_Of_Existing_Type></SpaceUnit>"
                "<Props_Generic Name=\"barrel\"><Land_Model_Name>W_Barrel.alo</Land_Model_Name></Props_Generic>"
                "<GroundCompany Name=\"Company_Without_Model\" />"
                "<HeroCompany Name=\"Hero_Team\"><Company_Units>Tank, Missing</Company_Units></HeroCompany>"
                "<SFXEvent Name=\"Tank\" />"
                "</Units>";
    }
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    const auto types = eaw::list_spawn_types(vfs);
    std::filesystem::remove_all(root, ignored);
    require(types.size() == 5 && types[0].name == "barrel" && types[1].name == "Frigate" && types[2].name == "Frigate_Variant" && types[3].name == "Hero_Team" && types[4].name == "Tank",
            "spawnable types must be every named definition with a model, or a company with a member that has one, sorted case-insensitively");
    require(!types[0].unit && types[1].unit && types[1].space_model && !types[1].land_model && types[2].space_model && types[4].land_model && types[4].kind == "GroundVehicle",
            "spawnable types must record their kind and model contexts, through variants");
    require(types[3].unit && types[3].land_model && !types[3].space_model, "a company without a model must spawn where its members have models");
}

const ModuleRegistrar registrar("launcher", {
                                                {"test_scenario_arguments", &test_scenario_arguments},
                                                {"test_scenario_presets", &test_scenario_presets},
                                                {"test_command_line_quoting", &test_command_line_quoting},
                                                {"test_spawn_catalog", &test_spawn_catalog},
                                            });

} // namespace
