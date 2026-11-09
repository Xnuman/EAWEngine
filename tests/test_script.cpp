// The Lua script host: sandbox, recording stubs, script-owned globals, threads on ticks, require
// through the VFS, the instruction budget, bytecode refusal, deterministic random, and ScriptExit.
// Only synthetic scripts written here run; stock scripts never run in tests.
#include "test_support.hpp"

#include "eaw/script_host.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <set>

namespace {

using namespace eaw_test;

struct ScriptFixture
{
    std::filesystem::path root;
    eaw::VirtualFileSystem vfs;
    eaw::HostApiRecorder recorder;

    explicit ScriptFixture(const std::string_view name) : root(std::filesystem::temp_directory_path() / ("eawengine-script-" + std::string(name)))
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::filesystem::create_directories(root / "Data" / "Scripts" / "Library");
        std::filesystem::create_directories(root / "Data" / "Scripts" / "GameObject");
    }
    ~ScriptFixture()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    ScriptFixture(const ScriptFixture&) = delete;
    ScriptFixture& operator=(const ScriptFixture&) = delete;

    void write(const std::string& relative, const std::string_view text)
    {
        std::ofstream file(root / relative, std::ios::binary);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    std::unique_ptr<eaw::ScriptHost> host(const std::uint64_t seed = 1, const std::uint64_t budget = 200000)
    {
        vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
        return expect_ok(eaw::ScriptHost::create(vfs, recorder, {seed, 1.0 / 30.0, budget}), "creating a script host");
    }
};

bool printed(const eaw::ScriptHost& host, const std::string_view line)
{
    return std::ranges::find(host.output(), line) != host.output().end();
}

void test_script_sandbox_and_stubs()
{
    ScriptFixture fixture("stubs");
    fixture.write("Data/Scripts/GameObject/Probe.lua", "print(tostring(io == nil and os == nil and debug == nil and dofile == nil and loadfile == nil))\n"
                                                       "if not MyTable then MyTable = {} end\n"
                                                       "print(tostring(type(MyTable)))\n"
                                                       "local player = Find_Player(\"Rebel\")\n"
                                                       "local name = player.Get_Faction_Name()\n"
                                                       "Some_Command(3, \"text\", player)\n"
                                                       "print(\"count \" .. (player + 1))\n"
                                                       "for key, value in pairs(Find_All_Objects_Of_Type(\"X\")) do print(\"never\") end\n"
                                                       "local roll = GameRandom.Get_Float()\n");
    auto host = fixture.host();
    expect_ok(host->load("Probe"), "loading the probe");
    require(printed(*host, "true"), "io, os, debug, dofile, and loadfile must be absent");
    require(printed(*host, "table"), "a script's own global must start as nil, never as a stub");
    require(printed(*host, "count 0") && !printed(*host, "never"), "stub objects must give 0 in arithmetic and iterate as empty");
    const auto& calls = fixture.recorder.calls();
    require(calls.contains("Find_Player") && calls.at("Find_Player").signatures.contains("string") && !calls.at("Find_Player").implemented, "a host function call must be recorded");
    require(calls.contains("Find_Player().Get_Faction_Name") && calls.contains("Some_Command") && calls.at("Some_Command").signatures.contains("number, string, object(Find_Player)"),
            "methods of stub objects and argument types must be recorded");
    require(!calls.contains("MyTable") && calls.at("Find_Player").scripts.contains("DATA/SCRIPTS/GAMEOBJECT/PROBE.LUA"), "calls must be filed under their script");
    require(calls.contains("GameRandom.Get_Float"), "engine tables must be indexable stubs whose members record");
    require(host->errors().empty(), "the probe must run without errors");
}

// The framework's thread protocol, in miniature: Sleep waits in Lua by yielding through PumpEvents
// until GetCurrentTime has advanced, threads are serviced every ServiceRate seconds, yielding false
// ends a thread, _ScriptExit ends the script at the next yield, and ThreadValue and GetThreadID are
// per thread.
void test_script_threads_and_require()
{
    ScriptFixture fixture("threads");
    fixture.write("Data/Scripts/Library/MiniBase.lua", "function PumpEvents() coroutine.yield(true) end\n"
                                                       "function Sleep(time)\n"
                                                       "  ThreadValue.Set(\"StartTime\", GetCurrentTime())\n"
                                                       "  while GetCurrentTime() - ThreadValue(\"StartTime\") < time do PumpEvents() end\n"
                                                       "end\n"
                                                       "function ScriptExit() _ScriptExit() if GetThreadID() >= 0 then coroutine.yield(false) end end\n");
    fixture.write("Data/Scripts/GameObject/Worker.lua", "require(\"MiniBase\")\n"
                                                        "function Base_Definitions() ServiceRate = 0.2 print(\"outside \" .. GetThreadID()) end\n"
                                                        "function Worker(n)\n"
                                                        "  print(\"start \" .. n .. \" thread \" .. GetThreadID())\n"
                                                        "  Sleep(1)\n"
                                                        "  print(\"woke at \" .. GetCurrentTime())\n"
                                                        "  coroutine.yield(false)\n"
                                                        "  print(\"never\")\n"
                                                        "end\n"
                                                        "function Leaver() Sleep(3) ScriptExit() print(\"after exit\") end\n"
                                                        "function main() Create_Thread(\"Worker\", 5) Create_Thread(\"Leaver\") end\n");
    auto host = fixture.host();
    expect_ok(host->load("Worker"), "loading the worker");
    expect_ok(host->call("Base_Definitions"), "calling Base_Definitions");
    require(printed(*host, "outside -1"), "GetThreadID must be -1 outside threads");
    expect_ok(host->start_thread("main"), "starting main");
    host->tick(); // main runs once and spawns its threads for the next tick
    host->tick();
    require(printed(*host, "start 5 thread 2") && host->live_threads() == 2, "a spawned thread must get its first arguments and its own id");
    // Serviced every 6 ticks (0.2 s at 30 Hz): Sleep(1) from tick 2 ends at the first service at or
    // after 1 s later, tick 32.
    for (int tick = 2; tick < 31; ++tick)
    {
        host->tick();
    }
    require(!printed(*host, "woke at 1.0666666666667"), "Sleep must wait in game time");
    host->tick();
    require(printed(*host, "woke at 1.0666666666667") && !printed(*host, "never") && host->live_threads() == 1, "yielding false must end a thread");
    for (int tick = 0; tick < 70 && !host->exited(); ++tick)
    {
        host->tick();
    }
    require(host->exited() && host->live_threads() == 0 && !printed(*host, "after exit") && host->errors().empty(), "_ScriptExit must end the script at the next yield");
    require(fixture.recorder.calls().at("Create_Thread").implemented && fixture.recorder.calls().at("_ScriptExit").implemented, "host primitives must be marked");
}

void test_script_budget_bytecode_random_exit()
{
    ScriptFixture fixture("limits");
    fixture.write("Data/Scripts/GameObject/Spin.lua", "function Spin() while true do end end\n"
                                                      "function Spin_Thread() local x = 0 while true do x = x + 1 end end\n"
                                                      "function Steady() while true do coroutine.yield(true) end end\n"
                                                      "function main() Create_Thread(\"Spin_Thread\") Create_Thread(\"Steady\") end\n");
    fixture.write("Data/Scripts/GameObject/Compiled.lua", "\x1bLuaP fake bytecode");
    fixture.write("Data/Scripts/GameObject/Dice.lua", "for i = 1, 3 do print(tostring(math.random(1, 1000))) end\n");
    {
        auto host = fixture.host();
        expect_ok(host->load("Spin"), "loading the spinner");
        require(!host->call("Spin") && host->errors().back().message.find("instruction budget") != std::string::npos, "a runaway call must stop at the budget");
        expect_ok(host->start_thread("main"), "starting main");
        host->tick();
        host->tick();
        require(host->errors().back().context == "thread Spin_Thread" && host->errors().back().message.find("instruction budget") != std::string::npos,
                "a runaway thread must stop at the budget without stopping the host");
        require(host->live_threads() == 1, "the host must keep running its other threads");
    }
    {
        auto host = fixture.host();
        const auto compiled = host->load("Compiled");
        require(!compiled && compiled.error().message.find("compiled") != std::string::npos, "compiled chunks must be refused");
    }
    const auto dice = [&](const std::uint64_t seed)
    {
        ScriptFixture rolls("dice-" + std::to_string(seed));
        rolls.write("Data/Scripts/GameObject/Dice.lua", "for i = 1, 3 do print(tostring(math.random(1, 1000))) end\n");
        auto host = rolls.host(seed);
        expect_ok(host->load("Dice"), "loading the dice");
        return host->output();
    };
    require(dice(5) == dice(5) && dice(5) != dice(6), "math.random must follow the host's seed");
}

// A name defined by any script is a script name even where its file is not loaded, and a bound stub
// stands for an engine object until the engine provides it.
void test_script_shared_names_and_bound_stubs()
{
    ScriptFixture fixture("shared");
    fixture.write("Data/Scripts/Library/Story.lua", "function Story_Init() end\n");
    fixture.write("Data/Scripts/GameObject/Unit.lua", "if Story_Init then print(\"story\") else print(\"no story\") end\n"
                                                      "function Probe() print(tostring(Object.Is_Valid() ~= nil)) end\n");
    fixture.vfs.mount(std::make_unique<eaw::DirectoryMount>("test", fixture.root));
    eaw::ScriptHostOptions options;
    options.script_globals = std::make_shared<const std::set<std::string>>(eaw::script_defined_globals(fixture.vfs));
    auto host = expect_ok(eaw::ScriptHost::create(fixture.vfs, fixture.recorder, options), "creating a script host");
    expect_ok(host->load("Unit"), "loading the unit");
    require(options.script_globals->contains("Story_Init") && printed(*host, "no story"), "a name another script defines must read nil when its file is not loaded");
    host->bind_stub("Object");
    expect_ok(host->call("Probe"), "calling the probe");
    require(printed(*host, "true") && fixture.recorder.calls().contains("Object.Is_Valid"), "a bound stub must stand for its engine object and record its methods");
}

void test_script_owned_globals()
{
    const auto names = eaw::script_owned_globals("-- Commented = 1\n"
                                                 "local text = \"Quoted = 2\"\n"
                                                 "--[[ Block = 3 ]]\n"
                                                 "Assigned = 4\n"
                                                 "if Compared == 5 then end\n"
                                                 "function Defined() end\n"
                                                 "function Table.Method() end\n"
                                                 "Table.Field = 6\n");
    require(names == std::vector<std::string>{"Assigned", "Defined", "Table", "text"}, "owned globals must skip comments, strings, comparisons, and fields");
}

const ModuleRegistrar registrar("script", {
                                              {"test_script_sandbox_and_stubs", &test_script_sandbox_and_stubs},
                                              {"test_script_threads_and_require", &test_script_threads_and_require},
                                              {"test_script_budget_bytecode_random_exit", &test_script_budget_bytecode_random_exit},
                                              {"test_script_shared_names_and_bound_stubs", &test_script_shared_names_and_bound_stubs},
                                              {"test_script_owned_globals", &test_script_owned_globals},
                                          });

} // namespace
