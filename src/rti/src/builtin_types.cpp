// The types every reflected type builds on: the arithmetic types, strings, and rti::Type itself (by
// name, so a type written out reads back as the same type in another run).
#include "rti/registration.hpp"

#include <cstdint>
#include <string>

EAW_RTI_REGISTRATION
{
    using namespace eaw::rti;
    ensure_registered<bool, char, std::int8_t, std::uint8_t, std::int16_t, std::uint16_t, std::int32_t, std::uint32_t, std::int64_t, std::uint64_t, float, double, std::string>();
    register_type<Type>().property(
        "name",
        [](const Type& type)
        {
            return std::string(type.name());
        },
        [](Type& type, const std::string& name)
        {
            type = Type::of(name);
        });
}
