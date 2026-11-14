#pragma once

// A system's view of the world's data (docs/ecs-design.md, 6.3 and migration step 4; the idea of
// Singe's access modifiers): a system declares what it reads and writes as the type of its view,
// SystemView<Read<A>, Write<B>, ...>, and reaches the data only through it. get<A>() gives a const
// reference for a Read, a mutable one for a Write; asking for anything undeclared does not compile.
// The declaration is the access, so it cannot drift from the code.

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>

namespace eaw {

template <class T> struct Read
{
    using Type = T;
    static constexpr bool writes = false;
};

template <class T> struct Write
{
    using Type = T;
    static constexpr bool writes = true;
};

template <class... Accesses> class SystemView final
{
    template <class Access> using Pointer = std::conditional_t<Access::writes, typename Access::Type*, const typename Access::Type*>;

public:
    using AccessList = std::tuple<Accesses...>;

    explicit SystemView(Pointer<Accesses>... data) noexcept : data_{data...}
    {}

    template <class T> [[nodiscard]] auto& get() const noexcept
    {
        static_assert(index_of<T>() < sizeof...(Accesses), "the system's view does not declare this data (once) as Read or Write");
        return *std::get<index_of<T>()>(data_);
    }

private:
    template <class T> static constexpr std::size_t index_of() noexcept
    {
        constexpr std::array<bool, sizeof...(Accesses)> matches{std::is_same_v<typename Accesses::Type, T>...};
        std::size_t found = sizeof...(Accesses);
        std::size_t count = 0;
        for (std::size_t index = 0; index < matches.size(); ++index)
        {
            if (matches[index])
            {
                found = index;
                ++count;
            }
        }
        return count == 1 ? found : sizeof...(Accesses);
    }
    static_assert(sizeof...(Accesses) > 0, "a system view declares what it reads and writes");

    std::tuple<Pointer<Accesses>...> data_;
};

} // namespace eaw
