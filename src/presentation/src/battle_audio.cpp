#include "presentation/battle_audio.hpp"

#include "core/log.hpp"
#include "core/string_utils.hpp"
#include "formats/xml_document.hpp"
#include "formats/xml_helpers.hpp"
#include "assets/game_object_catalog.hpp"
#include "assets/map_object_layout.hpp"
#include "assets/resource_loaders.hpp"
#include "assets/type_fields.hpp"
#include "battle/object_types.hpp"
#include "presentation/scene_presentation.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace eaw {
namespace {

constexpr std::string_view audio_settings_path = "DATA/XML/AUDIO.XML";
// Warnings of the animation event table logged one by one; the rest are counted.
constexpr std::size_t max_reported_warnings = 8;

float setting(const XmlNode& root, const std::string_view name, const float fallback)
{
    const auto* node = xml_child(root, name);
    if (node == nullptr)
    {
        return fallback;
    }
    const auto text = ascii_trim(node->text);
    float value = fallback;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && std::isfinite(value) ? value : fallback;
}

// The first value of a type's field through its variant chain, trimmed (empty when it has none).
std::string type_field(const GameObjectCatalog& catalog, const XmlDefinition& definition, const std::string_view field)
{
    const auto* node = catalog.field(definition, field).field;
    return node != nullptr ? std::string(ascii_trim(node->text)) : std::string{};
}

} // namespace

AudioModeSettings read_audio_mode_settings(const AssetReader& reader, const bool land)
{
    AudioModeSettings settings;
    const auto bytes = reader.read(audio_settings_path);
    const auto xml = bytes ? read_xml(*bytes) : Result<XmlData>(bytes.error());
    if (!xml || xml->roots.empty())
    {
        log_warning("audio", make_error(std::string(audio_settings_path) + " could not be read; default 3D settings apply"));
        return settings;
    }
    const auto& root = xml->roots.front();
    const std::string mode = land ? "Land" : "Space";
    settings.saturation_scale = setting(root, "Audio_" + mode + "_3D_Saturation_Distance_Mod", settings.saturation_scale);
    settings.rolloff_scale = setting(root, "Audio_" + mode + "_3D_Rolloff_Distance_Mod", settings.rolloff_scale);
    settings.listener_height = setting(root, "Audio_" + mode + "_3D_Listener_Z_Pullback_Dist", settings.listener_height);
    return settings;
}

BattleAudio::BattleAudio(const AssetReader& reader, AssetCache* cache, AudioDevice& device, std::string language, const bool land, const std::uint64_t seed)
    : reader_(reader), cache_(cache), device_(device), language_(std::move(language)), settings_(read_audio_mode_settings(reader, land)), land_(land), random_(seed)
{
    const auto entry = ResourceManager(reader, cache).get<CachedCatalog>(xml_catalog_path);
    events_ = SoundEventCatalog::build(*entry->database);
    catalog_ = &entry->catalog;
    catalog_owner_ = entry;
    surface_effects_ = SurfaceEffectCatalog::build(*entry->database);
    std::vector<std::string> warnings;
    animation_events_ = load_animation_events(reader, &warnings);
    for (std::size_t index = 0; index < std::min(warnings.size(), max_reported_warnings); ++index)
    {
        log_warning("audio", warnings[index]);
    }
    if (warnings.size() > max_reported_warnings)
    {
        log_warning("audio", std::to_string(warnings.size() - max_reported_warnings) + " more animation event warnings");
    }
    for (const auto& event : events_.events())
    {
        event_names_.emplace(game_object_name_crc(event.name), event.name);
    }
    for (const auto& definition : entry->database->definitions())
    {
        if (ascii_equal_ignoring_case(definition.type, "Projectile"))
        {
            projectile_names_.emplace(game_object_name_crc(definition.name), definition.name);
        }
    }
}

BattleAudio::~BattleAudio()
{
    for (const auto& voice : voices_)
    {
        device_.stop(voice.id);
    }
}

