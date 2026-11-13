#pragma once

// Debris (XML SpaceBehavior DEBRIS props, a hardpoint's Death_Breakoff_Prop): a model that breaks
// off a destroyed hardpoint, drifts and tumbles away, trails its attached particle, and explodes
// when its life ends. Presentation only: the simulation never sees it.
//
// Engine choices (the original's units are not documented): Debris_Movement_Vector is world units
// per 30 Hz frame and Debris_Facing_Rotate_Vector degrees per 30 Hz frame, both in the frame of the
// unit it broke off (legacy_units, as Max_Speed and Max_Rate_Of_Turn); the piece turns about the
// hardpoint, where it broke off.

#include "math/matrix.hpp"
#include "math/vector.hpp"
#include "render/visual_frame.hpp"
#include "simulation/simulation.hpp"
#include "presentation/particle_effects.hpp"

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

namespace eaw {

// A debris prop type: its mesh in the scene, its motion, and its effects (Particle object keys, 0
// none).
struct DebrisType
{
    std::uint32_t mesh{};
    // Per second, in the frame of the unit it broke off: drift (world units) and turn (radians).
    Vector3 velocity;
    Vector3 spin;
    float min_seconds{};
    float max_seconds{};
    std::uint32_t trail_effect{};
    std::uint32_t death_effect{};
};

// Debris types by the CRC-32 of their name (game_object_name_crc).
using DebrisTable = std::unordered_map<std::uint32_t, DebrisType>;

class BattleDebris
{
public:
    void add_types(const DebrisTable& types);
    void clear(ParticleWorld& particles);

    // Breaks a piece of type `type` off a unit drawn at `transform` (its instance transform), at the
    // hardpoint at `pivot` (world). Unknown types are ignored.
    void break_off(std::uint32_t type, const Matrix4& transform, const Vector3& pivot, ParticleWorld& particles, const ParticleEffectObjectTable& effects, std::uint64_t seed);
    // Moves the pieces by `seconds`; a piece whose life ends returns its death effect's key and
    // position through `ended` and goes.
    struct Ended
    {
        std::uint32_t effect{};
        Vector3 position;
    };
    void update(float seconds, ParticleWorld& particles, std::vector<Ended>& ended);
    // Appends a drawn instance per piece, with an invalid entity (never picked).
    void append(std::vector<VisualInstance>& instances, std::vector<EntityId>& entities) const;
    [[nodiscard]] std::size_t size() const noexcept
    {
        return pieces_.size();
    }

private:
    struct Piece
    {
        std::uint32_t mesh{};
        Matrix4 start{Matrix4::identity()};
        Vector3 pivot;
        Vector3 velocity;
        Vector3 spin;
        float age{};
        float lifetime{};
        std::uint32_t death_effect{};
        ParticleInstanceId trail;
        Matrix4 transform{Matrix4::identity()};
    };
    DebrisTable types_;
    std::vector<Piece> pieces_;
    std::minstd_rand random_;

    static Matrix4 place(const Piece& piece);
};

} // namespace eaw
