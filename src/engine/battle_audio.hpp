#pragma once

// A battle's sounds, from presentation: weapon fire (each weapon's own sound, from the snapshot's
// shots) and deaths from each tick's snapshot, engine
// loops on moving units, and unit responses the player's orders call for, played through an
// AudioDevice by the game's sound events (assets/sound_events). Like BattleEffects it reads
// snapshots and never feeds back into the simulation; its random choices (samples, volume, pitch)
// come from a presentation stream of its own.

#include "assets/animation_events.hpp"
#include "assets/asset_cache.hpp"
#include "assets/sound_events.hpp"
#include "assets/surface_effects.hpp"
#include "audio/audio_device.hpp"
#include "audio/spatial_audio.hpp"
#include "core/random.hpp"
#include "eaw/simulation.hpp"
#include "eaw/visual_scene.hpp"
#include "unit_animation.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eaw {

class GameObjectCatalog;

// AUDIO.XML's 3D factors of a mode: saturation distances scale by `saturation_scale`, the rolloff
// past them by `rolloff_scale`, and the listener sits `listener_height` above the camera's focus.
struct AudioModeSettings
{
    float saturation_scale{1.0F};
    float rolloff_scale{1.0F};
    float listener_height{60.0F};
};

// The land or space settings of AUDIO.XML (defaults when it cannot be read).
[[nodiscard]] AudioModeSettings read_audio_mode_settings(const AssetReader& reader, bool land);

// What a unit says when the player orders it.
enum class UnitResponse
{
    select,
    move,
    attack,
    guard,
};

// The volume curve beyond saturation, before AUDIO.XML's per-mode factor (an engine choice that
// puts land sounds at half volume 200 units past saturation, with the land factor of 10), the
// saturation distance of events that name none, and how many unit loops (engines, ambient) play at
// once, a quarter of the device's voices (the units nearest the listener).
inline constexpr float base_rolloff = 0.05F;
inline constexpr float default_saturation_distance = 200.0F;
inline constexpr std::size_t max_unit_loops = 16;
// The pause after an order's response before another unit type's assist line, and how long an
// assist line waits for the voices it must not talk over (engine choices).
inline constexpr float assist_response_delay = 0.8F;
inline constexpr float assist_patience_seconds = 5.0F;
// Health fractions at which a unit's low and critical warnings play: the stock GameConstants'
// Health_Low_Percent_Threshold and Health_Critical_Percent_Threshold (shortcut: fixed here, read
// GameConstants when a mod changes them).
inline constexpr float health_low_fraction = 0.33F;
inline constexpr float health_critical_fraction = 0.10F;

class BattleAudio final
{
public:
    // Reads the sound events (through `cache`'s XML catalog) and the mode's settings; `language`
    // finds localized speech.
    BattleAudio(const AssetReader& reader, AssetCache* cache, AudioDevice& device, std::string language, bool land, std::uint64_t seed = 1);
    // Stops its voices (loops would otherwise play on in the device).
    ~BattleAudio();
    BattleAudio(const BattleAudio&) = delete;
    BattleAudio& operator=(const BattleAudio&) = delete;

    // Weapon fire, shot impacts, and deaths of the tick the snapshot ends (after every tick, so none
    // is missed).
    void play_tick_events(const SimSnapshot& snapshot, const SimulationWorld& world);
    // Once per frame: the listener follows the camera, positioned voices follow it, engine and
    // ambient loops follow the nearest units, delayed and chained events start, finished voices end.
    void update(const VisualCamera& camera, const SimSnapshot& snapshot, const SimulationWorld& world, float seconds);
    // A sound event by name, at a point (positional events) or heard everywhere; false when it did
    // not play (unknown, its chance, its instance limit, no voice, or a missing sample).
    bool play_event(std::string_view name, std::optional<Vector3> position = std::nullopt);
    // The sounds placed in the animations units played this frame (UnitAnimator::cues): sound rows
    // play their event, surface rows the sound of their SurfaceFX on the ground there (its default
    // settings until terrain surface kinds are known).
    void play_animation_cues(std::span<const AnimationCue> cues);
    // A unit type's response to an order (XML SFXEvent_Select, SFXEvent_Move, SFXEvent_Attack,
    // SFXEvent_Guard).
    bool play_response(std::string_view type_name, UnitResponse response);
    // A unit type's line when ordered as part of a group (XML SFXEvent_Group_Move for moves,
    // SFXEvent_Group_Attack for attacks; heroes), or false when it has none.
    bool play_group_response(std::string_view type_name, UnitResponse response);
    // A unit type's answer to an order given with another type, `pause` seconds after the response
    // playing now has finished (XML SFXEvent_Assist_Move for moves, SFXEvent_Assist_Attack for
    // attacks: "Escorting!" after Luke's "Understood!"); false when it has none. It waits while any
    // voice sharing its overlap name plays, at most assist_patience_seconds.
    bool play_assist_response(std::string_view type_name, UnitResponse response, float pause);