const BattleAudio::TypeSounds& BattleAudio::type_sounds(const std::string_view type_name)
{
    auto key = ascii_lower(type_name);
    if (const auto found = type_sounds_.find(key); found != type_sounds_.end())
    {
        return found->second;
    }
    TypeSounds sounds;
    const auto definitions = catalog_->find(type_name);
    if (!definitions.empty())
    {
        const auto& definition = *definitions.front();
        sounds.death = type_field(*catalog_, definition, "Death_SFXEvent_Start_Die");
        sounds.death_explosion = type_field(*catalog_, definition, "Death_Explosion_SFXEvent");
        // A type with a secondary locomotor (jetpacks: Boba Fett, the Dark Troopers) plays its moving
        // loop only on it (a developer comment: Boba's loop is for flying with his jet pack); the
        // engine has no secondary locomotor yet, so the loop stays silent.
        if (type_field(*catalog_, definition, "Secondary_Locomotor_Name").empty())
        {
            sounds.engine_loop = type_field(*catalog_, definition, "SFXEvent_Engine_Moving_Loop");
        }
        sounds.engine_idle_loop = type_field(*catalog_, definition, "SFXEvent_Engine_Idle_Loop");
        sounds.ambient_loop = type_field(*catalog_, definition, "SFXEvent_Ambient_Loop");
        sounds.turret_loop = type_field(*catalog_, definition, "SFXEvent_Turret_Rotating_Loop");
        sounds.select = type_field(*catalog_, definition, "SFXEvent_Select");
        sounds.move = type_field(*catalog_, definition, "SFXEvent_Move");
        sounds.attack = type_field(*catalog_, definition, "SFXEvent_Attack");
        sounds.guard = type_field(*catalog_, definition, "SFXEvent_Guard");
        const auto behaves = [&](const std::string_view behavior)
        {
            for (const auto* field : {"Behavior", "LandBehavior", "SpaceBehavior"})
            {
                if (const auto* list = catalog_->field(definition, field).field)
                {
                    for (const auto name : split_list(list->text))
                    {
                        if (ascii_equal_ignoring_case(name, behavior))
                        {
                            return true;
                        }
                    }
                }
            }
            return false;
        };
        if (behaves("AMBIENT_SFX"))
        {
            sounds.ambient_moving = type_field(*catalog_, definition, "SFXEvent_Ambient_Moving");
        }
        sounds.health_low = type_field(*catalog_, definition, "SFXEvent_Health_Low_Warning");
        sounds.health_critical = type_field(*catalog_, definition, "SFXEvent_Health_Critical_Warning");
        if (land_)
        {
            sounds.damage_steps = type_damage_states(*catalog_, definition);
            for (const auto item : TypeFields(*catalog_, definition).list("Land_Damage_SFX"))
            {
                sounds.damage_sounds.emplace_back(ascii_equal_ignoring_case(item, "null") ? std::string_view{} : item);
            }
        }
        sounds.spin = type_field(*catalog_, definition, "Spin_Away_On_Death_SFXEvent_Start_Die");
        sounds.group_move = type_field(*catalog_, definition, "SFXEvent_Group_Move");
        sounds.group_attack = type_field(*catalog_, definition, "SFXEvent_Group_Attack");
        sounds.assist_move = type_field(*catalog_, definition, "SFXEvent_Assist_Move");
        sounds.assist_attack = type_field(*catalog_, definition, "SFXEvent_Assist_Attack");
        for (const auto* entry : catalog_->fields(definition, "SFXEvent_Hardpoint_Destroyed").fields)
        {
            const auto items = split_list(entry->text);
            const auto kind = items.empty() ? std::string_view{} : items[0];
            const auto event = items.size() < 2U ? std::string_view{} : items[1];
            if (!kind.empty() && !event.empty())
            {
                sounds.hardpoint_lost.emplace_back(game_object_name_crc(kind), std::string(event));
            }
        }
        sounds.ambient_moving_min = std::max(0.0F, type_float_field(*catalog_, definition, "SFXEvent_Ambient_Moving_Min_Delay_Seconds"));
        sounds.ambient_moving_max = std::max(sounds.ambient_moving_min, type_float_field(*catalog_, definition, "SFXEvent_Ambient_Moving_Max_Delay_Seconds"));
    }
    return type_sounds_.emplace(std::move(key), std::move(sounds)).first->second;
}

SpatialGain BattleAudio::place(const SoundEvent& event, const std::optional<Vector3>& position) const
{
    if (!event.positional || !position)
    {
        return {};
    }
    const float saturation = (event.saturation_distance > 0.0F ? event.saturation_distance : default_saturation_distance) * settings_.saturation_scale;
    return spatialize(listener_, *position, saturation, base_rolloff * settings_.rolloff_scale);
}

