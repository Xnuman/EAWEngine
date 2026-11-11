#pragma once

// Moments of animations that play something (ANIMATIONSFXMAPS.TXT, read by
// formats/animation_sfx_map): a sound event, or a surface trigger at a bone whose effect depends
// on the ground there (assets/surface_effects). Animations are named by file stem
// (`ei_trooper_move_00` for DATA/ART/MODELS/EI_TROOPER_MOVE_00.ALA), ignoring case.

#include "formats/animation_sfx_map.hpp"
#include "vfs/virtual_file_system.hpp"

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eaw {

inline constexpr std::string_view animation_events_path = "DATA/XML/ANIMATIONSFXMAPS.TXT";
// Row times are hundredths of a frame (an inference: a trooper's left foot lands at 1032-1170 in
// its 22-frame walk, half way, and 59 of 60 sampled animations end after their last row).
inline constexpr float animation_event_time_per_frame = 100.0F;

// The placeholder the data writes for "nothing" (rows naming it are left out).
inline constexpr std::string_view none_name = "NONE";

enum class AnimationEventKind
{
    sound,
    surface,
};

struct AnimationEvent
{
    AnimationEventKind kind{AnimationEventKind::sound};
    // When it plays, in frames of the animation.
    float frame{};
    // The sound event (sound) or the surface trigger (surface).
    std::string name;
    // Surface events: the bone the effect appears at.
    std::string bone;
};

class AnimationEventTable final
{
public:
    // Rows of other kinds or with too few fields or a bad time are skipped and reported in
    // `warnings` (when given) with their line.
    [[nodiscard]] static AnimationEventTable build(const AnimationSfxMapData& data, std::vector<std::string>* warnings = nullptr);

    // An animation's events in frame order; empty when it has none. `animation` is a stem or a
    // path (its stem is used).
    [[nodiscard]] std::span<const AnimationEvent> find(std::string_view animation) const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    // Lower-case stem -> events.
    std::unordered_map<std::string, std::vector<AnimationEvent>> events_;
    std::size_t size_{};
};

// The table of the VFS; empty, with a warning in `warnings`, when it is missing or unreadable.
[[nodiscard]] AnimationEventTable load_animation_events(const AssetReader& reader, std::vector<std::string>* warnings = nullptr);

} // namespace eaw
