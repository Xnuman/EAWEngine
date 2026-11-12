#pragma once

// The game's sound events (XML SFXEvent), with their presets folded in: what plays, how loud, at
// what pitch, positioned or not, how often at once. Events name samples by bare file names; the
// stock effects live in the sound archives under DATA/AUDIO/SFX/ and speech under
// DATA/AUDIO/SPEECH/<language>/ (resolve_sound_sample).

#include "vfs/virtual_file_system.hpp"
#include "assets/xml_asset_database.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eaw {

struct SoundEvent
{
    std::string name;
    // Sample file names as the XML writes them; one is chosen per play.
    std::vector<std::string> samples;
    // A looping event's intro and outro.
    std::vector<std::string> pre_samples;
    std::vector<std::string> post_samples;
    // Positioned in the world (Is_3D), else heard as interface or voice.
    bool positional{};
    // Volume and pitch ranges as factors (the XML writes percent), and the chance to play at all.
    float min_volume{1.0F};
    float max_volume{1.0F};
    float min_pitch{1.0F};
    float max_pitch{1.0F};
    float probability{1.0F};
    // Samples in turn instead of at random.
    bool sequential{};
    bool localized{};
    // Repeats until stopped (Play_Count -1), with fades in and out in seconds.
    bool loop{};
    float loop_fade_in{};
    float loop_fade_out{};
    // A random wait before playing, in seconds (the XML writes milliseconds).
    float min_predelay{};
    float max_predelay{};
    // Full volume within this distance (world units); 0 uses the mode's default.
    float saturation_distance{};
    std::uint32_t priority{};
    // Plays of this event at once (0: unlimited).
    std::uint32_t max_instances{};
    // Plays that share this name block each other (unit responses of one type).
    std::string overlap_test;
    // Plays after this one.
    std::string chained_event;
    bool unit_response{};
    bool kills_previous_object_sound{};
};

class SoundEventCatalog final
{
public:
    // Every SFXEvent definition that is not a preset, its Use_Preset's fields first and its own over
    // them. The first definition of a name wins, as for object types.
    [[nodiscard]] static SoundEventCatalog build(const XmlAssetDatabase& database);

    [[nodiscard]] const SoundEvent* find(std::string_view name) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const std::vector<SoundEvent>& events() const noexcept;
    // Events naming a preset that does not exist.
    [[nodiscard]] const std::vector<std::string>& missing_presets() const noexcept;

private:
    std::vector<SoundEvent> events_;
    // Folded name -> index.
    std::unordered_map<std::string, std::size_t> index_;
    std::vector<std::string> missing_presets_;
};

// The virtual path of a sample: under DATA/AUDIO/SFX/, else under DATA/AUDIO/SPEECH/<language>/,
// each as named and then with an .MP3 extension (the stock speech is MP3 though the XML says .wav).
[[nodiscard]] std::optional<std::string> resolve_sound_sample(const AssetReader& reader, std::string_view sample, std::string_view language);

} // namespace eaw