VoiceId BattleAudio::start(const SoundEvent& event, const std::optional<Vector3> position, const std::uint64_t entity)
{
    const auto active = [&](const auto& matches)
    {
        return static_cast<std::size_t>(std::ranges::count_if(voices_, matches));
    };
    // At its instance limit an event's newest play replaces its oldest one-shot (an inference: a
    // weapon firing faster than its sample lasts is heard on every shot, as the shots are seen);
    // a unit loop never replaces another unit's, so loops at the limit wait.
    std::optional<std::size_t> replaced;
    if (event.max_instances > 0 && active(
                                       [&](const Voice& voice)
                                       {
                                           return voice.event == &event;
                                       }) >= event.max_instances)
    {
        const auto oldest = std::ranges::find_if(voices_,
                                                 [&](const Voice& voice)
                                                 {
                                                     return voice.event == &event && voice.entity == 0;
                                                 });
        if (entity != 0 || oldest == voices_.end())
        {
            ++stats_.skipped;
            return 0;
        }
        replaced = static_cast<std::size_t>(oldest - voices_.begin());
    }
    // Plays sharing an overlap name (one unit type's responses) do not talk over each other.
    if (overlap_busy(event))
    {
        ++stats_.skipped;
        return 0;
    }
    if (event.samples.empty())
    {
        ++stats_.missing;
        return 0;
    }
    std::size_t pick = 0;
    if (event.sequential)
    {
        auto& next = next_sample_[&event];
        pick = next % event.samples.size();
        next = pick + 1U;
    }
    else
    {
        pick = random_.below(static_cast<std::uint32_t>(event.samples.size()));
    }
    const auto path = resolve_sound_sample(reader_, event.samples[pick], language_);
    if (!path)
    {
        ++stats_.missing;
        return 0;
    }
    const auto sound = ResourceManager(reader_, cache_).get<Sound>(*path);
    if (!sound->ok())
    {
        ++stats_.missing;
        log_warning("audio", sound->error());
        return 0;
    }
    // The Sound shares the cache entry's lifetime.
    const std::shared_ptr<const Sound> samples(sound, &sound->value());
    const float volume = random_.range(std::min(event.min_volume, event.max_volume), std::max(event.min_volume, event.max_volume));
    const float pitch = random_.range(std::min(event.min_pitch, event.max_pitch), std::max(event.min_pitch, event.max_pitch));
    const auto gain = place(event, position);
    const auto category = event.unit_response ? AudioCategory::voice : event.positional ? AudioCategory::effects : AudioCategory::interface;
    if (replaced)
    {
        device_.stop(voices_[*replaced].id);
        voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(*replaced));
        ++stats_.replaced;
    }
    const auto voice = device_.play({samples, category, volume * gain.volume, pitch, gain.pan, event.loop});
    if (voice == 0)
    {
        ++stats_.skipped;
        return 0;
    }
    ++stats_.played;
    voices_.push_back({voice, &event, position, volume, entity});
    return voice;
}

bool BattleAudio::play_event(const std::string_view name, const std::optional<Vector3> position)
{
    const auto* event = events_.find(name);
    if (event == nullptr)
    {
        ++stats_.missing;
        return false;
    }
    if (event->probability < 1.0F && random_.unit() >= event->probability)
    {
        ++stats_.skipped;
        return false;
    }
    if (event->max_predelay > 0.0F)
    {
        pending_.push_back({random_.range(event->min_predelay, std::max(event->min_predelay, event->max_predelay)), event, position});
        return true;
    }
    return start(*event, position, 0) != 0;
}

void BattleAudio::play_animation_cues(const std::span<const AnimationCue> cues)
{
    visit_cue_events(animation_events_, cues,
                     [&](const AnimationCue& cue, const AnimationEvent& event)
                     {
                         if (event.kind == AnimationEventKind::sound)
                         {
                             stats_.from_animations += play_event(event.name, cue.position) ? 1U : 0U;
                         }
                         else if (const auto* surface = surface_effects_.find(event.name); surface != nullptr && !surface->sound.empty())
                         {
                             stats_.from_animations += play_event(surface->sound, cue.position) ? 1U : 0U;
                         }
                     });
}

