#pragma once
// A value of any type (Singe's rti::Variant): it owns its value (on the heap), or refers to a value it
// does not own (Variant::reference). Reflection hands values around as variants: property getters
// and setters, constructors, snapshots. Not for hot loops: each owned value is an allocation.
#include "core/fatal_error.hpp"

#include "rti/type.hpp"

#include <string>
#include <type_traits>
#include <utility>

namespace eaw::rti {

class Variant final
{
public:
    Variant() = default;

    // Owns a copy (or the moved value) of `value`.
    template <class T>
        requires(!std::is_same_v<std::remove_cvref_t<T>, Variant>)
    Variant(T&& value) // NOLINT(google-explicit-constructor): a variant takes any value, as Singe's
        : data_(new std::remove_cvref_t<T>(std::forward<T>(value))), operations_(&operations_of<std::remove_cvref_t<T>>())
    {}
    // Owns a T constructed in place from `arguments` (for types that cannot be moved).
    template <class T, class... Arguments> [[nodiscard]] static Variant make(Arguments&&... arguments)
    {
        Variant variant;
        variant.data_ = new T(std::forward<Arguments>(arguments)...);
        variant.operations_ = &operations_of<T>();
        return variant;
    }
    // Refers to `value` without owning it; copies of it refer to the same value.
    template <class T> [[nodiscard]] static Variant reference(T& value) noexcept
    {
        Variant variant;
        variant.data_ = const_cast<std::remove_const_t<T>*>(&value);
        variant.operations_ = &operations_of<std::remove_const_t<T>>();
        variant.reference_ = true;
        return variant;
    }

    Variant(const Variant& other);
    Variant(Variant&& other) noexcept;
    Variant& operator=(const Variant& other);
    Variant& operator=(Variant&& other) noexcept;
    ~Variant();

    explicit operator bool() const noexcept
    {
        return operations_ != nullptr;
    }
    [[nodiscard]] Type type() const;
    [[nodiscard]] bool is_reference() const noexcept
    {
        return reference_;
    }
    [[nodiscard]] void* data() noexcept
    {
        return data_;
    }
    [[nodiscard]] const void* data() const noexcept
    {
        return data_;
    }

    // The value as a T, or nullptr when it holds another type.
    template <class T> [[nodiscard]] T* get_if() noexcept
    {
        return operations_ != nullptr && type() == Type::of<T>() ? static_cast<T*>(data_) : nullptr;
    }
    template <class T> [[nodiscard]] const T* get_if() const noexcept
    {
        return operations_ != nullptr && type() == Type::of<T>() ? static_cast<const T*>(data_) : nullptr;
    }
    // The value as a T (asserts that it is one).
    template <class T> [[nodiscard]] T& get()
    {
        auto* value = get_if<T>();
        EAW_ASSERT_MSG(value != nullptr, std::string("a variant read as ") + std::string(type_name<T>()) + " holds " + std::string(type().name()));
        return *value;
    }
    template <class T> [[nodiscard]] const T& get() const
    {
        const auto* value = get_if<T>();
        EAW_ASSERT_MSG(value != nullptr, std::string("a variant read as ") + std::string(type_name<T>()) + " holds " + std::string(type().name()));
        return *value;
    }
    // Takes the owned value out: the variant is empty afterwards and the caller deletes the T.
    template <class T> [[nodiscard]] T* release()
    {
        EAW_ASSERT_MSG(!reference_, "only an owned value can be released");
        auto* value = &get<T>();
        data_ = nullptr;
        operations_ = nullptr;
        return value;
    }
    // An owned copy of the value (of a referred value too).
    [[nodiscard]] Variant copy() const;

private:
    struct Operations
    {
        Type (*type)();
        void (*destroy)(void* value);
        // A new copy of the value, or nullptr when the type cannot be copied.
        void* (*clone)(const void* value);
    };

    template <class T> static const Operations& operations_of() noexcept
    {
        static const Operations operations{
            []
            {
                return Type::of<T>();
            },
            [](void* value)
            {
                delete static_cast<T*>(value);
            },
            []([[maybe_unused]] const void* value) -> void*
            {
                if constexpr (std::is_copy_constructible_v<T>)
                {
                    return new T(*static_cast<const T*>(value));
                }
                else
                {
                    return nullptr;
                }
            },
        };
        return operations;
    }

    void clear() noexcept;

    void* data_{};
    const Operations* operations_{};
    bool reference_{};
};

} // namespace eaw::rti
