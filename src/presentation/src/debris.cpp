#include "presentation/debris.hpp"

#include "math/quaternion.hpp"
#include "math/transform.hpp"

#include <algorithm>
#include <cmath>

namespace eaw {

void BattleDebris::add_types(const DebrisTable& types)
{
    types_.insert(types.begin(), types.end());
}

void BattleDebris::clear(ParticleWorld& particles)
{
    for (const auto& piece : pieces_)
    {
        particles.stop(piece.trail);
    }
    pieces_.clear();
    types_.clear();
}

void BattleDebris::break_off(const std::uint32_t type, const Matrix4& transform, const Vector3& pivot, ParticleWorld& particles, const ParticleEffectObjectTable& effects, const std::uint64_t seed)
{
    const auto found = types_.find(type);
    if (type == 0 || found == types_.end())
    {
        return;
    }
    const auto& kind = found->second;
    random_.seed(static_cast<std::minstd_rand::result_type>(seed % std::minstd_rand::modulus) + 1U);
    Piece piece;
    piece.mesh = kind.mesh;
    piece.start = transform;
    piece.pivot = pivot;
    // The unit's frame without its scale.
    piece.velocity = transform_direction(kind.velocity, transform) * (1.0F / std::max(length(transform_direction({1.0F, 0.0F, 0.0F}, transform)), 1.0e-6F));
    piece.spin = transform_direction(kind.spin, transform) * (1.0F / std::max(length(transform_direction({1.0F, 0.0F, 0.0F}, transform)), 1.0e-6F));
    piece.lifetime = std::uniform_real_distribution<float>(kind.min_seconds, std::max(kind.min_seconds, kind.max_seconds))(random_);
    piece.death_effect = kind.death_effect;
    piece.transform = transform;
    if (const auto trail = effects.find(kind.trail_effect); kind.trail_effect != 0 && trail != effects.end() && trail->second.effect)
    {
        auto at = Matrix4::identity();
        set_translation(at, pivot);
        piece.trail = particles.play(trail->second.effect, at, random_(), {}, trail->second.scale);
    }
    pieces_.push_back(piece);
}

Matrix4 BattleDebris::place(const Piece& piece)
{
    // Row vectors: the unit's transform, then a turn about the pivot, then the drift.
    const auto angle = length(piece.spin) * piece.age;
    const auto turn = angle > 0.0F ? compose_transform({1.0F, 1.0F, 1.0F}, axis_angle(normalize_or(piece.spin, {0.0F, 0.0F, 1.0F}), angle), {}) : Matrix4::identity();
    const auto to_pivot = compose_transform({1.0F, 1.0F, 1.0F}, Quaternion::identity(), piece.pivot * -1.0F);
    const auto back = compose_transform({1.0F, 1.0F, 1.0F}, Quaternion::identity(), piece.pivot + piece.velocity * piece.age);
    return piece.start * to_pivot * turn * back;
}

void BattleDebris::update(const float seconds, ParticleWorld& particles, std::vector<Ended>& ended)
{
    for (auto& piece : pieces_)
    {
        piece.age += seconds;
        piece.transform = place(piece);
        auto at = Matrix4::identity();
        set_translation(at, piece.pivot + piece.velocity * piece.age);
        particles.set_transform(piece.trail, at);
    }
    std::erase_if(pieces_,
                  [&](const Piece& piece)
                  {
                      if (piece.age < piece.lifetime)
                      {
                          return false;
                      }
                      particles.stop(piece.trail);
                      ended.push_back({piece.death_effect, piece.pivot + piece.velocity * piece.age});
                      return true;
                  });
}

void BattleDebris::append(std::vector<VisualInstance>& instances, std::vector<EntityId>& entities) const
{
    for (const auto& piece : pieces_)
    {
        instances.push_back({piece.mesh, piece.transform});
        entities.emplace_back();
    }
}

} // namespace eaw