bool BattleAudio::play_response(const std::string_view type_name, const UnitResponse response)
{
    const auto& sounds = type_sounds(type_name);
    const auto& event = response == UnitResponse::select ? sounds.select : response == UnitResponse::move ? sounds.move : response == UnitResponse::guard ? sounds.guard : sounds.attack;
    return !event.empty() && play_response_event(event);
}

bool BattleAudio::play_response_event(const std::string& name)
{
    const auto* event = events_.find(name);
    if (event == nullptr)
    {
        ++stats_.missing;
        return false;
    }
    if (event->probability < 1.0F && random_.unit() >= event->probability)
    {
        ++stats_.skipped;
        return false;
    }
    if (event->max_predelay > 0.0F)
    {
        Pending pending{random_.range(event->min_predelay, std::max(event->min_predelay, event->max_predelay)), event, std::nullopt};
        pending.response = true;
        pending_.push_back(pending);
        return true;
    }
    const auto voice = start(*event, std::nullopt, 0);
    if (const auto found = std::ranges::find(voices_, voice, &Voice::id); voice != 0 && found != voices_.end())
    {
        found->response = true;
    }
    return voice != 0;
}

bool BattleAudio::response_busy() const
{
    return std::ranges::any_of(voices_, &Voice::response) || std::ranges::any_of(pending_,
                                                                                 [](const Pending& pending)
                                                                                 {
                                                                                     return pending.response && pending.pause <= 0.0F;
                                                                                 });
}

bool BattleAudio::play_group_response(const std::string_view type_name, const UnitResponse response)
{
    const auto& sounds = type_sounds(type_name);
    const auto& event = response == UnitResponse::move ? sounds.group_move : response == UnitResponse::attack ? sounds.group_attack : std::string{};
    return !event.empty() && play_response_event(event);
}

bool BattleAudio::play_assist_response(const std::string_view type_name, const UnitResponse response, const float pause)
{
    const auto& sounds = type_sounds(type_name);
    const auto& name = response == UnitResponse::move ? sounds.assist_move : response == UnitResponse::attack ? sounds.assist_attack : std::string{};
    if (name.empty())
    {
        return false;
    }
    const auto* event = events_.find(name);
    if (event == nullptr)
    {
        ++stats_.missing;
        return false;
    }
    pending_.push_back({pause, event, std::nullopt, pause, assist_patience_seconds, true});
    return true;
}

bool BattleAudio::overlap_busy(const SoundEvent& event) const
{
    return !event.overlap_test.empty() && std::ranges::any_of(voices_,
                                                              [&](const Voice& voice)
                                                              {
                                                                  return ascii_equal_ignoring_case(voice.event->overlap_test, event.overlap_test);
                                                              });
}

