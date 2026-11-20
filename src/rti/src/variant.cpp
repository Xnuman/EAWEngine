#include "rti/variant.hpp"

namespace eaw::rti {

Variant::Variant(const Variant& other) : data_(other.data_), operations_(other.operations_), reference_(other.reference_)
{
    if (operations_ != nullptr && !reference_)
    {
        data_ = operations_->clone(other.data_);
        EAW_ASSERT_MSG(data_ != nullptr, std::string("a variant of ") + std::string(type().name()) + " cannot be copied");
    }
}

Variant::Variant(Variant&& other) noexcept : data_(std::exchange(other.data_, nullptr)), operations_(std::exchange(other.operations_, nullptr)), reference_(std::exchange(other.reference_, false))
{}

Variant& Variant::operator=(const Variant& other)
{
    if (this != &other)
    {
        *this = Variant(other);
    }
    return *this;
}

Variant& Variant::operator=(Variant&& other) noexcept
{
    if (this != &other)
    {
        clear();
        data_ = std::exchange(other.data_, nullptr);
        operations_ = std::exchange(other.operations_, nullptr);
        reference_ = std::exchange(other.reference_, false);
    }
    return *this;
}

Variant::~Variant()
{
    clear();
}

Type Variant::type() const
{
    return operations_ != nullptr ? operations_->type() : Type{};
}

Variant Variant::copy() const
{
    Variant owned;
    if (operations_ != nullptr)
    {
        owned.data_ = operations_->clone(data_);
        EAW_ASSERT_MSG(owned.data_ != nullptr, std::string("a variant of ") + std::string(type().name()) + " cannot be copied");
        owned.operations_ = operations_;
    }
    return owned;
}

void Variant::clear() noexcept
{
    if (operations_ != nullptr && !reference_)
    {
        operations_->destroy(data_);
    }
    data_ = nullptr;
    operations_ = nullptr;
    reference_ = false;
}

} // namespace eaw::rti
