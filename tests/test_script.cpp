// The Lua script host: sandbox, recording stubs, script-owned globals, threads on ticks, require
// through the VFS, the instruction budget, bytecode refusal, deterministic random, ScriptExit,
// engine events and callbacks, and values read back from scripts.
// Only synthetic scripts written here run; stock scripts never run in tests.
#include "test_support.hpp"

#include "script/script_host.hpp"
#include "script/script_value.hpp"

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
    host->set_global("Object", eaw::ScriptValue::object("Object"));
    expect_ok(host->call("Probe"), "calling the probe");
    require(printed(*host, "true") && fixture.recorder.calls().contains("Object.Is_Valid"), "a bound stub must stand for its engine object and record its methods");
}

// The framework's event loop, in miniature: PumpEvents yields, then drains GetEvent and calls each
// event with GetEvent.Params unpacked. Events arrive in queue order with their arguments (nil and
// engine objects included), a missing handler is refused, functions handed to engine functions
// become callbacks (the same function keeps its index), and an exited script takes no events.
void test_script_events_and_callbacks()
{
    ScriptFixture fixture("events");
    fixture.write("Data/Scripts/GameObject/Listener.lua", "function PumpEvents()\n"
                                                          "  coroutine.yield(true)\n"
                                                          "  local event = GetEvent()\n"
                                                          "  while event do\n"
                                                          "    local params = GetEvent.Params()\n"
                                                          "    if params then event(unpack(params)) else event() end\n"
                                                          "    event = GetEvent()\n"
                                                          "  end\n"
                                                          "end\n"
                                                          "function On_Named(name, count, flag, missing, unit)\n"
                                                          "  print(\"named \" .. name .. \" \" .. count .. \" \" .. tostring(flag) .. \" \" .. tostring(missing) .. \" \" .. tostring(unit))\n"
                                                          "end\n"
                                                          "function On_Bare() print(\"bare \" .. tostring(GetEvent.Params())) end\n"
                                                          "function Near(self, other) print(\"near \" .. tostring(other)) end\n"
                                                          "function main()\n"
                                                          "  Object.Event_Object_In_Range(Near, 100)\n"
                                                          "  Object.Event_Object_In_Range(Near, 200)\n"
                                                          "  Object.Event_Object_In_Range(function() print(\"other\") end, 50)\n"
                                                          "  while true do PumpEvents() end\n"
                                                          "end\n"
                                                          "function Leave() _ScriptExit() end\n");
    auto host = fixture.host();
    expect_ok(host->load("Listener"), "loading the listener");
    host->set_global("Object", eaw::ScriptValue::object("Object"));
    expect_ok(host->start_thread("main"), "starting main");
    host->tick();
    require(host->callbacks().size() == 2 && host->callbacks()[0].registered_by == "Object.Event_Object_In_Range", "functions passed to engine functions must become callbacks, once each");

    const std::vector arguments{eaw::ScriptValue::from_string("alpha"), eaw::ScriptValue::from_number(3), eaw::ScriptValue::from_boolean(false), eaw::ScriptValue{},
                                eaw::ScriptValue::object("Some_Unit")};
    expect_ok(host->queue_event("On_Named", arguments), "queueing a named event");
    expect_ok(host->queue_event("On_Bare"), "queueing an event without arguments");
    expect_ok(host->queue_callback(0, {eaw::ScriptValue::object("Self"), eaw::ScriptValue::object("Intruder")}), "queueing a callback");
    require(host->queued_events() == 3, "events must wait until a thread pumps them");
    require(!host->queue_event("No_Such_Handler") && !host->queue_callback(7), "a missing handler or callback must be refused");
    host->tick();
    const auto& output = host->output();
    require(output.size() == 3 && output[0] == "named alpha 3 false nil <Some_Unit>" && output[1] == "bare nil" && output[2] == "near <Intruder>",
            "events must run in queue order with their arguments, nil and engine objects included");
    require(host->queued_events() == 0 && host->errors().empty(), "the queue must be drained without errors");

    expect_ok(host->queue_event("Leave"), "queueing the exit");
    host->tick();
    require(host->exited(), "an event may end the script");
    expect_ok(host->queue_event("On_Bare"), "an exited script ignores events");
    require(host->queued_events() == 0, "an exited script must take no events");
}

