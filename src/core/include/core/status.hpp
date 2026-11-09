#pragma once

// Return values for recoverable failures (malformed or truncated game and mod data, missing assets,
// unavailable devices). The engine does not use exceptions: a function that can fail returns a
// Status or a Result<T> carrying a contextual message (virtual path, section, offset), and callers
// decide whether the failure is a warning (logged, work continues) or fatal (eaw::fatal_error).
// Programming errors are not statuses; they are EAW_ASSERTs.

#include "core/fatal_error.hpp"

#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace eaw {

// A failure description, readable on its own (include what was being done and where).
struct Error
{
    std::string message;
};

[[nodiscard]] inline Error make_error(std::string message)
{
    return Error{std::move(message)};
}

// Prefixes context to an error as it propagates ("reading DATA/ART/MODELS/X.ALO: ").
[[nodiscard]] inline Error with_context(std::string_view context, Error error)
{
    error.message = std::string(context) + ": " + error.message;
    return error;
}

// Success, or an Error.
class [[nodiscard]] Status final
{
public:
    Status() = default;
    Status(Error error) : error_(std::move(error))
    {}

    [[nodiscard]] static Status success() noexcept
    {
        return {};
    }
    [[nodiscard]] bool ok() const noexcept
    {
        return !error_.has_value();
    }
    explicit operator bool() const noexcept
    {
        return ok();
    }
    [[nodiscard]] const Error& error() const& noexcept
    {
        EAW_ASSERT_MSG(error_.has_value(), "error() called on a successful Status");
        return *error_;
    }
    [[nodiscard]] Error&& error() && noexcept
    {
        EAW_ASSERT_MSG(error_.has_value(), "error() called on a successful Status");
        return std::move(*error_);
    }

private:
    std::optional<Error> error_;
};

// A value, or an Error. Reading the value of a failed Result is a programming error (fatal).
template <class T> class [[nodiscard]] Result final
{
    static_assert(!std::is_same_v<std::remove_cvref_t<T>, Error>, "Result<Error> is ambiguous");

public:
    Result(T value) : storage_(std::in_place_index<0>, std::move(value))
    {}
    Result(Error error) : storage_(std::in_place_index<1>, std::move(error))
    {}

    [[nodiscard]] bool ok() const noexcept
    {
        return storage_.index() == 0;
    }
    explicit operator bool() const noexcept
    {
        return ok();
    }

    [[nodiscard]] T& value() & noexcept
    {
        EAW_ASSERT_MSG(ok(), failure_message());
        return *std::get_if<0>(&storage_);
    }
    [[nodiscard]] const T& value() const& noexcept
    {
        EAW_ASSERT_MSG(ok(), failure_message());
        return *std::get_if<0>(&storage_);
    }
    [[nodiscard]] T&& value() && noexcept
    {
        EAW_ASSERT_MSG(ok(), failure_message());
        return std::move(*std::get_if<0>(&storage_));
    }
    T* operator->() noexcept
    {
        return &value();
    }
    const T* operator->() const noexcept
    {
        return &value();
    }
    T& operator*() & noexcept
    {
        return value();
    }
    const T& operator*() const& noexcept
    {
        return value();
    }

    [[nodiscard]] const Error& error() const& noexcept
    {
        EAW_ASSERT_MSG(!ok(), "error() called on a successful Result");
        return *std::get_if<1>(&storage_);
    }
    [[nodiscard]] Error&& error() && noexcept
    {
        EAW_ASSERT_MSG(!ok(), "error() called on a successful Result");
        return std::move(*std::get_if<1>(&storage_));
    }

    [[nodiscard]] T value_or(T fallback) const&
    {
        return ok() ? *std::get_if<0>(&storage_) : std::move(fallback);
    }

private:
    [[nodiscard]] std::string failure_message() const
    {
        return "value() called on a failed Result: " + std::get_if<1>(&storage_)->message;
    }

    std::variant<T, Error> storage_;
};

} // namespace eaw

// Propagation helpers for functions returning Status or Result<U>.
//   EAW_TRY(status_expression);                  returns the error when the status failed
//   EAW_TRY_ASSIGN(auto name, result_expression); declares `name` from a successful Result
#define EAW_CONCAT_INNER(a, b) a##b
#define EAW_CONCAT(a, b) EAW_CONCAT_INNER(a, b)

#define EAW_TRY(expression)                                                                                                                                                                            \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        auto eaw_try_status_ = (expression);                                                                                                                                                           \
        if (!eaw_try_status_)                                                                                                                                                                          \
        {                                                                                                                                                                                              \
            return std::move(eaw_try_status_).error();                                                                                                                                                 \
        }                                                                                                                                                                                              \
    } while (false)

#define EAW_TRY_ASSIGN_IMPL(temporary, declaration, expression)                                                                                                                                        \
    auto temporary = (expression);                                                                                                                                                                     \
    if (!temporary)                                                                                                                                                                                    \
    {                                                                                                                                                                                                  \
        return std::move(temporary).error();                                                                                                                                                           \
    }                                                                                                                                                                                                  \
    declaration = std::move(temporary).value()

#define EAW_TRY_ASSIGN(declaration, expression) EAW_TRY_ASSIGN_IMPL(EAW_CONCAT(eaw_try_result_, __LINE__), declaration, expression)
