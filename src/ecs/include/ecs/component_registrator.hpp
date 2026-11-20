#pragma once
// Registering a component's reflection (Singe's ComponentRegistrator): its type with its id as
// metadata (meta_component_id), so a component read back by type name (a snapshot) finds its id.
// Add its properties as for any type:
//
//     EAW_RTI_REGISTRATION
//     {
//         eaw::ecs::register_component<Health>().property("points", &Health::points);
//     }
#include "ecs/component.hpp"
#include "rti/registrator.hpp"

namespace eaw::ecs {

inline constexpr const char* meta_component_id = "ecs.component_id";

template <class T> struct ComponentRegistrator : rti::Registrator<T>
{
    ComponentRegistrator()
    {
        this->meta(meta_component_id, component_id<T>());
    }
};

template <class T> ComponentRegistrator<T> register_component()
{
    return ComponentRegistrator<T>();
}

} // namespace eaw::ecs
