#pragma once
// Registering a singleton type (Singe's SingletonRegistrator): with a callback that adds it to a
// world, so World::add_singleton (and a snapshot) can add it by reflected type. A singleton is a
// plain value the world holds once (World::singleton<T>()); unlike Singe's, it needs no base class.
#include "ecs/world.hpp"
#include "rti/registrator.hpp"

namespace eaw::ecs {

// Metadata name: the callback that adds a singleton type to a world.
inline constexpr const char* meta_singleton_add = "ecs.singleton.add";
using SingletonAddCallback = void (*)(World& world);

template <class T> struct SingletonRegistrator : rti::Registrator<T>
{
    SingletonRegistrator()
    {
        this->meta(meta_singleton_add, SingletonAddCallback(&add));
    }

private:
    static void add(World& world)
    {
        world.template add_singletons<T>();
    }
};

template <class T> SingletonRegistrator<T> register_singleton()
{
    return SingletonRegistrator<T>();
}

} // namespace eaw::ecs
