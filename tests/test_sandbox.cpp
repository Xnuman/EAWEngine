// The sandbox command line.
#include "test_support.hpp"

#include "battle.hpp"
#include "battle_effects.hpp"
#include "battle_log.hpp"
#include "particle_effects.hpp"
#include "projectile_visuals.hpp"
#include "scene_builder_internal.hpp"
#include "sandbox_options.hpp"
#include "unit_animation.hpp"

namespace {

using namespace eaw_test;

void test_sandbox_options()
{
    const auto parse = [](std::initializer_list<const char*> arguments)
    {
        std::vector<const char*> argv{"EAWEngine"};
        argv.insert(argv.end(), arguments.begin(), arguments.end());
        return eaw::parse_sandbox_options(static_cast<int>(argv.size()), argv.data());
    };
    const auto options = expect_ok(parse({"sandbox", "--map", "DATA/ART/MAPS/X.TED", "--spawn", "T2B_Tank:6@2", "--spawn", "AT_ST_Walker", "--vsync", "off", "--frame-limit", "10", "--overlay",
                                          "--mod", "D:/Mods/A", "--game", "eaw"}),
                                   "parsing sandbox options");
    require(options.mode == eaw::RunMode::sandbox && options.map == "DATA/ART/MAPS/X.TED" && !options.vsync && options.frame_limit == 10U && options.overlay && options.mods.size() == 1 &&
                options.variant == eaw::GameVariant::empire_at_war,
            "sandbox options must be read");
    require(options.spawns.size() == 2 && options.spawns[0].type == "T2B_Tank" && options.spawns[0].count == 6 && options.spawns[0].owner == 2U && options.spawns[1].type == "AT_ST_Walker" &&
                options.spawns[1].count == 0 && !options.spawns[1].owner,
            "--spawn must read its type, count, and owner");
    require(expect_ok(parse({"--help"}), "parsing --help").show_help && expect_ok(parse({"doctor"}), "parsing doctor").mode == eaw::RunMode::doctor && !expect_ok(parse({}), "parsing nothing").overlay,
            "the help flag, doctor mode, and the hidden statistics panel must be the defaults they claim");
    require(!parse({"--spawn", "Tank:1@64"}) && !parse({"--spawn", "Tank:1001"}) && !parse({"--spawn", ":3"}) && !parse({"--vsync", "maybe"}) && !parse({"--width", "0"}) && !parse({"--bogus"}) &&
                !parse({"--map"}) && !parse({"--frame-latency", "3"}),
            "invalid sandbox options must be rejected");
    const auto battle = expect_ok(parse({"battle", "--map", "DATA/ART/MAPS/X.TED", "--ticks", "500", "--record", "out.log", "--check-determinism", "--auto-move"}), "parsing battle options");
    require(battle.mode == eaw::RunMode::battle && battle.ticks == 500U && battle.record == std::filesystem::path("out.log") && battle.check_determinism && battle.auto_move,
            "battle options must be read");
    const auto opened = expect_ok(parse({"--panel", "spawn", "--panel", "menu"}), "parsing --panel");
    require(opened.panels.spawn && opened.panels.menu && !opened.panels.statistics && !parse({"--panel", "bogus"}), "--panel must open the named panels and reject others");
    const auto later = expect_ok(parse({"--map", "X.TED", "--spawn-later", "AT_ST_Walker:2@2"}), "parsing --spawn-later");
    require(later.spawns.empty() && later.later_spawns.size() == 1 && later.later_spawns[0].type == "AT_ST_Walker" && later.later_spawns[0].count == 2 && later.later_spawns[0].owner == 2U,
            "--spawn-later must read like --spawn into its own list");
    require(expect_ok(parse({"battle", "--replay", "in.log"}), "parsing a replay").replay == std::filesystem::path("in.log"), "a replay needs no map");
    const auto capture =
        expect_ok(parse({"--map", "X.TED", "--capture", "out/kuat.png", "--capture-frame", "90", "--fixed-frame-time", "0.05", "--camera", "-100", "250.5", "1200"}), "parsing capture options");
    require(capture.capture == std::filesystem::path("out/kuat.png") && capture.capture_frame == 90U && capture.fixed_frame_time == 0.05F && capture.camera_view &&
                (*capture.camera_view)[0] == -100.0F && (*capture.camera_view)[1] == 250.5F && (*capture.camera_view)[2] == 1200.0F,
            "capture options must be read");
    require(expect_ok(parse({"--capture", "a.png"}), "parsing --capture").capture_frame == 1U && !expect_ok(parse({}), "parsing nothing").fixed_frame_time,
            "a capture defaults to the first frame and runs only on a fixed clock it sets itself");
    require(!parse({"--capture-frame", "0"}) && !parse({"--fixed-frame-time", "0"}) && !parse({"--fixed-frame-time", "2"}) && !parse({"--fixed-frame-time", "x"}) && !parse({"--camera", "1", "2"}) &&
                !parse({"--camera", "1", "2", "0"}) && !parse({"--camera", "1", "2", "nan"}),
            "invalid capture options must be rejected");
    require(!parse({"battle"}) && !parse({"battle", "--map", "X.TED", "--ticks", "0"}) && !parse({"battle", "--replay", "in.log", "--spawn", "Tank:1"}) &&
                !parse({"battle", "--replay", "in.log", "--auto-move"}) && !parse({"sandbox", "--replay", "in.log"}),
            "invalid battle options must be rejected");
}

const ModuleRegistrar registrar("sandbox", {
                                               {"test_sandbox_options", &test_sandbox_options},
                                           });

} // namespace
