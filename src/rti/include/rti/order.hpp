#pragma once
// Ordering types by metadata (Singe's OrderMeta and SortByOrder): a registrator that derives from
// OrderMeta lists the types its type comes before and after (by name, under the metadata names
// Policy::meta_before and Policy::meta_after); sort_by_order splits types into turns, each type in a
// turn after every type it must follow.
#include "rti/type.hpp"
#include "rti/type_name.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eaw::rti {

template <class Derived, class Policy> struct OrderMeta
{
    template <class... Types> Derived& before()
    {
        auto& self = static_cast<Derived&>(*this);
        self.meta(Policy::meta_before, std::vector<std::string>{std::string(type_name<Types>())...});
        return self;
    }

    template <class... Types> Derived& after()
    {
        auto& self = static_cast<Derived&>(*this);
        self.meta(Policy::meta_after, std::vector<std::string>{std::string(type_name<Types>())...});
        return self;
    }
};

// The turns of `types`: each type in the turn after the latest turn of the types it comes after (its
// own `after` list, and every type that lists it in `before`); types in a turn keep the order they
// are given in. A cycle is a programming error (fatal, naming the types). Types named in the lists
// but not given are ordered by but left out of the turns.
[[nodiscard]] std::vector<std::vector<Type>> sort_by_order(std::span<const Type> types, std::string_view before, std::string_view after);

template <class Policy> [[nodiscard]] std::vector<std::vector<Type>> sort_by_order(const std::span<const Type> types)
{
    return sort_by_order(types, Policy::meta_before, Policy::meta_after);
}

} // namespace eaw::rti
