#include "simulation/combat.hpp"

namespace eaw {

void DamageTable::set(const std::uint16_t damage_type, const std::uint16_t armor_type, const float modifier)
{
    modifiers_[(static_cast<std::uint32_t>(damage_type) << 16U) | armor_type] = modifier;
}

float DamageTable::modifier(const std::uint16_t damage_type, const std::uint16_t armor_type) const noexcept
{
    const auto found = modifiers_.find((static_cast<std::uint32_t>(damage_type) << 16U) | armor_type);
    return found == modifiers_.end() ? 1.0F : found->second;
}

std::size_t DamageTable::size() const noexcept
{
    return modifiers_.size();
}

void Hostility::set(const std::uint32_t first, const std::uint32_t second, const bool hostile) noexcept
{
    if (first >= max_owners || second >= max_owners)
    {
        return;
    }
    const auto first_bit = std::uint64_t{1} << second;
    const auto second_bit = std::uint64_t{1} << first;
    rows_[first] = hostile ? rows_[first] | first_bit : rows_[first] & ~first_bit;
    rows_[second] = hostile ? rows_[second] | second_bit : rows_[second] & ~second_bit;
}

bool Hostility::hostile(const std::uint32_t first, const std::uint32_t second) const noexcept
{
    return first < max_owners && second < max_owners && (rows_[first] & (std::uint64_t{1} << second)) != 0;
}

} // namespace eaw
