#pragma once
// What the type registry knows of a type (Singe's rti::TypeInfo): its name, size, and kind; how to
// construct it; its named properties with their metadata; its parents and children; and metadata of
// its own. Types are registered once (rti/registrator.hpp, usually in an EAW_RTI_REGISTRATION block)
// and read afterwards; the registry is a process-wide service (AGENTS.md).
#include "rti/type.hpp"
#include "rti/variant.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eaw::rti {

enum class TypeKind : std::uint8_t
{
    plain,
    arithmetic,
    // A std::vector: its elements through TypeInfo::array.
    array,
};

// A named value of a type, read and written through variants: a data member, or a getter and a
// setter. `address` gives a member's place in an instance (none for a getter and setter).
struct Property
{
    std::string name;
    Type type;
    std::function<Variant(const void* instance)> get;
    std::function<void(void* instance, Variant&& value)> set;
    std::function<void*(void* instance)> address;
    // Metadata by name (rti/meta: a value whose type names its kind).
    std::map<std::string, Variant, std::less<>> meta;

    // The metadata of kind M (registered as an M value), or nullptr.
    template <class M> [[nodiscard]] const M* meta_of() const
    {
        const auto found = meta.find(M::meta_name);
        return found != meta.end() ? found->second.template get_if<M>() : nullptr;
    }
};

// A way to construct a type from arguments of the listed types (matched by type, without
// references and cv-qualifiers).
struct Constructor
{
    std::vector<Type> arguments;
    std::function<Variant(std::span<Variant> arguments)> construct;
};

// An array type's elements.
struct ArrayOperations
{
    Type element;
    std::size_t (*size)(const void* array);
    void* (*element_at)(void* array, std::size_t index);
    void (*resize)(void* array, std::size_t size);
};

class TypeInfo final
{
public:
    [[nodiscard]] static const TypeInfo* get(Type type);
    [[nodiscard]] static const TypeInfo* get(std::string_view name);
    template <class T> [[nodiscard]] static const TypeInfo* get()
    {
        return get(Type::of<T>());
    }
    // Every registered type, in the order of their numbers.
    [[nodiscard]] static std::vector<const TypeInfo*> all();

    [[nodiscard]] Type type() const noexcept
    {
        return type_;
    }
    [[nodiscard]] std::string_view name() const noexcept
    {
        return name_;
    }
    [[nodiscard]] std::size_t size() const noexcept
    {
        return size_;
    }
    [[nodiscard]] std::size_t alignment() const noexcept
    {
        return alignment_;
    }
    [[nodiscard]] TypeKind kind() const noexcept
    {
        return kind_;
    }
    // An array's elements (nullptr for other kinds).
    [[nodiscard]] const ArrayOperations* array() const noexcept
    {
        return kind_ == TypeKind::array ? &array_ : nullptr;
    }
    [[nodiscard]] const std::vector<Property>& properties() const noexcept
    {
        return properties_;
    }
    [[nodiscard]] const Property* property(std::string_view name) const;
    [[nodiscard]] const std::vector<Type>& parents() const noexcept
    {
        return parents_;
    }
    [[nodiscard]] const std::vector<Type>& children() const noexcept
    {
        return children_;
    }
    // Whether `type` is this type or derives from it (through its registered parents).
    [[nodiscard]] bool is_base_of(const TypeInfo& type) const;
    template <class T> [[nodiscard]] bool is_base_of() const
    {
        const auto* type = get<T>();
        return type != nullptr && is_base_of(*type);
    }
    // The type's own metadata of this name, or nullptr.
    [[nodiscard]] const Variant* meta(std::string_view name) const;

    // A new value from `arguments` by the constructor that takes their types, or an empty variant
    // when there is none.
    template <class... Arguments> [[nodiscard]] Variant construct(Arguments&&... arguments) const
    {
        Variant values[sizeof...(Arguments) + 1] = {Variant::reference(arguments)...};
        return construct_from({Type::of<Arguments>()...}, std::span<Variant>(values, sizeof...(Arguments)));
    }

private:
    template <class T> friend class Registrator;

    // The registry's entry of `type`, made when it has none (with `made` set).
    [[nodiscard]] static TypeInfo& entry(Type type, bool& made);
    [[nodiscard]] static TypeInfo& entry(Type type);
    [[nodiscard]] Variant construct_from(std::initializer_list<Type> types, std::span<Variant> arguments) const;

    Type type_;
    std::string_view name_;
    std::size_t size_{};
    std::size_t alignment_{};
    TypeKind kind_{TypeKind::plain};
    ArrayOperations array_{};
    std::vector<Constructor> constructors_;
    std::vector<Property> properties_;
    std::vector<Type> parents_;
    std::vector<Type> children_;
    std::map<std::string, Variant, std::less<>> meta_;
};

} // namespace eaw::rti
