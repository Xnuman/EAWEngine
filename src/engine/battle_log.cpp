#include "battle_log.hpp"
#include "core/file_io.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace eaw {
namespace {

constexpr std::string_view header = "EAWEngine battle log 1";

void append_float(std::string& out, const float value)
{
    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    EAW_ASSERT_MSG(error == std::errc{}, "a float always fits 32 characters");
    out.append(buffer.data(), end);
}

void append_id(std::string& out, const EntityId id)
{
    out += std::to_string(id.index);
    out += '.';
    out += std::to_string(id.generation);
}

std::string_view stance_name(const Stance stance) noexcept
{
    switch (stance)
    {
    case Stance::aggressive:
        return "aggressive";
    case Stance::hold_position:
        return "hold_position";
    case Stance::hold_fire:
        return "hold_fire";
    }
    return "aggressive";
}

std::vector<std::string_view> split_words(const std::string_view line)
{
    std::vector<std::string_view> words;
    std::size_t position = 0;
    while (position < line.size())
    {
        while (position < line.size() && (line[position] == ' ' || line[position] == '\t'))
        {
            ++position;
        }
        const auto start = position;
        while (position < line.size() && line[position] != ' ' && line[position] != '\t')
        {
            ++position;
        }
        if (position > start)
        {
            words.push_back(line.substr(start, position - start));
        }
    }
    return words;
}

template <class T> std::optional<T> parse_whole(const std::string_view text)
{
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

std::optional<float> parse_float(const std::string_view text)
{
    const auto value = parse_whole<float>(text);
    return value && std::isfinite(*value) ? value : std::nullopt;
}

std::optional<EntityId> parse_id(const std::string_view text)
{
    const auto dot = text.find('.');
    if (dot == std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto index = parse_whole<std::uint32_t>(text.substr(0, dot));
    const auto generation = parse_whole<std::uint32_t>(text.substr(dot + 1U));
    if (!index || !generation)
    {
        return std::nullopt;
    }
    return EntityId{*index, *generation};
}

} // namespace

std::string format_battle_log(const BattleLog& log)
{
    std::string out(header);
    out += '\n';
    if (!log.map.empty())
    {
        out += "map " + log.map + '\n';
    }
    for (const auto& entry : log.entries)
    {
        const auto& command = entry.command;
        out += std::to_string(entry.tick);
        const auto point = [&]
        {
            for (const auto value : {command.point.x, command.point.y, command.point.z})
            {
                out += ' ';
                append_float(out, value);
            }
        };
        switch (command.order)
        {
        case BattleOrder::spawn:
            out += " spawn " + command.type + ' ' + std::to_string(command.count) + ' ' + std::to_string(command.owner);
            point();
            break;
        case BattleOrder::move:
            out += " move";
            point();
            break;
        case BattleOrder::attack:
            out += " attack ";
            append_id(out, command.target);
            break;
        case BattleOrder::stop:
            out += " stop";
            break;
        case BattleOrder::set_stance:
            out += " stance ";
            out += stance_name(command.stance);
            break;
        case BattleOrder::remove:
            out += " remove";
            break;
        }
        for (const auto id : command.entities)
        {
            out += ' ';
            append_id(out, id);
        }
        out += '\n';
    }
    if (log.end_tick)
    {
        out += "end " + std::to_string(*log.end_tick);
        if (log.end_hash)
        {
            std::array<char, 17> hex{};
            const auto [end, error] = std::to_chars(hex.data(), hex.data() + hex.size(), *log.end_hash, 16);
            EAW_ASSERT_MSG(error == std::errc{}, "a 64-bit hash always fits 16 hexadecimal digits");
            out += ' ';
            out.append(hex.data(), end);
        }
        out += '\n';
    }
    return out;
}

Result<BattleLog> parse_battle_log(const std::string_view text)
{
    BattleLog log;
    std::size_t line_start = 0;
    std::size_t line_number = 0;
    bool seen_header = false;
    while (line_start < text.size())
    {
        auto line_end = text.find('\n', line_start);
        if (line_end == std::string_view::npos)
        {
            line_end = text.size();
        }
        auto line = text.substr(line_start, line_end - line_start);
        line_start = line_end + 1U;
        ++line_number;
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        const auto failure = [&](const std::string& what)
        {
            return make_error("battle log line " + std::to_string(line_number) + ": " + what);
        };
        if (!seen_header)
        {
            if (line != header)
            {
                return failure("expected '" + std::string(header) + "'");
            }
            seen_header = true;
            continue;
        }
        const auto words = split_words(line);
        if (words.empty() || words.front().starts_with('#'))
        {
            continue;
        }
        if (words.front() == "map")
        {
            // The rest of the line: mod paths may contain spaces.
            const auto path = line.substr(line.find("map") + 3U);
            const auto first = path.find_first_not_of(" \t");
            const auto last = path.find_last_not_of(" \t");
            if (first == std::string_view::npos)
            {
                return failure("map needs a virtual path");
            }
            log.map = std::string(path.substr(first, last - first + 1U));
            continue;
        }
        if (words.front() == "end")
        {
            const auto end_tick = words.size() >= 2 ? parse_whole<std::uint64_t>(words[1]) : std::nullopt;
            if (!end_tick || words.size() > 3 || log.end_tick)
            {
                return failure("end needs one tick and optionally a hexadecimal state hash, once");
            }
            if (!log.entries.empty() && *end_tick < log.entries.back().tick)
            {
                return failure("the end tick comes before the last order");
            }
            log.end_tick = end_tick;
            if (words.size() == 3)
            {
                std::uint64_t hash{};
                const auto [end, error] = std::from_chars(words[2].data(), words[2].data() + words[2].size(), hash, 16);
                if (error != std::errc{} || end != words[2].data() + words[2].size())
                {
                    return failure("invalid state hash '" + std::string(words[2]) + "'");
                }
                log.end_hash = hash;
            }
            continue;
        }
        if (log.end_tick)
        {
            return failure("orders after the end line");
        }
        const auto tick = parse_whole<std::uint64_t>(words.front());
        if (!tick)
        {
            return failure("expected a tick, 'map', or a comment");
        }
        if (!log.entries.empty() && *tick < log.entries.back().tick)
        {
            return failure("ticks must not decrease");
        }
        if (words.size() < 2)
        {
            return failure("missing order");
        }
        BattleCommand command;
        std::size_t next = 2;
        const auto read_point = [&]() -> bool
        {
            if (words.size() < next + 3U)
            {
                return false;
            }
            const auto x = parse_float(words[next]);
            const auto y = parse_float(words[next + 1U]);
            const auto z = parse_float(words[next + 2U]);
            if (!x || !y || !z)
            {
                return false;
            }
            command.point = {*x, *y, *z};
            next += 3U;
            return true;
        };
        const auto& order = words[1];
        if (order == "spawn")
        {
            command.order = BattleOrder::spawn;
            if (words.size() != 8)
            {
                return failure("spawn needs a type, count, owner, and point");
            }
            command.type = std::string(words[2]);
            const auto count = parse_whole<std::uint32_t>(words[3]);
            const auto owner = parse_whole<std::uint32_t>(words[4]);
            next = 5;
            if (!count || !owner || !read_point())
            {
                return failure("spawn has an invalid count, owner, or point");
            }
            command.count = *count;
            command.owner = *owner;
        }
        else if (order == "move")
        {
            command.order = BattleOrder::move;
            if (!read_point())
            {
                return failure("move needs a point");
            }
        }
        else if (order == "attack")
        {
            command.order = BattleOrder::attack;
            const auto target = words.size() > 2 ? parse_id(words[2]) : std::nullopt;
            if (!target)
            {
                return failure("attack needs a target id (index.generation)");
            }
            command.target = *target;
            next = 3;
        }
        else if (order == "stop")
        {
            command.order = BattleOrder::stop;
        }
        else if (order == "remove")
        {
            command.order = BattleOrder::remove;
        }
        else if (order == "stance")
        {
            command.order = BattleOrder::set_stance;
            if (words.size() < 3)
            {
                return failure("stance needs a stance");
            }
            if (words[2] == "aggressive")
            {
                command.stance = Stance::aggressive;
            }
            else if (words[2] == "hold_position")
            {
                command.stance = Stance::hold_position;
            }
            else if (words[2] == "hold_fire")
            {
                command.stance = Stance::hold_fire;
            }
            else
            {
                return failure("unknown stance '" + std::string(words[2]) + "'");
            }
            next = 3;
        }
        else
        {
            return failure("unknown order '" + std::string(order) + "'");
        }
        if (command.order != BattleOrder::spawn)
        {
            for (; next < words.size(); ++next)
            {
                const auto id = parse_id(words[next]);
                if (!id)
                {
                    return failure("invalid entity id '" + std::string(words[next]) + "'");
                }
                command.entities.push_back(*id);
            }
            if (command.entities.empty())
            {
                return failure("the order names no entities");
            }
        }
        log.entries.push_back({*tick, std::move(command)});
    }
    if (!seen_header)
    {
        return make_error("battle log is empty");
    }
    return log;
}

Status save_battle_log(const std::filesystem::path& path, const BattleLog& log)
{
    auto written = write_text_file(path, format_battle_log(log), WriteMode::replace);
    if (!written)
    {
        return with_context("battle log", std::move(written).error());
    }
    return Status::success();
}

Result<BattleLog> load_battle_log(const std::filesystem::path& path)
{
    // Order logs are small text; a larger file is not one (an engine choice).
    constexpr std::uint64_t maximum_log_bytes = 64ULL * 1024ULL * 1024ULL;
    auto file = read_text_file(path, maximum_log_bytes);
    if (!file)
    {
        return with_context("battle log", std::move(file).error());
    }
    const auto& text = *file;
    auto parsed = parse_battle_log(text);
    if (!parsed)
    {
        return with_context(path.string(), parsed.error());
    }
    return parsed;
}

} // namespace eaw