// Create_Thread is also a table: Kill ends another thread at once and the running thread at its next
// yield, and Is_Thread_Active reports both. GameRandom draws integers in its inclusive range and
// floats below 1 from the host's seeded stream.
void test_script_thread_control_and_game_random()
{
    ScriptFixture fixture("control");
    fixture.write("Data/Scripts/GameObject/Control.lua", "function Waiter() while true do coroutine.yield(true) end end\n"
                                                         "function Self_Killer() Create_Thread.Kill(GetThreadID()) print(\"still running\") coroutine.yield(true) print(\"never\") end\n"
                                                         "function Controller()\n"
                                                         "  local waiter = Create_Thread(\"Waiter\")\n"
                                                         "  coroutine.yield(true)\n"
                                                         "  print(\"active \" .. tostring(Create_Thread.Is_Thread_Active(waiter)))\n"
                                                         "  Create_Thread.Kill(waiter)\n"
                                                         "  print(\"after kill \" .. tostring(Create_Thread.Is_Thread_Active(waiter)))\n"
                                                         "end\n"
                                                         "function Rolls()\n"
                                                         "  local low, high, fraction = 10, 0, true\n"
                                                         "  for i = 1, 200 do\n"
                                                         "    local value = GameRandom(3, 5)\n"
                                                         "    if value < low then low = value end\n"
                                                         "    if value > high then high = value end\n"
                                                         "    local float = GameRandom.Get_Float()\n"
                                                         "    fraction = fraction and float >= 0 and float < 1\n"
                                                         "  end\n"
                                                         "  print(\"range \" .. low .. \" \" .. high .. \" \" .. tostring(fraction) .. \" \" .. GameRandom.Free_Random(7, 7))\n"
                                                         "end\n");
    auto host = fixture.host();
    expect_ok(host->load("Control"), "loading the controller");
    expect_ok(host->start_thread("Controller"), "starting the controller");
    expect_ok(host->start_thread("Self_Killer"), "starting the self killer");
    for (int tick = 0; tick < 4; ++tick)
    {
        host->tick();
    }
    require(printed(*host, "active true") && printed(*host, "after kill false"), "Is_Thread_Active must see a thread until Kill ends it");
    require(printed(*host, "still running") && !printed(*host, "never"), "a thread that kills itself must end at its next yield");
    require(host->live_threads() == 0 && host->errors().empty(), "killed and finished threads must be gone without errors");
    expect_ok(host->call("Rolls"), "rolling");
    require(printed(*host, "range 3 5 true 7"), "GameRandom must cover its inclusive range and Get_Float stay below 1");
    require(fixture.recorder.calls().at("Create_Thread.Kill").implemented && fixture.recorder.calls().at("GameRandom").implemented, "the primitives must be marked as host functions");
}

// Definitions the engine reads back: nested tables in a deterministic order, engine objects by
// name, functions as functions; a table that refers to itself is refused, not followed forever.
void test_script_globals_read_back()
{
    ScriptFixture fixture("globals");
    fixture.write("Data/Scripts/GameObject/Plan.lua", "TaskForce = { { \"MainForce\", \"MinimumTotalSize = 2\" }, { \"Escort\" } }\n"
                                                      "Settings = { zeta = 1, alpha = true, [2] = \"two\", [1] = \"one\", handler = function() end, owner = Find_Player(\"Empire\") }\n"
                                                      "Loop = {} Loop.self = Loop\n");
    auto host = fixture.host();
    expect_ok(host->load("Plan"), "loading the plan");
    const auto task_forces = expect_ok(host->global("TaskForce"), "reading TaskForce");
    require(task_forces.kind == eaw::ScriptValue::Kind::table && task_forces.fields.size() == 2 && task_forces.element(1)->element(1)->text == "MainForce" &&
                task_forces.element(2)->element(1)->text == "Escort",
            "nested definition tables must read back in order");
    const auto settings = expect_ok(host->global("Settings"), "reading Settings");
    std::vector<std::string> keys;
    for (const auto& field : settings.fields)
    {
        keys.push_back(field.key.kind == eaw::ScriptValue::Kind::number ? std::to_string(static_cast<int>(field.key.number)) : field.key.text);
    }
    require(keys == std::vector<std::string>{"1", "2", "alpha", "handler", "owner", "zeta"}, "keys must sort as numbers, then strings");
    require(settings.field("handler")->kind == eaw::ScriptValue::Kind::function && settings.field("owner")->kind == eaw::ScriptValue::Kind::object && settings.field("owner")->text == "Find_Player" &&
                settings.field("alpha")->boolean,
            "functions and engine objects must read back as such");
    require(expect_ok(host->global("Undefined_Global"), "reading an unset global").kind == eaw::ScriptValue::Kind::nil, "an unset global must read as nil");
    const auto loop = host->global("Loop");
    require(!loop && loop.error().message.find("nested") != std::string::npos, "a table that refers to itself must be refused");
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
                                              {"test_script_events_and_callbacks", &test_script_events_and_callbacks},
                                              {"test_script_globals_read_back", &test_script_globals_read_back},
                                              {"test_script_thread_control_and_game_random", &test_script_thread_control_and_game_random},
                                              {"test_script_owned_globals", &test_script_owned_globals},
                                          });

} // namespace