void BattleAudio::play_tick_events(const SimSnapshot& snapshot, const SimulationWorld& world)
{
    for (const auto& entity : snapshot.entities)
    {
        const auto key = entity_key(entity.id);
        auto known = entity_sounds_.find(key);
        if (known == entity_sounds_.end())
        {
            known = entity_sounds_.emplace(key, &type_sounds(world.type_name(entity.id))).first;
        }
    }
    // Every weapon's shots play its sound (a hardpoint's Fire_SFXEvent, else the type's
    // SFXEvent_Fire); the pulses of one tick that share a shooter and a sound play once.
    std::vector<std::pair<std::uint64_t, std::uint32_t>> fired;
    for (const auto& shot : snapshot.shots)
    {
        const auto source = std::pair(entity_key(shot.shooter), shot.sound);
        if (shot.sound == 0 || std::ranges::find(fired, source) != fired.end())
        {
            continue;
        }
        fired.push_back(source);
        if (const auto event = event_names_.find(shot.sound); event != event_names_.end())
        {
            (void)play_event(event->second, shot.origin);
        }
        else
        {
            ++stats_.missing;
        }
    }
    // Shots that hit, and death projectiles where they go off, play their projectile's detonation: the
    // armor-reduced one when armor took part (falling back to the plain one); shields take the plain one too. Shots that flew their range
    // out play nothing (an engine choice: whether a lapsed shot detonates is not known).
    for (const auto& impact : snapshot.impacts)
    {
        if (impact.kind == ImpactKind::expired)
        {
            continue;
        }
        if (impact.kind == ImpactKind::ground)
        {
            // A miss that met the ground: its ground detonation sound and its SurfaceFX's sound for
            // the surface there (a ricochet on ground; the stock liquids name none).
            if (const auto* sounds = projectile_sounds(impact.visual))
            {
                if (!sounds->ground.empty())
                {
                    (void)play_event(sounds->ground, impact.position);
                }
                const auto liquid = world.ground().liquid_at(impact.position.x, impact.position.y);
                if (const auto* surface = surface_effects_.find(sounds->ground_surface_fx, liquid ? to_string(*liquid) : std::string_view{"default"}); surface != nullptr && !surface->sound.empty())
                {
                    (void)play_event(surface->sound, impact.position);
                }
            }
            continue;
        }
        if (const auto* sounds = projectile_sounds(impact.visual))
        {
            const auto& name = impact.kind == ImpactKind::hull_reduced && !sounds->reduced.empty() ? sounds->reduced : sounds->detonate;
            if (!name.empty())
            {
                (void)play_event(name, impact.position);
            }
        }
    }
    // Destroyed hardpoints play their Death_Explosion_SFXEvent (every stock use is on HardPoint
    // definitions, none on the unit types).
    for (const auto& fallen : snapshot.hardpoint_deaths)
    {
        // The owner reports the loss in its own voice (every owner until the player layer exists).
        if (world.alive(fallen.entity))
        {
            const auto& sounds = type_sounds(world.type_name(fallen.entity));
            if (const auto line = std::ranges::find(sounds.hardpoint_lost, fallen.type_key, &std::pair<std::uint32_t, std::string>::first); line != sounds.hardpoint_lost.end())
            {
                (void)play_event(line->second, fallen.position);
            }
        }
        if (fallen.sound == 0)
        {
            continue;
        }
        if (const auto event = event_names_.find(fallen.sound); event != event_names_.end())
        {
            (void)play_event(event->second, fallen.position);
        }
        else
        {
            ++stats_.missing;
        }
    }
    // Units that start spinning away play their spin sound.
    for (const auto& spin : snapshot.spins)
    {
        if (const auto& sounds = type_sounds(world.type_name(spin.id)); !sounds.spin.empty())
        {
            (void)play_event(sounds.spin, spin.position);
        }
    }
    // A unit's own voice when its health falls into the low or the critical band (each band once on
    // the way down; healing above it lets it play again).
    for (const auto& entity : snapshot.entities)
    {
        // Spinning (dying) units have no health left to warn about.
        if (entity.max_health <= 0.0F || entity.health <= 0.0F)
        {
            continue;
        }
        const auto fraction = entity.health / entity.max_health;
        const std::uint8_t band = fraction <= health_critical_fraction ? 2U : fraction <= health_low_fraction ? 1U : 0U;
        auto& previous = health_bands_[entity_key(entity.id)];
        if (band > previous)
        {
            const auto& sounds = type_sounds(world.type_name(entity.id));
            const auto& event = band == 2U ? sounds.health_critical : sounds.health_low;
            if (!event.empty())
            {
                (void)play_event(event, entity.position);
            }
        }
        previous = band;
        // Land_Damage_SFX: the sound of a damage state the unit enters on the way down (first sight
        // only records where it is).
        if (const auto& sounds = type_sounds(world.type_name(entity.id)); !sounds.damage_steps.empty())
        {
            const auto step = damage_step(sounds.damage_steps, fraction);
            const auto current = step ? *step + 1U : 0U;
            const auto [seen, first] = damage_steps_.try_emplace(entity_key(entity.id), current);
            if (!first && current > seen->second && *step < sounds.damage_sounds.size() && !sounds.damage_sounds[*step].empty())
            {
                (void)play_event(sounds.damage_sounds[*step], entity.position);
            }
            seen->second = current;
        }
    }
    for (const auto& death : snapshot.deaths)
    {
        health_bands_.erase(entity_key(death.id));
        damage_steps_.erase(entity_key(death.id));
        const auto key = entity_key(death.id);
        if (const auto known = entity_sounds_.find(key); known != entity_sounds_.end())
        {
            for (const auto* event : {&known->second->death, &known->second->death_explosion})
            {
                if (!event->empty())
                {
                    (void)play_event(*event, death.position);
                }
            }
            entity_sounds_.erase(known);
        }
    }
}