    struct Stats
    {
        std::uint64_t played{};
        // Not played by the event's rules (chance, a loop at its instance limit, overlap) or for want
        // of a voice.
        std::uint64_t skipped{};
        // Plays that replaced the event's oldest play at its instance limit.
        std::uint64_t replaced{};
        // Plays started by animations (footsteps, hydraulics), counted in `played` too.
        std::uint64_t from_animations{};
        // Unknown events, and samples that could not be found or decoded.
        std::uint64_t missing{};
    };
    [[nodiscard]] const Stats& stats() const noexcept;
    [[nodiscard]] const SoundEventCatalog& events() const noexcept;

private:
    // The sound events of an object type (weapon sounds come with the shots).
    struct TypeSounds
    {
        std::string death;
        std::string death_explosion;
        std::string engine_loop;
        std::string engine_idle_loop;
        std::string ambient_loop;
        std::string select;
        std::string move;
        std::string attack;
        std::string guard;
        // Played now and then while it moves, every min..max seconds (SFXEvent_Ambient_Moving), when
        // its behaviors include AMBIENT_SFX (a developer comment requires it).
        std::string ambient_moving;
        float ambient_moving_min{};
        float ambient_moving_max{};
        // Its own voice when its health falls to low and to critical (SFXEvent_Health_Low_Warning,
        // _Critical_Warning), and its sound when it spins away (Spin_Away_On_Death_SFXEvent_Start_Die).
        std::string health_low;
        std::string health_critical;
        std::string spin;
        std::string group_move;
        std::string group_attack;
        std::string assist_move;
        std::string assist_attack;
        // Its voice reporting the loss of a hardpoint, by the hardpoint's Type key
        // (SFXEvent_Hardpoint_Destroyed: "HARD_POINT_<kind>, event" entries).
        std::vector<std::pair<std::uint32_t, std::string>> hardpoint_lost;
    };
    // A projectile type's impact sounds (Projectile_SFXEvent_Detonate, and _Reduced_By_Armor).
    struct ProjectileSounds
    {
        std::string detonate;
        std::string reduced;
    };
    struct Voice
    {
        VoiceId id{};
        const SoundEvent* event{};
        std::optional<Vector3> position;
        float volume{1.0F};
        // The unit a loop follows (its entity key), else 0.
        std::uint64_t entity{};
    };
    struct Pending
    {
        float delay{};
        const SoundEvent* event{};
        std::optional<Vector3> position;
        // Assist lines: the pause they keep after the voices of their overlap name end (the delay
        // restarts at it while one plays), and how much longer they may wait.
        float pause{};
        float patience{};
    };
    // Whether a voice sharing `event`'s overlap name plays.
    [[nodiscard]] bool overlap_busy(const SoundEvent& event) const;

    const TypeSounds& type_sounds(std::string_view type_name);
    // Starts `event` now; the voice id, or 0.
    VoiceId start(const SoundEvent& event, std::optional<Vector3> position, std::uint64_t entity);
    [[nodiscard]] SpatialGain place(const SoundEvent& event, const std::optional<Vector3>& position) const;
    void update_unit_loops(const SimSnapshot& snapshot, const SimulationWorld& world);
    void update_ambient_moving(const SimSnapshot& snapshot, const SimulationWorld& world, float seconds);
    [[nodiscard]] const ProjectileSounds* projectile_sounds(std::uint32_t visual);

    const AssetReader& reader_;
    AssetCache* cache_;
    AudioDevice& device_;
    std::string language_;
    AudioModeSettings settings_;
    SoundEventCatalog events_;
    AnimationEventTable animation_events_;
    SurfaceEffectCatalog surface_effects_;
    const GameObjectCatalog* catalog_{};
    std::shared_ptr<const void> catalog_owner_;
    AudioListener listener_;
    Pcg32 random_;
    std::unordered_map<std::string, TypeSounds> type_sounds_;
    // Sound event names by their CRC (WeaponProfile::fire_sound).
    std::unordered_map<std::uint32_t, std::string> event_names_;
    // Projectile type names by their CRC (ImpactSnapshot::visual), and their sounds once looked up.
    std::unordered_map<std::uint32_t, std::string> projectile_names_;
    std::unordered_map<std::uint32_t, ProjectileSounds> projectile_sounds_;
    // Moving units' seconds until their next SFXEvent_Ambient_Moving (by entity key).
    std::unordered_map<std::uint64_t, float> ambient_moving_due_;
    // Per entity: the health band it last reported (0 above low, 1 low, 2 critical).
    std::unordered_map<std::uint64_t, std::uint8_t> health_bands_;
    // Entity key -> type sounds, kept for entities seen alive so their deaths find them.
    std::unordered_map<std::uint64_t, const TypeSounds*> entity_sounds_;
    // Sequential events: the next sample's index, by event.
    std::unordered_map<const SoundEvent*, std::size_t> next_sample_;
    std::vector<Voice> voices_;
    std::vector<Pending> pending_;
    Stats stats_;
};

} // namespace eaw
