#pragma once
// A system (Singe's ecs::ISystem): what the world runs every tick, in turns ordered by each system's
// before and after lists (rti/order.hpp). Write systems by deriving from System<T> (ecs/system.hpp).
#include "rti/type.hpp"

namespace eaw::ecs {

class World;

class ISystem
{
public:
    // Metadata names: the systems a system runs before and after, and the callback that adds it to a
    // world (World::add_system by reflected type).
    static constexpr const char* meta_before = "ecs.system.before";
    static constexpr const char* meta_after = "ecs.system.after";
    static constexpr const char* meta_add = "ecs.system.add";
    using AddCallback = void (*)(World& world);

    explicit ISystem(World& world) noexcept : world_(world)
    {}
    virtual ~ISystem() = default;
    ISystem(const ISystem&) = delete;
    ISystem& operator=(const ISystem&) = delete;

    [[nodiscard]] virtual rti::Type type() const = 0;

protected:
    friend class World;
    virtual void execute(float dt) = 0;

    World& world_;
};

} // namespace eaw::ecs
