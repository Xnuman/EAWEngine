#pragma once

#include "core/status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw {

enum class LuaScriptKind
{
    source,
    lua50_bytecode,
    petroglyph_bytecode,
    unknown_binary,
};

struct LuaBytecodeHeader
{
    std::uint8_t version{};
    bool little_endian{};
    std::uint8_t integer_size{};
    std::uint8_t size_type_size{};
    std::uint8_t instruction_size{};
    std::uint8_t opcode_bits{};
    std::uint8_t argument_a_bits{};
    std::uint8_t argument_b_bits{};
    std::uint8_t argument_c_bits{};
    std::uint8_t number_size{};
};

struct LuaScriptInfo
{
    LuaScriptKind kind{LuaScriptKind::source};
    LuaBytecodeHeader bytecode_header;
    std::size_t lines{};
    std::size_t prototypes{};
    std::size_t instructions{};
    std::size_t number_constants{};
    std::size_t string_constants{};
    std::vector<std::uint64_t> persistent_function_ids;
    std::vector<std::string> dependencies;
    std::vector<std::string> defined_functions;
    std::vector<std::string> called_functions;
    std::vector<std::string> warnings;

    // Source never fails; malformed bytecode is an error naming the structure that is broken.
    [[nodiscard]] static Result<LuaScriptInfo> inspect(std::span<const std::byte> bytes);
};

[[nodiscard]] std::string_view to_string(LuaScriptKind kind) noexcept;

} // namespace eaw
