#pragma once
// Writing a system (Singe's System, ViewWalker, and SystemRegistrator): derive from System<T>, list
// the views it uses in `Views`, and register it with its order:
//
//     class MovementSystem final : public eaw::ecs::System<MovementSystem>
//     {
//     public:
//         using Moving = eaw::ecs::IterView<eaw::ecs::RWAccess<Transform>, eaw::ecs::ROAccess<Velocity>>;
//         using Views = std::tuple<Moving>;
//         using System::System;
//
//     protected:
//         void execute(float dt) override
//         {
//             for (auto [entity, transform, velocity] : view<Moving>()) { ... }
//         }
//     };
//
//     EAW_RTI_REGISTRATION
//     {
//         eaw::ecs::register_system<MovementSystem>().after<InputSystem>();
//     }
//
// The views are made when the system is, so they exist (and bind archetypes) before its first tick.
#include "ecs/isystem.hpp"
#include "ecs/world.hpp"
#include "rti/order.hpp"
#include "rti/registrator.hpp"

#include <memory>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace eaw::ecs {

// A system's views, made from the world when the system is.
template <class T> class ViewWalker
{
public:
    explicit ViewWalker(World& world) noexcept : walker_world_(world)
    {}

    template <class View> [[nodiscard]] View& view() noexcept
    {
        return *std::get<View*>(*std::static_pointer_cast<typename Pointers<typename T::Views>::Type>(views_));
    }
    [[nodiscard]] World& world() noexcept
    {
        return walker_world_;
    }

protected:
    // Makes the views (System's constructor calls it, where T is complete).
    void make_views()
    {
        views_ = std::make_shared<typename Pointers<typename T::Views>::Type>(Pointers<typename T::Views>::make(walker_world_));
    }

private:
    template <class Tuple> struct Pointers;
    template <class... Views> struct Pointers<std::tuple<Views...>>
    {
        using Type = std::tuple<Views*...>;
        static Type make([[maybe_unused]] World& world)
        {
            return Type(&world.template view<Views>()...);
        }
    };

    World& walker_world_;
    std::shared_ptr<void> views_;
};

template <class T> class System : public ISystem, public ViewWalker<T>
{
public:
    explicit System(World& world) : ISystem(world), ViewWalker<T>(world)
    {
        this->make_views();
    }

    [[nodiscard]] rti::Type type() const final
    {
        return rti::Type::of<T>();
    }
};

template <class T> struct SystemRegistrator : rti::Registrator<T>, rti::OrderMeta<SystemRegistrator<T>, ISystem>
{
    static_assert(std::is_base_of_v<ISystem, T>, "a system derives from ISystem");

    SystemRegistrator()
    {
        this->template constructor<World&>();
        this->template parents<ISystem>();
        this->meta(ISystem::meta_add, ISystem::AddCallback(&add));
        this->template before<>();
        this->template after<>();
    }

    template <class Value> SystemRegistrator& meta(const std::string_view name, Value&& value)
    {
        rti::Registrator<T>::meta(name, std::forward<Value>(value));
        return *this;
    }

private:
    static void add(World& world)
    {
        world.template add_systems<T>();
    }
};

template <class T> SystemRegistrator<T> register_system()
{
    return SystemRegistrator<T>();
}

} // namespace eaw::ecs
