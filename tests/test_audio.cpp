// Audio: the WAV reader, sound decoding, sound events, spatial gains, the null device, the sound
// archives' mounting, and battle sounds.
#include "test_support.hpp"

#include "assets/animation_events.hpp"
#include "assets/sound_events.hpp"
#include "assets/surface_effects.hpp"
#include "assets/sound_loaders.hpp"
#include "audio/null_audio_device.hpp"
#include "audio/spatial_audio.hpp"
#include "battle_audio.hpp"
#include "formats/animation_sfx_map.hpp"
#include "formats/wav_audio.hpp"

#include <cmath>

namespace {

using namespace eaw_test;

void append_tag(std::vector<std::byte>& bytes, const std::string_view tag)
{
    for (const char character : tag)
    {
        bytes.push_back(static_cast<std::byte>(character));
    }
}

// A RIFF WAVE file: its fmt chunk, an odd-sized LIST chunk (padded), then the data chunk.
std::vector<std::byte> make_wav(const std::uint16_t format, const std::uint16_t channels, const std::uint16_t bits, const std::vector<std::byte>& samples, const bool with_format = true)
{
    std::vector<std::byte> body;
    append_tag(body, "WAVE");
    if (with_format)
    {
        append_tag(body, "fmt ");
        append_u32(body, 16U);
        append_u16(body, format);
        append_u16(body, channels);
        append_u32(body, 8000U);
        const auto block = static_cast<std::uint16_t>(channels * bits / 8U);
        append_u32(body, 8000U * block);
        append_u16(body, block);
        append_u16(body, bits);
    }
    append_tag(body, "LIST");
    append_u32(body, 3U);
    append_tag(body, "abc");
    body.push_back(std::byte{0});
    append_tag(body, "data");
    append_u32(body, static_cast<std::uint32_t>(samples.size()));
    body.insert(body.end(), samples.begin(), samples.end());
    std::vector<std::byte> file;
    append_tag(file, "RIFF");
    append_u32(file, static_cast<std::uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

std::vector<std::byte> pcm16(const std::vector<std::int16_t>& values)
{
    std::vector<std::byte> bytes;
    for (const auto value : values)
    {
        append_u16(bytes, static_cast<std::uint16_t>(value));
    }
    return bytes;
}

void test_wav_reader()
{
    const auto file = make_wav(eaw::wav_format::pcm, 1, 16, pcm16({0, 1000, -1000, 32767}));
    const auto wav = expect_ok(eaw::read_wav(file), "PCM WAV");
    require(wav.format == eaw::wav_format::pcm && wav.channels == 1 && wav.sample_rate == 8000U && wav.bits_per_sample == 16 && wav.samples.size() == 8, "the fmt and data chunks must be read");
    require(wav.other_chunks.size() == 1 && wav.other_chunks[0].bytes.size() == 3 && wav.other_chunks[0].id == std::array<char, 4>{'L', 'I', 'S', 'T'},
            "other chunks must be kept, without their padding byte");

    const auto truncated = std::span(file).first(file.size() - 3);
    require(!eaw::read_wav(truncated).ok(), "a data chunk past the end must be an error");
    require(!eaw::read_wav(make_wav(eaw::wav_format::pcm, 1, 16, pcm16({1}), false)).ok(), "a file without a fmt chunk must be an error");
    std::vector<std::byte> not_riff(file);
    not_riff[0] = std::byte{'X'};
    require(!eaw::read_wav(not_riff).ok(), "a file that is not RIFF must be an error");
    require(!eaw::read_wav(std::span(file).first(10)).ok(), "a header-only file must be an error");
}

void test_sound_decoding()
{
    const auto sound = expect_ok(eaw::decode_sound(make_wav(eaw::wav_format::pcm, 2, 16, pcm16({1, 2, 3, 4}))), "16-bit stereo");
    require(sound.channels == 2 && sound.sample_rate == 8000U && sound.frames() == 2 && sound.samples == std::vector<std::int16_t>{1, 2, 3, 4}, "16-bit PCM must pass through");

    const std::vector<std::byte> eight{std::byte{0x80}, std::byte{0x00}, std::byte{0xFF}};
    const auto low = expect_ok(eaw::decode_sound(make_wav(eaw::wav_format::pcm, 1, 8, eight)), "8-bit");
    require(low.samples.size() == 3 && low.samples[0] == 0 && low.samples[1] == -32768 && low.samples[2] > 32000, "8-bit PCM is unsigned around 128");

    std::vector<std::byte> floats;
    for (const float value : {1.0F, -1.0F, 0.0F, 2.0F})
    {
        append_float(floats, value);
    }
    const auto from_float = expect_ok(eaw::decode_sound(make_wav(eaw::wav_format::ieee_float, 1, 32, floats)), "float");
    require(from_float.samples == std::vector<std::int16_t>{32767, -32767, 0, 32767}, "float samples must scale and clip");

    const auto adpcm = eaw::decode_sound(make_wav(eaw::wav_format::ms_adpcm, 1, 16, pcm16({0, 0})));
    require(!adpcm.ok() && adpcm.error().message.find("coding 2") != std::string::npos, "an unsupported WAV coding must be an error naming it");

    const auto bytes_of = [](const std::string_view text)
    {
        std::vector<std::byte> bytes;
        append_tag(bytes, text);
        return bytes;
    };
    require(eaw::sound_file_format(bytes_of("ID3\x03")) == eaw::SoundFileFormat::mp3 && eaw::sound_file_format(bytes_of("\xFF\xFB\x90")) == eaw::SoundFileFormat::mp3 &&
                eaw::sound_file_format(bytes_of("OggS")) == eaw::SoundFileFormat::ogg && eaw::sound_file_format(bytes_of("RIFF....WAVE")) == eaw::SoundFileFormat::wav &&
                eaw::sound_file_format(bytes_of("xx")) == eaw::SoundFileFormat::unknown,
            "formats must be recognized from their first bytes");
    require(!eaw::decode_sound(bytes_of("ID3\x03 mp3 data")).ok() && !eaw::decode_sound(bytes_of("??")).ok(), "MP3 (not yet supported) and unknown data must be errors");
}

void test_spatial_audio()
{
    const eaw::AudioListener listener{{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}};
    const auto near = eaw::spatialize(listener, {0.0F, 50.0F, 0.0F}, 100.0F, 0.01F);
    require(near.volume == 1.0F && std::abs(near.pan) < 1e-5F, "within saturation a sound plays at full volume, centred ahead");
    // 100 units past saturation with rolloff 0.01: 100 / (100 + 0.01 * 100 * ...).
    const auto far = eaw::spatialize(listener, {0.0F, 300.0F, 0.0F}, 100.0F, 0.01F);
    require(far.volume > 0.0F && far.volume < 1.0F, "past saturation a sound fades");
    require(eaw::spatialize(listener, {0.0F, 900.0F, 0.0F}, 100.0F, 0.01F).volume < far.volume, "farther sounds are quieter");
    require(eaw::spatialize(listener, {200.0F, 0.0F, 0.0F}, 100.0F, 0.01F).pan > 0.9F && eaw::spatialize(listener, {-200.0F, 0.0F, 0.0F}, 100.0F, 0.01F).pan < -0.9F,
            "sounds to the listener's right pan right");
    const auto centre = eaw::stereo_gains(0.0F);
    const auto right = eaw::stereo_gains(1.0F);
    require(std::abs(centre.left - centre.right) < 1e-5F && std::abs(centre.left * centre.left + centre.right * centre.right - 1.0F) < 1e-5F && right.left < 1e-5F && right.right > 0.999F,
            "the pan law keeps equal power");
}

void test_null_audio_device()
{
    eaw::NullAudioDevice device;
    const auto sound = std::make_shared<const eaw::Sound>(eaw::Sound{1, 8000U, {1, 2}});
    const auto shot = device.play({sound, eaw::AudioCategory::effects, 0.5F, 1.0F, 0.0F, false});
    const auto loop = device.play({sound, eaw::AudioCategory::effects, 1.0F, 1.0F, 0.0F, true});
    require(shot != 0 && loop != 0 && shot != loop && device.active_voices() == 2 && device.playing(shot), "plays must get distinct voices");
    device.update_voice(shot, 0.25F, -1.0F);
    require(device.played().size() == 2 && device.played()[0].playback.volume == 0.25F && device.played()[0].playback.pan == -1.0F, "plays must be recorded with their latest gains");
    device.update();
    require(!device.playing(shot) && device.playing(loop), "a one-shot ends at the next update, a loop plays on");
    device.stop(loop);
    require(device.active_voices() == 0 && device.play({nullptr}) == 0, "stopped voices end, and an empty play gets no voice");
    device.set_category_volume(eaw::AudioCategory::music, 0.5F);
    device.set_master_volume(0.2F);
    require(device.category_volume(eaw::AudioCategory::music) == 0.5F && device.master_volume() == 0.2F, "category and master volumes must be kept");
}

void write_file(const std::filesystem::path& path, const std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string text_of(const std::vector<std::byte>& bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// The sound archives are not in megafiles.xml and store bare names: they mount under
// DATA/AUDIO/SFX/, the language archive of the chosen language only, below loose files.
void test_sound_archives()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-sound-archives-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    const auto sfx = root / "Data" / "Audio" / "SFX";
    write_test_meg(sfx / "sfx2d_non_localized.meg", {{"SHARED.WAV", "archive"}, {"LOOSE.WAV", "archive"}});
    write_test_meg(sfx / "SFX2D_English.meg", {{"HELLO.WAV", "english"}});
    write_test_meg(sfx / "sfx2d_german.meg", {{"HELLO.WAV", "german"}});
    write_file(sfx / "LOOSE.WAV", "loose");
    const eaw::GameProfile profile{root, root, root / "Data", eaw::GameVariant::empire_at_war};
    for (const auto& [language, hello] : {std::pair<std::string_view, std::string_view>{"English", "english"}, {"german", "german"}})
    {
        eaw::VirtualFileSystem vfs;
        (void)eaw::mount_game_assets(vfs, profile, {}, language);
        require(read_vfs_text(vfs, "Data/Audio/SFX/shared.wav") == "archive", "a sound archive's bare names must resolve under DATA/AUDIO/SFX");
        require(read_vfs_text(vfs, "DATA/AUDIO/SFX/HELLO.WAV") == hello, "only the chosen language's archive must be mounted");
        require(read_vfs_text(vfs, "DATA/AUDIO/SFX/LOOSE.WAV") == "loose", "loose files must win over the sound archives");
        require(!vfs.resolve("HELLO.WAV"), "archive entries must not appear at the root");
    }
    std::filesystem::remove_all(root, ignored);
}

// Sound events fold their presets in, and samples resolve under SFX, then the language's speech
// folder, then as MP3.
void test_sound_events()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-sound-events-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    write_file(root / "Data" / "XML" / "SFXEvents.xml",
               "<SFXEvents>"
               "<SFXEvent Name=\"Preset_Loud\"><Is_Preset>Yes</Is_Preset><Is_3D>Yes</Is_3D><Min_Volume>80</Min_Volume><Max_Volume>90</Max_Volume><Max_Instances>2</Max_Instances></SFXEvent>"
               "<SFXEvent Name=\"Shot\"><Use_Preset>Preset_Loud</Use_Preset><Max_Volume>100</Max_Volume><Samples>shot_a.wav, shot_b.wav\n shot_c.wav</Samples>"
               "<Min_Predelay>100</Min_Predelay><Max_Predelay>300</Max_Predelay><Probability>50, 50</Probability></SFXEvent>"
               "<SFXEvent Name=\"Loop\"><Play_Count>-1</Play_Count><Use_Preset>Nowhere</Use_Preset></SFXEvent>"
               "<SFXEvent Name=\"shot\"><Samples>duplicate.wav</Samples></SFXEvent>"
               "</SFXEvents>");
    write_file(root / "Data" / "Audio" / "SFX" / "shot_a.wav", "x");
    write_file(root / "Data" / "Audio" / "Speech" / "English" / "hello.mp3", "x");
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    const auto catalog = eaw::SoundEventCatalog::build(eaw::XmlAssetDatabase::build(vfs));
    require(catalog.size() == 2 && catalog.find("Preset_Loud") == nullptr, "presets are not events, and the first definition of a name wins");
    const auto* shot = catalog.find("SHOT");
    require(shot != nullptr && shot->positional && std::abs(shot->min_volume - 0.8F) < 1e-6F && std::abs(shot->max_volume - 1.0F) < 1e-6F && shot->max_instances == 2,
            "an event's own fields must apply over its preset's");
    require(shot->samples == std::vector<std::string>{"shot_a.wav", "shot_b.wav", "shot_c.wav"} && std::abs(shot->max_predelay - 0.3F) < 1e-6F && std::abs(shot->probability - 0.5F) < 1e-6F,
            "samples, predelays (ms), and the first probability (percent) must be read");
    require(catalog.find("Loop")->loop && catalog.missing_presets() == std::vector<std::string>{"Loop (Nowhere)"}, "a negative play count loops and a missing preset is reported");

    require(eaw::ascii_equal_ignoring_case(eaw::resolve_sound_sample(vfs, "SHOT_A.WAV", "English").value_or(""), "DATA/AUDIO/SFX/SHOT_A.WAV"), "effects resolve under DATA/AUDIO/SFX");
    require(eaw::ascii_equal_ignoring_case(eaw::resolve_sound_sample(vfs, "hello.wav", "english").value_or(""), "DATA/AUDIO/SPEECH/ENGLISH/HELLO.MP3"), "speech named .wav resolves to its MP3");
    require(!eaw::resolve_sound_sample(vfs, "shot_b.wav", "English"), "a missing sample resolves to nothing");
    std::filesystem::remove_all(root, ignored);
}

// Battle sounds: a shooter's fire event per shot (within its instance limit), its victim's death,
// a unit response heard everywhere, and unknown events counted as missing.
void test_battle_audio()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-battle-audio-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    write_file(root / "Data" / "XML" / "SFXEvents.xml", "<SFXEvents>"
                                                        "<SFXEvent Name=\"Shot\"><Is_3D>Yes</Is_3D><Max_Instances>1</Max_Instances><Samples>shot.wav</Samples></SFXEvent>"
                                                        "<SFXEvent Name=\"Die\"><Is_3D>Yes</Is_3D><Samples>shot.wav</Samples><Chained_SFXEvent>Shot</Chained_SFXEvent></SFXEvent>"
                                                        "<SFXEvent Name=\"Hello\"><Is_2D>Yes</Is_2D><Is_Unit_Response_VO>Yes</Is_Unit_Response_VO><Samples>shot.wav</Samples></SFXEvent>"
                                                        "<SFXEvent Name=\"Silent\"><Samples>nothing.wav</Samples></SFXEvent>"
                                                        "<SFXEvent Name=\"Zap\"><Is_3D>Yes</Is_3D><Samples>shot.wav</Samples></SFXEvent>"
                                                        "<SFXEvent Name=\"Hum\"><Play_Count>-1</Play_Count><Samples>shot.wav</Samples></SFXEvent>"
                                                        "</SFXEvents>");
    write_file(root / "Data" / "XML" / "Units.xml", "<Units>"
                                                    "<GroundVehicle Name=\"Gunner\"><SFXEvent_Select>Hello</SFXEvent_Select></GroundVehicle>"
                                                    "<GroundVehicle Name=\"Target\"><Death_SFXEvent_Start_Die>Die</Death_SFXEvent_Start_Die></GroundVehicle>"
                                                    "</Units>");
    write_file(root / "Data" / "XML" / "Audio.xml", "<Audio><Audio_Land_3D_Rolloff_Distance_Mod>10</Audio_Land_3D_Rolloff_Distance_Mod></Audio>");
    {
        const auto wav = make_wav(eaw::wav_format::pcm, 1, 16, pcm16({1, 2, 3}));
        write_file(root / "Data" / "Audio" / "SFX" / "shot.wav", text_of(wav));
    }
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    require(eaw::read_audio_mode_settings(vfs, true).rolloff_scale == 10.0F && eaw::read_audio_mode_settings(vfs, false).rolloff_scale == 1.0F, "AUDIO.XML's mode factors must be read");

    eaw::AssetCache cache;
    eaw::NullAudioDevice device;
    eaw::BattleAudio audio(vfs, &cache, device, "English", true);
    eaw::SimulationWorld world(0.1);
    eaw::Hostility hostility;
    hostility.set(1, 2, true);
    world.set_hostility(hostility);
    eaw::CombatProfile gun;
    gun.health = 50.0F;
    // Three weapons (hardpoints): two share a sound, which their shots of one tick play once.
    eaw::WeaponProfile shot{.range = 100.0F, .recharge_seconds = 0.1F, .damage = 10.0F};
    shot.fire_sound = eaw::game_object_name_crc("Shot");
    auto zap = shot;
    zap.damage = 0.0F;
    zap.fire_sound = eaw::game_object_name_crc("ZAP");
    auto twin = zap;
    twin.fire_sound = shot.fire_sound;
    gun.weapons = {shot, zap, twin};
    eaw::CombatProfile target;
    target.health = 15.0F;
    require(world.spawn({.type_name = "Gunner", .owner = 1, .combat = gun}) && world.spawn({.type_name = "Target", .owner = 2, .position = {60.0F, 0.0F, 0.0F}, .combat = target}),
            "test spawns must succeed");
    const auto count = [&](const eaw::AudioCategory category)
    {
        return std::ranges::count_if(device.played(),
                                     [&](const eaw::NullAudioDevice::Played& play)
                                     {
                                         return play.playback.category == category;
                                     });
    };
    world.step();
    audio.play_tick_events(world.snapshot(), world);
    require(device.played().size() == 2 && device.played()[0].playback.volume == 1.0F && audio.stats().skipped == 0, "each weapon must play its own fire event, once per tick");
    world.step();
    audio.play_tick_events(world.snapshot(), world);
    require(world.stats().kills == 1 && audio.stats().skipped == 0 && audio.stats().replaced == 1 && device.played().size() == 5 && !device.playing(device.played()[0].voice),
            "the second Shot replaces the first at its instance limit; Zap and the death play");
    const eaw::VisualCamera camera{{0.0F, -100.0F, 100.0F}, {0.0F, 0.0F, 0.0F}};
    // The device ends finished voices at the end of a frame; the next frame starts their chains.
    audio.update(camera, world.snapshot(), world, 0.1F);
    audio.update(camera, world.snapshot(), world, 0.1F);
    require(device.played().size() == 6, "a finished event must start its chained event");
    require(audio.play_response("gunner", eaw::UnitResponse::select) && count(eaw::AudioCategory::voice) == 1, "a unit response must play as voice");
    require(!audio.play_response("Target", eaw::UnitResponse::move) && !audio.play_event("Undefined") && !audio.play_event("Silent") && audio.stats().missing == 2,
            "unknown events and unresolved samples must be counted as missing");
    require(audio.stats().played == 7 && count(eaw::AudioCategory::effects) == 6, "battle sounds must play as effects");
    {
        // A battle's loops end with it.
        device.update();
        eaw::BattleAudio second(vfs, &cache, device, "English", true);
        require(second.play_event("Hum") && device.active_voices() == 1, "a loop must play");
    }
    device.update();
    require(device.active_voices() == 0, "a battle's voices must stop when it ends");
    std::filesystem::remove_all(root, ignored);
}

// A unit's loops: its idle engine and ambient loop while still, its moving engine instead of the idle
// one once it moves, and nothing once it is gone.
void test_unit_loops()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-unit-loops-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    write_file(root / "Data" / "XML" / "SFXEvents.xml", "<SFXEvents>"
                                                        "<SFXEvent Name=\"Hum\"><Is_3D>Yes</Is_3D><Play_Count>-1</Play_Count><Samples>loop.wav</Samples></SFXEvent>"
                                                        "<SFXEvent Name=\"Idle\"><Is_3D>Yes</Is_3D><Play_Count>-1</Play_Count><Samples>loop.wav</Samples></SFXEvent>"
                                                        "<SFXEvent Name=\"Amb\"><Is_3D>Yes</Is_3D><Play_Count>-1</Play_Count><Samples>loop.wav</Samples></SFXEvent>"
                                                        "</SFXEvents>");
    write_file(root / "Data" / "XML" / "Units.xml", "<Units><GroundVehicle Name=\"Walker\"><SFXEvent_Engine_Moving_Loop>Hum</SFXEvent_Engine_Moving_Loop>"
                                                    "<SFXEvent_Engine_Idle_Loop>Idle</SFXEvent_Engine_Idle_Loop><SFXEvent_Ambient_Loop>Amb</SFXEvent_Ambient_Loop></GroundVehicle></Units>");
    write_file(root / "Data" / "Audio" / "SFX" / "loop.wav", text_of(make_wav(eaw::wav_format::pcm, 1, 16, pcm16({1, 2}))));
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    eaw::AssetCache cache;
    eaw::NullAudioDevice device;
    eaw::BattleAudio audio(vfs, &cache, device, "English", true);
    eaw::SimulationWorld world(0.1);
    const auto walker = *world.spawn({.type_name = "Walker", .owner = 1, .max_speed = 50.0F});
    const eaw::VisualCamera camera{{0.0F, -100.0F, 100.0F}, {0.0F, 0.0F, 0.0F}};
    world.step();
    audio.update(camera, world.snapshot(), world, 0.1F);
    require(device.played().size() == 2 && device.active_voices() == 2, "a still unit must play its idle engine and ambient loops");
    world.submit({eaw::SimCommandKind::move, {walker}, {1000.0F, 0.0F, 0.0F}});
    world.step();
    world.step();
    audio.update(camera, world.snapshot(), world, 0.1F);
    require(device.played().size() == 3 && device.active_voices() == 2, "a moving unit must swap its idle loop for its moving loop and keep its ambient loop");
    world.submit({eaw::SimCommandKind::destroy, {walker}});
    world.step();
    audio.update(camera, world.snapshot(), world, 0.1F);
    require(device.active_voices() == 0, "a unit's loops must stop when it is gone");
    std::filesystem::remove_all(root, ignored);
}

// The animation event table: rows as written (tabs or spaces, CR LF, a byte-order mark), their
// meaning (frames in hundredths, NONE left out, bad rows reported), lookup by stem; surface effects
// by trigger and surface with the default as fallback; and cues playing what they pass.
void test_animation_events()
{
    const auto bytes = [](const std::string_view text)
    {
        std::vector<std::byte> out;
        append_tag(out, text);
        return out;
    };
    const auto data =
        expect_ok(eaw::read_animation_sfx_map(bytes("\xEF\xBB\xBFSOUND\tanim_walk_00\t1050\tStep_Sound\r\n\r\n  SURFACE anim_walk_00 0 FOOT_PLANT B_Foot_R\nODD\r")), "reading the animation SFX map");
    require(data.rows.size() == 3 && data.rows[0].fields.size() == 4 && data.rows[0].fields[0] == "SOUND" && data.rows[1].line == 3 && data.rows[1].fields[4] == "B_Foot_R" &&
                data.rows[2].fields == std::vector<std::string>{"ODD"} && data.rows[2].line == 4,
            "rows must keep their fields and lines");
    require(!eaw::read_animation_sfx_map(bytes(std::string_view("SOUND\0x", 7))).ok(), "a NUL byte must be an error");

    eaw::AnimationSfxMapData table_data;
    table_data.rows = {{{"SOUND", "Anim_Walk_00", "1050", "Step_Sound"}, 1}, {{"SURFACE", "anim_walk_00", "0", "FOOT_PLANT", "B_Foot_R"}, 2},
                       {{"SOUND", "anim_walk_00", "200", "NONE"}, 3},        {{"SOUND", "anim_walk_00", "x", "Step_Sound"}, 4},
                       {{"SURFACE", "anim_walk_00", "10", "FOOT_PLANT"}, 5}, {{"ODD", "a", "b", "c"}, 6}};
    std::vector<std::string> warnings;
    const auto table = eaw::AnimationEventTable::build(table_data, &warnings);
    const auto events = table.find("DATA/ART/MODELS/ANIM_WALK_00.ALA");
    require(table.size() == 2 && events.size() == 2 && events[0].kind == eaw::AnimationEventKind::surface && events[0].frame == 0.0F && events[0].bone == "B_Foot_R" &&
                events[1].kind == eaw::AnimationEventKind::sound && events[1].frame == 10.5F && events[1].name == "Step_Sound",
            "events must be read in frame order, in hundredths of a frame, found by stem");
    require(warnings.size() == 3 && warnings[0].find("line 4") != std::string::npos && warnings[2].find("line 6") != std::string::npos && table.find("other").empty(),
            "bad rows must be reported with their line, NONE left out silently");

    const auto root = std::filesystem::temp_directory_path() / "eawengine-animation-events-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    write_file(root / "Data" / "XML" / "SurfaceFX.xml", "<SurfaceEffects>"
                                                        "<SurfaceFX name=\"Foot_Plant\"><Surface_Settings name=\"default\"><SoundFX_Name>Step_Sound</SoundFX_Name></Surface_Settings>"
                                                        "<Surface_Settings name=\"Water\"><SoundFX_Name>Splash</SoundFX_Name><Decal_Name>NONE</Decal_Name></Surface_Settings></SurfaceFX>"
                                                        "<SurfaceFX name=\"Track\"><Surface_Settings name=\"snow\"><Decal_Name>Tracks</Decal_Name></Surface_Settings></SurfaceFX>"
                                                        "</SurfaceEffects>");
    write_file(root / "Data" / "XML" / "SFXEvents.xml", "<SFXEvents><SFXEvent Name=\"Step_Sound\"><Is_3D>Yes</Is_3D><Samples>step.wav</Samples></SFXEvent></SFXEvents>");
    write_file(root / "Data" / "Audio" / "SFX" / "step.wav", text_of(make_wav(eaw::wav_format::pcm, 1, 16, pcm16({1, 2}))));
    write_file(root / "Data" / "XML" / "AnimationSFXMaps.txt", "SOUND\tanim_walk_00\t1050\tStep_Sound\nSURFACE\tanim_walk_00\t0\tFOOT_PLANT\tB_Foot_R\n");
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    const auto surfaces = eaw::SurfaceEffectCatalog::build(eaw::XmlAssetDatabase::build(vfs));
    require(surfaces.size() == 2 && surfaces.find("foot_plant")->sound == "Step_Sound" && surfaces.find("FOOT_PLANT", "water")->sound == "Splash" &&
                surfaces.find("FOOT_PLANT", "water")->decal.empty() && surfaces.find("Foot_Plant", "grass")->surface == "default" && surfaces.find("Track") == nullptr &&
                surfaces.find("Undefined") == nullptr,
            "surface effects must be found by trigger and surface, falling back to the default, with NONE empty");