const BattleAudio::ProjectileSounds* BattleAudio::projectile_sounds(const std::uint32_t visual)
{
    if (const auto known = projectile_sounds_.find(visual); known != projectile_sounds_.end())
    {
        return &known->second;
    }
    const auto name = projectile_names_.find(visual);
    if (name == projectile_names_.end())
    {
        return nullptr;
    }
    ProjectileSounds sounds;
    if (const auto definitions = catalog_->find(name->second); !definitions.empty())
    {
        sounds.detonate = type_field(*catalog_, *definitions.front(), "Projectile_SFXEvent_Detonate");
        sounds.reduced = type_field(*catalog_, *definitions.front(), "Projectile_SFXEvent_Detonate_Reduced_By_Armor");
        sounds.ground = type_field(*catalog_, *definitions.front(), "Projectile_Ground_Detonation_SFXEvent");
        sounds.ground_surface_fx = type_field(*catalog_, *definitions.front(), "Projectile_Ground_Detonation_SurfaceFX");
    }
    return &projectile_sounds_.emplace(visual, std::move(sounds)).first->second;
}

void BattleAudio::update_ambient_moving(const SimSnapshot& snapshot, const SimulationWorld& world, const float seconds)
{
    // Moving units with an SFXEvent_Ambient_Moving play it every min..max seconds, the first time
    // after such a wait; a unit that stops forgets its wait.
    std::erase_if(ambient_moving_due_,
                  [&](const auto& entry)
                  {
                      return std::ranges::none_of(snapshot.entities,
                                                  [&](const EntitySnapshot& entity)
                                                  {
                                                      return entity_key(entity.id) == entry.first && (entity.moving || entity.speed > 0.0F);
                                                  });
                  });
    for (const auto& entity : snapshot.entities)
    {
        if (!entity.moving && entity.speed <= 0.0F)
        {
            continue;
        }
        const auto key = entity_key(entity.id);
        auto known = entity_sounds_.find(key);
        if (known == entity_sounds_.end())
        {
            known = entity_sounds_.emplace(key, &type_sounds(world.type_name(entity.id))).first;
        }
        const auto& sounds = *known->second;
        if (sounds.ambient_moving.empty() || sounds.ambient_moving_max <= 0.0F)
        {
            continue;
        }
        const auto [due, added] = ambient_moving_due_.try_emplace(key, random_.range(sounds.ambient_moving_min, sounds.ambient_moving_max));
        due->second -= added ? 0.0F : seconds;
        if (due->second <= 0.0F)
        {
            (void)play_event(sounds.ambient_moving, entity.position);
            due->second = random_.range(sounds.ambient_moving_min, sounds.ambient_moving_max);
        }
    }
}

void BattleAudio::update_unit_loops(const SimSnapshot& snapshot, const SimulationWorld& world)
{
    // Each unit's loops: its engine while it moves (SFXEvent_Engine_Moving_Loop) or idles
    // (SFXEvent_Engine_Idle_Loop), its ambient sound (SFXEvent_Ambient_Loop) either way, and its
    // turret motor while the turret turns (SFXEvent_Turret_Rotating_Loop; the XML comment: while it
    // turns to aim; judged between ticks, so frames between them keep the answer); the
    // nearest max_unit_loops of them play, nearest the listener first.
    struct Wanted
    {
        float range{};
        std::uint64_t entity{};
        const SoundEvent* event{};
        Vector3 position;
    };
    std::vector<Wanted> wanted;
    for (const auto& entity : snapshot.entities)
    {
        const auto key = entity_key(entity.id);
        auto known = entity_sounds_.find(key);
        if (known == entity_sounds_.end())
        {
            known = entity_sounds_.emplace(key, &type_sounds(world.type_name(entity.id))).first;
        }
        const auto& sounds = *known->second;
        const bool moving = entity.moving || entity.speed > 0.0F;
        const auto range = distance(entity.position, listener_.position);
        bool turret_turning = false;
        if (!sounds.turret_loop.empty())
        {
            auto& turret = turret_motion_[key];
            if (turret.tick != snapshot.tick)
            {
                turret.turning = turret.tick != 0 && std::abs(entity.turret_yaw - turret.yaw) > turret_turning_epsilon;
                turret.tick = snapshot.tick;
                turret.yaw = entity.turret_yaw;
            }
            turret_turning = turret.turning;
        }
        static const std::string no_loop;
        for (const auto* name : {moving ? &sounds.engine_loop : &sounds.engine_idle_loop, &sounds.ambient_loop, turret_turning ? &sounds.turret_loop : &no_loop})
        {
            if (const auto* event = name->empty() ? nullptr : events_.find(*name))
            {
                wanted.push_back({range, key, event, entity.position});
            }
        }
    }
    // Turrets of entities gone from the snapshot are forgotten.
    std::erase_if(turret_motion_,
                  [&](const auto& entry)
                  {
                      return entry.second.tick != snapshot.tick;
                  });
    std::ranges::sort(wanted,
                      [](const Wanted& left, const Wanted& right)
                      {
                          if (left.range != right.range)
                          {
                              return left.range < right.range;
                          }
                          return left.entity != right.entity ? left.entity < right.entity : left.event->name < right.event->name;
                      });
    wanted.resize(std::min(wanted.size(), max_unit_loops));
    const auto matches = [](const Voice& voice, const Wanted& entry)
    {
        return voice.entity == entry.entity && voice.event == entry.event;
    };
    // Loops no longer wanted (the unit stopped or started, died, or fell out of the nearest) stop.
    std::erase_if(voices_,
                  [&](const Voice& voice)
                  {
                      if (voice.entity == 0 || std::ranges::any_of(wanted,
                                                                   [&](const Wanted& entry)
                                                                   {
                                                                       return matches(voice, entry);
                                                                   }))
                      {
                          return false;
                      }
                      device_.stop(voice.id);
                      return true;
                  });
    for (const auto& entry : wanted)
    {
        const auto playing = std::ranges::find_if(voices_,
                                                  [&](const Voice& voice)
                                                  {
                                                      return matches(voice, entry);
                                                  });
        if (playing != voices_.end())
        {
            playing->position = entry.position;
            continue;
        }
        (void)start(*entry.event, entry.position, entry.entity);
    }
}

