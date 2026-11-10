#pragma once

// A value crossing between the engine and a script: event arguments the engine passes in, and
// definitions the engine reads back out of a script's globals (an AI plan's TaskForce table, a story
// script's StoryModeEvents). Engine objects are named stand-ins for now: a script sees a recording
// stub of that name until the engine provides object handles.

#include <string>
#include <string_view>
#include <vector>

namespace eaw {

struct ScriptField;

struct ScriptValue
{
    enum class Kind
    {
        nil,
        boolean,
        number,
        string,
        // An engine object; `text` names it (the stub a script sees).
        object,
        // A script function (read back only; the engine cannot pass one in).
        function,
        table,
    };

    Kind kind{Kind::nil};
    bool boolean{};
    double number{};
    // The string, or the engine object's name.
    std::string text;
    // A table's entries: number keys ascending, then string keys in byte order, then other keys in
    // the order the table yields them.
    std::vector<ScriptField> fields;

    [[nodiscard]] static ScriptValue from_boolean(bool value);
    [[nodiscard]] static ScriptValue from_number(double value);
    [[nodiscard]] static ScriptValue from_string(std::string_view value);
    [[nodiscard]] static ScriptValue object(std::string_view name);

    // The value at a string key of a table, or nullptr.
    [[nodiscard]] const ScriptValue* field(std::string_view key) const;
    // The value at a number key of a table (Lua arrays start at 1), or nullptr.
    [[nodiscard]] const ScriptValue* element(double index) const;
};

struct ScriptField
{
    ScriptValue key;
    ScriptValue value;
};

} // namespace eaw
