#include "rti/order.hpp"

#include "core/fatal_error.hpp"

#include "rti/type_info.hpp"

#include <algorithm>
#include <cstdint>

namespace eaw::rti {

namespace {

constexpr std::uint8_t unvisited = 0;
constexpr std::uint8_t visiting = 1;
constexpr std::uint8_t done = 2;

// The type's list of names under `meta` (none when it has none).
const std::vector<std::string>* names_under(const Type type, const std::string_view meta)
{
    const auto* info = TypeInfo::get(type);
    const auto* list = info != nullptr ? info->meta(meta) : nullptr;
    return list != nullptr ? list->get_if<std::vector<std::string>>() : nullptr;
}

} // namespace

std::vector<std::vector<Type>> sort_by_order(const std::span<const Type> types, const std::string_view before, const std::string_view after)
{
    // Every type taking part: those given, then those their lists name, in first-seen order.
    std::vector<Type> nodes(types.begin(), types.end());
    const auto index_of = [&](const Type type)
    {
        const auto found = std::ranges::find(nodes, type);
        if (found != nodes.end())
        {
            return static_cast<std::size_t>(found - nodes.begin());
        }
        nodes.push_back(type);
        return nodes.size() - 1;
    };
    // follows[i]: the nodes node i comes after.
    std::vector<std::vector<std::size_t>> follows(nodes.size());
    for (std::size_t node = 0; node < nodes.size(); ++node)
    {
        if (const auto* list = names_under(nodes[node], after))
        {
            for (const auto& name : *list)
            {
                const auto other = index_of(Type::of(name));
                follows.resize(nodes.size());
                follows[node].push_back(other);
            }
        }
        if (const auto* list = names_under(nodes[node], before))
        {
            for (const auto& name : *list)
            {
                const auto other = index_of(Type::of(name));
                follows.resize(nodes.size());
                follows[other].push_back(node);
            }
        }
    }
    std::vector<std::uint8_t> state(nodes.size(), unvisited);
    std::vector<std::size_t> turn(nodes.size(), 0);
    // Depth first, iterative: a node's turn is one after the latest of those it follows.
    const auto resolve = [&](const std::size_t start)
    {
        std::vector<std::pair<std::size_t, std::size_t>> stack{{start, 0}};
        state[start] = visiting;
        while (!stack.empty())
        {
            auto& [node, next] = stack.back();
            if (next < follows[node].size())
            {
                const auto other = follows[node][next++];
                if (state[other] == visiting)
                {
                    fatal_error("a cycle in the order of " + std::string(nodes[node].name()) + " and " + std::string(nodes[other].name()));
                }
                if (state[other] == unvisited)
                {
                    state[other] = visiting;
                    stack.emplace_back(other, 0);
                }
                continue;
            }
            for (const auto other : follows[node])
            {
                turn[node] = std::max(turn[node], turn[other] + 1);
            }
            state[node] = done;
            stack.pop_back();
        }
    };
    for (std::size_t node = 0; node < nodes.size(); ++node)
    {
        if (state[node] == unvisited)
        {
            resolve(node);
        }
    }
    std::vector<std::vector<Type>> turns;
    for (std::size_t node = 0; node < types.size(); ++node)
    {
        if (turn[node] >= turns.size())
        {
            turns.resize(turn[node] + 1);
        }
        turns[turn[node]].push_back(nodes[node]);
    }
    std::erase_if(turns,
                  [](const std::vector<Type>& list)
                  {
                      return list.empty();
                  });
    return turns;
}

} // namespace eaw::rti