void BattleAudio::update(const VisualCamera& camera, const SimSnapshot& snapshot, const SimulationWorld& world, const float seconds)
{
    // The listener hovers above the camera's focus, facing where the camera looks.
    const Vector3 view{camera.target.x - camera.eye.x, camera.target.y - camera.eye.y, camera.target.z - camera.eye.z};
    listener_.position = {camera.target.x, camera.target.y, camera.target.z + settings_.listener_height};
    listener_.forward = normalize_or(view, Vector3{0.0F, 1.0F, 0.0F});
    listener_.up = camera.up;

    for (auto& pending : pending_)
    {
        pending.delay -= seconds;
        // An assist line keeps its pause after the answers it would talk over.
        if (pending.pause > 0.0F && (response_busy() || overlap_busy(*pending.event)))
        {
            pending.delay = pending.pause;
            pending.patience -= seconds;
        }
    }
    std::vector<Pending> due;
    std::erase_if(pending_,
                  [&](const Pending& pending)
                  {
                      if (pending.pause > 0.0F && pending.patience <= 0.0F)
                      {
                          ++stats_.skipped;
                          return true;
                      }
                      if (pending.delay > 0.0F)
                      {
                          return false;
                      }
                      due.push_back(pending);
                      return true;
                  });
    for (const auto& pending : due)
    {
        const auto voice = start(*pending.event, pending.position, 0);
        if (const auto found = std::ranges::find(voices_, voice, &Voice::id); pending.response && voice != 0 && found != voices_.end())
        {
            found->response = true;
        }
    }

    update_unit_loops(snapshot, world);
    update_ambient_moving(snapshot, world, seconds);

    // Finished voices end; their chained events start where they played.
    std::vector<std::pair<const SoundEvent*, std::optional<Vector3>>> chained;
    std::erase_if(voices_,
                  [&](const Voice& voice)
                  {
                      if (device_.playing(voice.id))
                      {
                          return false;
                      }
                      if (!voice.event->chained_event.empty())
                      {
                          chained.emplace_back(voice.event, voice.position);
                      }
                      return true;
                  });
    for (const auto& [event, position] : chained)
    {
        (void)play_event(event->chained_event, position);
    }
    for (const auto& voice : voices_)
    {
        if (voice.event->positional && voice.position)
        {
            const auto gain = place(*voice.event, voice.position);
            device_.update_voice(voice.id, voice.volume * gain.volume, gain.pan);
        }
    }
    device_.update();
}

const BattleAudio::Stats& BattleAudio::stats() const noexcept
{
    return stats_;
}

const SoundEventCatalog& BattleAudio::events() const noexcept
{
    return events_;
}

} // namespace eaw
