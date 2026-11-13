#include "render/lod_selection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eaw {

float visual_lod_scale(const VisualLodSettings& settings, const float lod_bias) noexcept
{
    const float detail = std::isfinite(settings.mesh_detail) ? std::clamp(settings.mesh_detail, 0.0F, 1.0F) : 1.0F;
    const float bias = std::isfinite(lod_bias) && lod_bias > 0.0F ? lod_bias : 1.0F;
    return bias * (0.5F + detail);
}

float visual_lod_position(const VisualLodSettings& settings, const float lod_bias, const float distance, const float radius, const float vertical_fov, const float viewport_height) noexcept
{
    const float scale = visual_lod_scale(settings, lod_bias);
    const auto by_distance = [&]
    {
        return std::max(0.0F, distance) / std::max(settings.distance_step * scale, 1.0e-3F);
    };
    const auto by_screen = [&]
    {
        const float half_angle = std::tan(std::clamp(vertical_fov, 1.0e-3F, 3.0F) * 0.5F);
        const float pixels = 2.0F * std::max(radius, 0.0F) / (std::max(distance, 1.0e-3F) * half_angle) * std::max(viewport_height, 1.0F) * 0.5F;
        return std::max(0.0F, std::log2(settings.full_detail_pixels * scale / std::max(pixels, 1.0e-3F)));
    };
    switch (settings.policy)
    {
    case VisualLodPolicy::off:
        return 0.0F;
    case VisualLodPolicy::distance:
        return by_distance();
    case VisualLodPolicy::screen_size:
        return by_screen();
    case VisualLodPolicy::hybrid:
        if (distance <= settings.near_distance * scale)
        {
            return 0.0F;
        }
        if (distance >= settings.far_distance * scale)
        {
            return 1.0e6F;
        }
        return by_screen();
    }
    return 0.0F;
}

std::uint32_t visual_lod_level(const float position, const std::uint32_t level_count, const std::optional<std::uint32_t> previous, const float hysteresis) noexcept
{
    if (level_count <= 1)
    {
        return 0;
    }
    const auto last = static_cast<float>(level_count - 1U);
    const float clamped = std::isfinite(position) ? std::clamp(position, 0.0F, last) : last;
    const auto plain = static_cast<std::uint32_t>(std::min(std::floor(clamped), last));
    if (!previous || *previous >= level_count)
    {
        return plain;
    }
    const auto held = static_cast<float>(*previous);
    const float band = std::max(hysteresis, 0.0F);
    // Coarser once past the next boundary by the band; finer once below this level's start by it.
    // The band is measured on the unclamped position, so the last level is reachable too.
    const float unclamped = std::isfinite(position) ? std::max(position, 0.0F) : std::numeric_limits<float>::max();
    if (unclamped >= held + 1.0F + band || unclamped < held - band)
    {
        return plain;
    }
    return *previous;
}

VisualLodChoice VisualLodTracker::update(const std::uint64_t key, const std::uint32_t mesh, const std::uint32_t state, const float position, const std::uint32_t level_count, const float now,
                                         const VisualLodSettings& settings)
{
    const float fade_seconds = std::isfinite(settings.fade_seconds) ? std::max(settings.fade_seconds, 0.0F) : 0.0F;
    auto [entry, added] = tracks_.try_emplace(key);
    auto& track = entry->second;
    VisualLodChoice choice;
    if (added || track.mesh != mesh || track.state != state)
    {
        track = {mesh, state, visual_lod_level(position, level_count, std::nullopt, settings.hysteresis), std::nullopt, now, 0};
    }
    else
    {
        const auto level = visual_lod_level(position, level_count, track.level, settings.hysteresis);
        if (level != track.level)
        {
            track.from = fade_seconds > 0.0F ? std::optional(track.level) : std::nullopt;
            track.level = level;
            track.fade_start = now;
            choice.changed = true;
        }
    }
    track.seen = pass_;
    choice.level = track.level;
    const float progress = fade_seconds > 0.0F ? std::clamp((now - track.fade_start) / fade_seconds, 0.0F, 1.0F) : 1.0F;
    if (track.from && progress < 1.0F && *track.from < level_count)
    {
        // Never exactly 0 (drawn whole) or 1 at either end of the fade.
        choice.from = track.from;
        choice.fade = std::clamp(progress, 1.0F / 64.0F, 63.0F / 64.0F);
    }
    else
    {
        track.from.reset();
    }
    return choice;
}

void VisualLodTracker::end_pass()
{
    std::erase_if(tracks_,
                  [&](const auto& entry)
                  {
                      return entry.second.seen != pass_;
                  });
}

} // namespace eaw