    eaw::AssetCache cache;
    eaw::NullAudioDevice device;
    eaw::BattleAudio audio(vfs, &cache, device, "English", true);
    const std::vector<eaw::AnimationCue> cues{{{}, {}, "DATA/ART/MODELS/ANIM_WALK_00.ALA", -1.0, 5.0}, {{}, {}, "DATA/ART/MODELS/ANIM_WALK_00.ALA", 5.0, 10.5}};
    audio.play_animation_cues(cues);
    require(device.played().size() == 2, "a cue must play the events it passes: the foot plant's surface sound at frame 0, the sound at 10.5");
    audio.play_animation_cues(std::vector<eaw::AnimationCue>{{{}, {}, "anim_walk_00", 0.0, 10.0}});
    require(device.played().size() == 2, "a cue must not play events at or before its start or after its end");
    std::filesystem::remove_all(root, ignored);
}

// Shot impacts play their projectile's detonation (the armor-reduced one when armor took part,
// nothing for shots that lapsed); a moving unit plays its SFXEvent_Ambient_Moving every
// min..max seconds; a guard order gets the SFXEvent_Guard response.
void test_impact_and_moving_sounds()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-impact-sounds-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    write_file(root / "Data" / "XML" / "SFXEvents.xml", "<SFXEvents>"
                                                        "<SFXEvent Name=\"Boom\"><Is_3D>Yes</Is_3D><Samples>a.wav</Samples></SFXEvent>"
                                                        "<SFXEvent Name=\"Thud\"><Is_3D>Yes</Is_3D><Samples>a.wav</Samples></SFXEvent>"
                                                        "<SFXEvent Name=\"Chirp\"><Is_3D>Yes</Is_3D><Samples>a.wav</Samples></SFXEvent>"
                                                        "<SFXEvent Name=\"On_Guard\"><Samples>a.wav</Samples></SFXEvent>"
                                                        "<SFXEvent Name=\"Low_Line\"><Samples>a.wav</Samples></SFXEvent>"
                                                        "<SFXEvent Name=\"Critical_Line\"><Samples>a.wav</Samples></SFXEvent>"
                                                        "<SFXEvent Name=\"Spinning\"><Is_3D>Yes</Is_3D><Samples>a.wav</Samples></SFXEvent>"
                                                        "<SFXEvent Name=\"Talk_A\"><Overlap_Test>Talk</Overlap_Test><Samples>a.wav</Samples></SFXEvent>"
                                                        "<SFXEvent Name=\"Talk_B\"><Overlap_Test>Talk</Overlap_Test><Samples>a.wav</Samples></SFXEvent>"
                                                        "</SFXEvents>");
    write_file(root / "Data" / "XML" / "Units.xml",
               "<Units>"
               "<Projectile Name=\"Proj_Base\"><Projectile_SFXEvent_Detonate>Boom</Projectile_SFXEvent_Detonate></Projectile>"
               "<Projectile Name=\"Proj_Armored\"><Variant_Of_Existing_Type>Proj_Base</Variant_Of_Existing_Type>"
               "<Projectile_SFXEvent_Detonate_Reduced_By_Armor>Thud</Projectile_SFXEvent_Detonate_Reduced_By_Armor></Projectile>"
               "<GroundVehicle Name=\"Walker\"><Behavior>IDLE, AMBIENT_SFX</Behavior><SFXEvent_Ambient_Moving>Chirp</SFXEvent_Ambient_Moving>"
               "<SFXEvent_Ambient_Moving_Min_Delay_Seconds>1"
               "</SFXEvent_Ambient_Moving_Min_Delay_Seconds><SFXEvent_Ambient_Moving_Max_Delay_Seconds>1</SFXEvent_Ambient_Moving_Max_Delay_Seconds>"
               "<SFXEvent_Guard>On_Guard</SFXEvent_Guard><SFXEvent_Assist_Move>On_Guard</SFXEvent_Assist_Move></GroundVehicle>"
               "<GroundVehicle Name=\"Leader\"><SFXEvent_Move>Talk_A</SFXEvent_Move></GroundVehicle>"
               "<GroundVehicle Name=\"Follower\"><SFXEvent_Assist_Move>Talk_B</SFXEvent_Assist_Move></GroundVehicle>"
               "<GroundVehicle Name=\"Other_Leader\"><SFXEvent_Move>Chirp</SFXEvent_Move></GroundVehicle>"
               "<HeroUnit Name=\"Jetpacker\"><Secondary_Locomotor_Name>JETPACK_LOCOMOTOR</Secondary_Locomotor_Name>"
               "<SFXEvent_Engine_Moving_Loop>Chirp</SFXEvent_Engine_Moving_Loop></HeroUnit>"
               "<GroundVehicle Name=\"Mute\"><SFXEvent_Ambient_Moving>Chirp</SFXEvent_Ambient_Moving><SFXEvent_Ambient_Moving_Min_Delay_Seconds>1"
               "</SFXEvent_Ambient_Moving_Min_Delay_Seconds><SFXEvent_Ambient_Moving_Max_Delay_Seconds>1</SFXEvent_Ambient_Moving_Max_Delay_Seconds></GroundVehicle>"
               "<HeroUnit Name=\"Hero\"><SFXEvent_Health_Low_Warning>Low_Line</SFXEvent_Health_Low_Warning>"
               "<SFXEvent_Health_Critical_Warning>Critical_Line</SFXEvent_Health_Critical_Warning>"
               "<Spin_Away_On_Death_SFXEvent_Start_Die>Spinning</Spin_Away_On_Death_SFXEvent_Start_Die><SFXEvent_Group_Move>Low_Line</SFXEvent_Group_Move>"
               "<SFXEvent_Hardpoint_Destroyed>HARD_POINT_ENGINE, </SFXEvent_Hardpoint_Destroyed>"
               "<SFXEvent_Hardpoint_Destroyed> HARD_POINT_WEAPON_LASER, Critical_Line </SFXEvent_Hardpoint_Destroyed></HeroUnit>"
               "</Units>");
    write_file(root / "Data" / "Audio" / "SFX" / "a.wav", text_of(make_wav(eaw::wav_format::pcm, 1, 16, pcm16({1, 2}))));
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    eaw::AssetCache cache;
    eaw::NullAudioDevice device;
    eaw::BattleAudio audio(vfs, &cache, device, "English", true);
    eaw::SimulationWorld world(0.1);
    eaw::SimSnapshot impacts;
    const auto base = eaw::game_object_name_crc("Proj_Base");
    const auto armored = eaw::game_object_name_crc("Proj_Armored");
    impacts.impacts = {{{}, {}, base, eaw::ImpactKind::hull},
                       {{}, {}, armored, eaw::ImpactKind::hull_reduced},
                       {{}, {}, base, eaw::ImpactKind::hull_reduced},
                       {{}, {}, base, eaw::ImpactKind::expired},
                       {{}, {}, 12345U, eaw::ImpactKind::hull}};
    audio.play_tick_events(impacts, world);
    require(audio.stats().played == 3 && audio.stats().missing == 0, "hits must play their detonation (reduced by armor when there is one), lapsed and unknown shots nothing");

    const auto walker = *world.spawn({.type_name = "Walker", .owner = 1, .max_speed = 10.0F});
    world.submit({eaw::SimCommandKind::move, {walker}, {1000.0F, 0.0F, 0.0F}});
    const eaw::VisualCamera camera{{0.0F, -100.0F, 100.0F}, {0.0F, 0.0F, 0.0F}};
    const auto played = audio.stats().played;
    for (int frame = 0; frame < 5; ++frame)
    {
        world.step();
        audio.update(camera, world.snapshot(), world, 0.5F);
    }
    require(audio.stats().played == played + 2, "a moving unit must play its ambient moving sound every second (two in five half-second frames after the first wait)");
    require(audio.play_response("Walker", eaw::UnitResponse::guard), "a guard order must get the type's guard response");
    require(audio.play_group_response("Hero", eaw::UnitResponse::move) && !audio.play_group_response("Hero", eaw::UnitResponse::attack) &&
                !audio.play_group_response("Walker", eaw::UnitResponse::move),
            "a group order must get a type's group line, and nothing without one");
    {
        // An assist line waits for its pause (with nothing else said).
        eaw::BattleAudio quiet_audio(vfs, &cache, device, "English", true);
        require(quiet_audio.play_assist_response("Walker", eaw::UnitResponse::move, 1.0F) && !quiet_audio.play_assist_response("Walker", eaw::UnitResponse::attack, 1.0F),
                "an assist line must be queued for a move, and nothing without one");
        eaw::SimulationWorld idle(0.1);
        quiet_audio.update(camera, idle.snapshot(), idle, 0.5F);
        const auto early = quiet_audio.stats().played;
        quiet_audio.update(camera, idle.snapshot(), idle, 0.6F);
        require(early == 0 && quiet_audio.stats().played == 1, "an assist line must start after its pause");
    }
    {
        // An assist line sharing the response's overlap name keeps its whole pause after the
        // response ends (the null device ends one-shots at its next update) instead of being dropped.
        eaw::BattleAudio talk(vfs, &cache, device, "English", true);
        eaw::SimulationWorld idle(0.1);
        require(talk.play_response("Leader", eaw::UnitResponse::move) && talk.play_assist_response("Follower", eaw::UnitResponse::move, 1.0F), "both lines must be accepted");
        // The response is heard for the first updates; the assist line plays only its pause after
        // that (at 1.0 s it would have started over the response and been dropped).
        float elapsed = 0.0F;
        while (talk.stats().played < 2 && elapsed < 5.0F)
        {
            talk.update(camera, idle.snapshot(), idle, 0.25F);
            elapsed += 0.25F;
        }
        require(talk.stats().played == 2 && talk.stats().skipped == 0 && elapsed > 1.0F, "an assist line must wait for the response to finish, then its pause");
    }
    {
        // The same when the response has another overlap name (or none): it is an answer to an order.
        eaw::BattleAudio talk(vfs, &cache, device, "English", true);
        eaw::SimulationWorld idle(0.1);
        require(talk.play_response("Other_Leader", eaw::UnitResponse::move) && talk.play_assist_response("Follower", eaw::UnitResponse::move, 1.0F), "both lines must be accepted");
        float elapsed = 0.0F;
        while (talk.stats().played < 2 && elapsed < 5.0F)
        {
            talk.update(camera, idle.snapshot(), idle, 0.25F);
            elapsed += 0.25F;
        }
        require(talk.stats().played == 2 && elapsed > 1.0F, "an assist line must wait for a response of any overlap name");
    }
    {
        // A type with a secondary locomotor (a jetpack) plays its moving loop only on it: walking is silent.
        eaw::BattleAudio jets(vfs, &cache, device, "English", true);
        eaw::SimulationWorld walk(0.1);
        const auto boba = *walk.spawn({.type_name = "Jetpacker", .owner = 1, .max_speed = 10.0F});
        walk.submit({eaw::SimCommandKind::move, {boba}, {1000.0F, 0.0F, 0.0F}});
        for (int frame = 0; frame < 3; ++frame)
        {
            walk.step();
            jets.update(camera, walk.snapshot(), walk, 0.5F);
        }
        require(jets.stats().played == 0, "a jetpack's moving loop must not play while the unit walks");
    }

    // Without AMBIENT_SFX among its behaviors, a moving unit plays no ambient sound.
    {
        eaw::BattleAudio quiet_audio(vfs, &cache, device, "English", true);
        eaw::SimulationWorld quiet(0.1);
        const auto mute = *quiet.spawn({.type_name = "Mute", .owner = 1, .max_speed = 10.0F});
        quiet.submit({eaw::SimCommandKind::move, {mute}, {1000.0F, 0.0F, 0.0F}});
        for (int frame = 0; frame < 5; ++frame)
        {
            quiet.step();
            quiet_audio.update(camera, quiet.snapshot(), quiet, 0.5F);
        }
        require(quiet_audio.stats().played == 0, "an ambient moving sound must need the AMBIENT_SFX behavior");
    }
    // Health warnings: once into the low band (33%), once into the critical band (10%); the spin sound.
    {
        eaw::SimulationWorld hurt(0.1);
        eaw::CombatProfile hero;
        hero.health = 100.0F;
        const auto unit = *hurt.spawn({.type_name = "Hero", .owner = 1, .combat = hero});
        const auto tick = [&](const float damage)
        {
            if (damage > 0.0F)
            {
                hurt.submit({.kind = eaw::SimCommandKind::damage, .entities = {unit}, .amount = damage});
            }
            hurt.step();
            const auto before = audio.stats().played;
            audio.play_tick_events(hurt.snapshot(), hurt);
            return audio.stats().played - before;
        };
        require(tick(0.0F) == 0 && tick(70.0F) == 1 && tick(0.0F) == 0 && tick(25.0F) == 1 && tick(0.0F) == 0, "a unit must warn once on falling to low health and once to critical");
        eaw::SimSnapshot spinning;
        spinning.spins = {{unit, {}}};
        const auto before = audio.stats().played;
        audio.play_tick_events(spinning, hurt);
        require(audio.stats().played == before + 1, "a unit spinning away must play its spin sound");
        eaw::SimSnapshot lost;
        lost.hardpoint_deaths = {{unit, 0, {}, 0U, 0U, eaw::game_object_name_crc("HARD_POINT_WEAPON_LASER")}, {unit, 1, {}, 0U, 0U, eaw::game_object_name_crc("HARD_POINT_ENGINE")}};
        const auto ahead = audio.stats().played;
        audio.play_tick_events(lost, hurt);
        require(audio.stats().played == ahead + 1, "a unit must report a lost hardpoint of a kind it has a line for, and nothing for an empty line");
    }
    std::filesystem::remove_all(root, ignored);
}

const ModuleRegistrar registrar("audio", {
                                             {"test_wav_reader", &test_wav_reader},
                                             {"test_sound_decoding", &test_sound_decoding},
                                             {"test_spatial_audio", &test_spatial_audio},
                                             {"test_null_audio_device", &test_null_audio_device},
                                             {"test_sound_archives", &test_sound_archives},
                                             {"test_sound_events", &test_sound_events},
                                             {"test_battle_audio", &test_battle_audio},
                                             {"test_unit_loops", &test_unit_loops},
                                             {"test_animation_events", &test_animation_events},
                                             {"test_impact_and_moving_sounds", &test_impact_and_moving_sounds},
                                         });

} // namespace
