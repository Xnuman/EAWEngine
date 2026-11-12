#include "battle/battle_scripts.hpp"

#include "core/log.hpp"
#include "core/string_utils.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace eaw {
namespace {

// The engine objects scripts get in battles, and their methods (a method's id is its index).
enum ObjectKind : std::uint16_t
{
    game_object_kind,
    object_type_kind,
    player_kind,
    position_kind,
    block_kind,
};

enum class GameObjectMethod : std::size_t
{
    is_valid,
    get_id,
    get_type,
    get_owner,
    get_position,
    get_hull,
    move_to,
    attack_target,
    stop,
    event_object_in_range,
    cancel_event_object_in_range,
    service_wrapper,
    get_distance,
    is_category,
    get_shield,
    despawn,
    prevent_opportunity_fire,
    attack_move,
    guard_target,
    make_invulnerable,
    set_cannot_be_killed,
    suspend_locomotor,
    take_damage,
    teleport_and_face,
    teleport,
    play_sfx_event,
};
constexpr std::array<std::string_view, 26> game_object_methods{"Is_Valid",
                                                               "Get_ID",
                                                               "Get_Type",
                                                               "Get_Owner",
                                                               "Get_Position",
                                                               "Get_Hull",
                                                               "Move_To",
                                                               "Attack_Target",
                                                               "Stop",
                                                               "Event_Object_In_Range",
                                                               "Cancel_Event_Object_In_Range",
                                                               "Service_Wrapper",
                                                               "Get_Distance",
                                                               "Is_Category",
                                                               "Get_Shield",
                                                               "Despawn",
                                                               "Prevent_Opportunity_Fire",
                                                               "Attack_Move",
                                                               "Guard_Target",
                                                               "Make_Invulnerable",
                                                               "Set_Cannot_Be_Killed",
                                                               "Suspend_Locomotor",
                                                               "Take_Damage",
                                                               "Teleport_And_Face",
                                                               "Teleport",
                                                               "Play_SFX_Event"};
enum class ObjectTypeMethod : std::size_t
{
    get_name,
    is_hero,
    get_combat_rating,
};
constexpr std::array<std::string_view, 3> object_type_methods{"Get_Name", "Is_Hero", "Get_Combat_Rating"};
constexpr std::array<std::string_view, 1> player_methods{"Get_ID"};
enum class BlockMethod : std::size_t
{
    is_finished,
    result,
};
constexpr std::array<std::string_view, 2> block_methods{"IsFinished", "Result"};

constexpr std::array<ScriptObjectKind, 5> object_kinds{{
    {"GameObject", game_object_methods, true},
    {"ObjectType", object_type_methods, true},
    {"Player", player_methods, true},
    {"Position", {}, false},
    {"Block", block_methods, false},
}};

enum class Function : std::size_t
{
    find_object_type,
    find_first_object,
    find_all_objects_of_type,
    find_hint,
    find_all_objects_with_hint,
    get_game_mode,
};
constexpr std::array<std::string_view, 6> functions{"Find_Object_Type", "Find_First_Object", "Find_All_Objects_Of_Type", "Find_Hint", "Find_All_Objects_With_Hint", "Get_Game_Mode"};

// Blocks a script keeps; the oldest is reused after this many orders, and a block of a reused slot
// reads as finished. An engine choice: a script waits on few orders at a time.
constexpr std::uint32_t max_blocks = 4096;

// Squared distance in the ground plane: ranges are measured there, as for weapons (provisional).
float ground_distance_squared(const Vector3& a, const Vector3& b) noexcept
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

ScriptObject game_object(const EntityId id)
{
    return {game_object_kind, id.index, id.generation, {}};
}

EntityId entity_of(const ScriptObject& object)
{
    return {object.index, object.generation};
}

} // namespace

// --- BattleScriptView ---

std::uint32_t BattleScriptView::intern_type(const Battle& battle, const std::string_view name)
{
    auto folded = ascii_lower(name);
    if (const auto found = type_index_.find(folded); found != type_index_.end())
    {
        return found->second;
    }
    const auto id = static_cast<std::uint32_t>(type_names_.size());
    type_names_.emplace_back(name);
    type_infos_.push_back(battle.type_info(name));
    type_entities_.emplace_back();
    type_index_.emplace(std::move(folded), id);
    return id;
}

void BattleScriptView::rebuild(const Battle& battle)
{
    const auto spawnable = battle.spawnable();
    for (; spawnable_seen_ < spawnable.size(); ++spawnable_seen_)
    {
        (void)intern_type(battle, spawnable[spawnable_seen_].type_name);
    }
    if (!hints_read_)
    {
        for (const auto& [id, hint] : battle.hinted_entities())
        {
            hints_.emplace_back(ascii_lower(hint), id);
        }
        hints_read_ = true;
    }
    snapshot_ = &battle.world().snapshot();
    land_ = battle.world().ground().valid();
    new_entities_.clear();
    std::ranges::fill(slot_positions_, 0U);
    for (auto& entities : type_entities_)
    {
        entities.clear();
    }
    const auto& entities = snapshot_->entities;
    for (std::size_t position = 0; position < entities.size(); ++position)
    {
        const auto id = entities[position].id;
        if (id.index >= slot_positions_.size())
        {
            slot_positions_.resize(id.index + 1U, 0U);
            slot_types_.resize(id.index + 1U, {0U, 0U});
        }
        slot_positions_[id.index] = static_cast<std::uint32_t>(position + 1U);
        if (slot_types_[id.index].first != id.generation)
        {
            slot_types_[id.index] = {id.generation, intern_type(battle, battle.world().type_name(id))};
            new_entities_.push_back(id);
        }
        type_entities_[slot_types_[id.index].second].push_back(static_cast<std::uint32_t>(position));
    }
}

const EntitySnapshot* BattleScriptView::entity(const EntityId id) const noexcept
{
    if (snapshot_ == nullptr || id.index >= slot_positions_.size() || slot_positions_[id.index] == 0)
    {
        return nullptr;
    }
    const auto& entity = snapshot_->entities[slot_positions_[id.index] - 1U];
    return entity.id == id ? &entity : nullptr;
}

std::optional<std::uint32_t> BattleScriptView::type_id(const std::string_view name) const
{
    if (const auto found = type_index_.find(ascii_lower(name)); found != type_index_.end())
    {
        return found->second;
    }
    return std::nullopt;
}

std::string_view BattleScriptView::type_name(const std::uint32_t type) const
{
    return type < type_names_.size() ? std::string_view(type_names_[type]) : std::string_view();
}

std::optional<std::uint32_t> BattleScriptView::type_of(const EntityId id) const noexcept
{
    if (entity(id) == nullptr)
    {
        return std::nullopt;
    }
    return slot_types_[id.index].second;
}

std::span<const std::uint32_t> BattleScriptView::entities_of_type(const std::uint32_t type) const noexcept
{
    return type < type_entities_.size() ? std::span<const std::uint32_t>(type_entities_[type]) : std::span<const std::uint32_t>();
}

const ScriptTypeInfo* BattleScriptView::type_info(const std::uint32_t type) const noexcept
{
    return type < type_infos_.size() ? type_infos_[type] : nullptr;
}

bool BattleScriptView::land() const noexcept
{
    return land_;
}

std::vector<EntityId> BattleScriptView::hinted(const std::string_view hint) const
{
    const auto folded = ascii_lower(hint);
    std::vector<EntityId> found;
    for (const auto& [name, id] : hints_)
    {
        if (name == folded && entity(id) != nullptr)
        {
            found.push_back(id);
        }
    }
    return found;
}

std::span<const EntityId> BattleScriptView::new_entities() const noexcept
{
    return new_entities_;
}

const SimSnapshot& BattleScriptView::snapshot() const noexcept
{
    return *snapshot_;
}

// --- BattleScriptContext: one script's provider and order buffer ---

class BattleScriptContext final : public ScriptObjectProvider
{
public:
    BattleScriptContext(const BattleScriptView& view, const std::optional<EntityId> object) noexcept : bound_object(object), view_(view)
    {}

    [[nodiscard]] std::span<const ScriptObjectKind> kinds() const noexcept override
    {
        return object_kinds;
    }
    [[nodiscard]] std::span<const std::string_view> functions() const noexcept override
    {
        return eaw::functions;
    }
    Status call_function(std::size_t function, ScriptCall& call) override;
    Status call_method(const ScriptObject& object, std::size_t method, ScriptCall& call) override;

    // Submits the buffered orders in the order they were given (script origin: not logged).
    void submit_orders(Battle& battle);
    // Moves the buffered sound requests, in the order they were made, to the end of `out`.
    void take_sounds(std::vector<ScriptSoundRequest>& out);

    std::unique_ptr<ScriptHost> host;
    // The game object the script belongs to (none for scripts not bound to one).
    std::optional<EntityId> bound_object;

private:
    // Event_Object_In_Range: queue `callback` with (self, other) for every object within `range`
    // when the registering object is serviced (Service_Wrapper), of the player's owner slot when one
    // was given as a real player.
    struct Proximity
    {
        EntityId self;
        std::size_t callback{};
        float range{};
        std::optional<std::uint32_t> owner;
    };
    struct Block
    {
        std::uint32_t generation{};
        EntityId entity;
        // Attack orders: the victim.
        EntityId target;
        BattleOrder kind{BattleOrder::move};
        // The order's serial once submitted (0 before).
        std::uint64_t serial{};
        // The battle refused the order, or it could never run (an immobile entity told to move).
        bool rejected{};
    };
    struct PendingOrder
    {
        BattleCommand command;
        std::optional<std::uint32_t> block;
    };

    Status call_game_object(const ScriptObject& object, GameObjectMethod method, ScriptCall& call);
    Status call_block(const ScriptObject& object, BlockMethod method, ScriptCall& call) const;
    // Buffers an order and returns its block.
    ScriptObject order(BattleCommand command, EntityId entity, bool rejected);
    // Where an order goes: a position, or a living game object (followed by id). Nothing, with a
    // warning, for a dead object or a value the bridge cannot use (a stub such as a task force).
    struct Destination
    {
        Vector3 point;
        EntityId object;
    };
    [[nodiscard]] std::optional<Destination> destination(ScriptCall& call, int argument, std::string_view method) const;
    // The type a function argument names: a type name or an ObjectType.
    [[nodiscard]] std::optional<std::uint32_t> type_argument(const ScriptCall& call, int argument) const;
    // Whether a block's order is over, and whether it ended as ordered (arrived, or the victim died).
    [[nodiscard]] std::pair<bool, bool> block_state(const ScriptObject& object) const;

    void service(EntityId self);

    const BattleScriptView& view_;
    std::vector<Proximity> proximities_;
    std::vector<PendingOrder> pending_;
    std::vector<ScriptSoundRequest> sounds_;
    std::vector<Block> blocks_;
    std::uint32_t next_block_{};
};

std::optional<std::uint32_t> BattleScriptContext::type_argument(const ScriptCall& call, const int argument) const
{
    if (const auto name = call.string(argument))
    {
        return view_.type_id(*name);
    }
    if (const auto object = call.object(argument); object && object->kind == object_type_kind)
    {
        return object->index;
    }
    return std::nullopt;
}

Status BattleScriptContext::call_function(const std::size_t function, ScriptCall& call)
{
    switch (static_cast<Function>(function))
    {
    case Function::find_object_type:
        // Types the battle knows (its spawnable types and the types of its entities); nil for others.
        if (const auto type = type_argument(call, 1))
        {
            call.return_object({object_type_kind, *type, 1, {}});
        }
        else
        {
            call.return_nil();
        }
        return {};
    case Function::find_first_object:
    {
        const auto type = type_argument(call, 1);
        const auto entities = type ? view_.entities_of_type(*type) : std::span<const std::uint32_t>();
        if (entities.empty())
        {
            call.return_nil();
            return {};
        }
        call.return_object(game_object(view_.snapshot().entities[entities.front()].id));
        return {};
    }
    case Function::find_all_objects_of_type:
    {
        // Find_All_Objects_Of_Type(type[, player]): the living entities of the type, of the player's
        // owner slot when one is given, in snapshot order.
        const auto type = type_argument(call, 1);
        const auto owner = call.object(2);
        std::vector<ScriptObject> found;
        for (const auto position : type ? view_.entities_of_type(*type) : std::span<const std::uint32_t>())
        {
            const auto& entity = view_.snapshot().entities[position];
            if (!owner || (owner->kind == player_kind && owner->index == entity.owner))
            {
                found.push_back(game_object(entity.id));
            }
        }
        call.return_objects(found);
        return {};
    }
    case Function::find_hint:
    {
        // Find_Hint(type, hint): the first living placed object of the type with the hint, or nil.
        const auto type = type_argument(call, 1);
        const auto hint = call.string(2);
        if (!type || !hint)
        {
            call.return_nil();
            return {};
        }
        for (const auto id : view_.hinted(*hint))
        {
            if (view_.type_of(id) == type)
            {
                call.return_object(game_object(id));
                return {};
            }
        }
        call.return_nil();
        return {};
    }
    case Function::find_all_objects_with_hint:
    {
        const auto hint = call.string(1);
        std::vector<ScriptObject> found;
        for (const auto id : hint ? view_.hinted(*hint) : std::vector<EntityId>{})
        {
            found.push_back(game_object(id));
        }
        call.return_objects(found);
        return {};
    }
    case Function::get_game_mode:
        // A tactical battle on a land or a space map; "Galactic" comes with galactic play.
        call.return_string(view_.land() ? "Land" : "Space");
        return {};
    }
    return make_error("unknown battle script function");
}

Status BattleScriptContext::call_method(const ScriptObject& object, const std::size_t method, ScriptCall& call)
{
    switch (object.kind)
    {
    case game_object_kind:
        return call_game_object(object, static_cast<GameObjectMethod>(method), call);
    case object_type_kind:
    {
        const auto* info = view_.type_info(object.index);
        switch (static_cast<ObjectTypeMethod>(method))
        {
        case ObjectTypeMethod::get_name:
            // The type name as the data spells it.
            call.return_string(view_.type_name(object.index));
            break;
        case ObjectTypeMethod::is_hero:
        {
            // No XML field says it; a hero category does (LandHero, SpaceHero, NonCombatHero), an
            // inference.
            const auto hero = [](const std::string& category)
            {
                return category.ends_with("hero");
            };
            call.return_boolean(info != nullptr && std::ranges::any_of(info->categories, hero));
            break;
        }
        case ObjectTypeMethod::get_combat_rating:
            call.return_number(info != nullptr ? info->combat_rating : 0.0);
            break;
        }
        return {};
    }
    case player_kind:
        // Get_ID: the owner slot (players are owner slots until the player layer exists).
        call.return_number(object.index);
        return {};
    case block_kind:
        return call_block(object, static_cast<BlockMethod>(method), call);
    default:
        return make_error("a method of an object without methods");
    }
}

Status BattleScriptContext::call_game_object(const ScriptObject& object, const GameObjectMethod method, ScriptCall& call)
{
    const auto id = entity_of(object);
    const auto* entity = view_.entity(id);
    if (method == GameObjectMethod::is_valid)
    {
        call.return_boolean(entity != nullptr);
        return {};
    }
    if (entity == nullptr)
    {
        // A dead or replaced object: queries give nil and orders are dropped (an engine choice; the
        // original's behavior is unknown).
        call.warn(std::string(game_object_methods[static_cast<std::size_t>(method)]) + " on a game object that no longer exists");
        call.return_nil();
        return {};
    }
    switch (method)
    {
    case GameObjectMethod::is_valid:
        break;
    case GameObjectMethod::get_id:
        // Unique among objects alive at once; exact while the slot generation stays below 2^21.
        call.return_number(static_cast<double>(entity->id.generation) * 4294967296.0 + static_cast<double>(entity->id.index));
        break;
    case GameObjectMethod::get_type:
        call.return_object({object_type_kind, *view_.type_of(id), 1, {}});
        break;
    case GameObjectMethod::get_owner:
        call.return_object({player_kind, entity->owner, 1, {}});
        break;
    case GameObjectMethod::get_position:
        call.return_object({position_kind, 0, 0, entity->position});
        break;
    case GameObjectMethod::get_hull:
        // The hull's share left (scripts compare it with 0.5); an entity without health reads 1.
        call.return_number(entity->max_health > 0.0F ? static_cast<double>(entity->health / entity->max_health) : 1.0);
        break;
    case GameObjectMethod::move_to:
    {
        // Move_To(target[, distance]): a position or an object's position. The distance is not
        // applied yet.
        const auto to = destination(call, 1, "Move_To");
        if (!to)
        {
            call.return_nil();
            break;
        }
        call.return_object(order({.order = BattleOrder::move, .entities = {id}, .point = to->point}, id, entity->max_speed <= 0.0F));
        break;
    }
    case GameObjectMethod::attack_target:
    {
        const auto target = call.object(1);
        if (!target || target->kind != game_object_kind)
        {
            return make_error("Attack_Target expects a game object");
        }
        if (view_.entity(entity_of(*target)) == nullptr)
        {
            call.warn("Attack_Target on a game object that no longer exists");
            call.return_nil();
            break;
        }
        call.return_object(order({.order = BattleOrder::attack, .entities = {id}, .target = entity_of(*target)}, id, false));
        break;
    }
    case GameObjectMethod::stop:
        (void)order({.order = BattleOrder::stop, .entities = {id}}, id, false);
        break;
    case GameObjectMethod::event_object_in_range:
    {
        // Event_Object_In_Range(handler, range[, player]). A filter that is not a real player (a
        // stub, while Find_Player is one) filters nothing.
        const auto callback = call.callback(1, "GameObject.Event_Object_In_Range");
        const auto range = call.number(2);
        if (!callback || !range)
        {
            return make_error("Event_Object_In_Range expects a function and a range");
        }
        const auto player = call.object(3);
        std::optional<std::uint32_t> owner;
        if (player && player->kind == player_kind)
        {
            owner = player->index;
        }
        std::erase_if(proximities_,
                      [&](const Proximity& proximity)
                      {
                          return proximity.self == id && proximity.callback == *callback;
                      });
        proximities_.push_back({id, *callback, static_cast<float>(*range), owner});
        break;
    }
    case GameObjectMethod::cancel_event_object_in_range:
    {
        const auto callback = call.callback(1, "GameObject.Event_Object_In_Range");
        std::erase_if(proximities_,
                      [&](const Proximity& proximity)
                      {
                          return proximity.self == id && (!callback || proximity.callback == *callback);
                      });
        break;
    }
    case GameObjectMethod::service_wrapper:
        service(id);
        break;
    case GameObjectMethod::get_distance:
    {
        // Get_Distance(object or position), in the ground plane like every range (provisional).
        const auto target = call.object(1);
        if (!target || (target->kind != position_kind && target->kind != game_object_kind))
        {
            return make_error("Get_Distance expects a position or a game object");
        }
        Vector3 point = target->point;
        if (target->kind == game_object_kind)
        {
            const auto* other = view_.entity(entity_of(*target));
            if (other == nullptr)
            {
                call.warn("Get_Distance to a game object that no longer exists");
                call.return_nil();
                break;
            }
            point = other->position;
        }
        call.return_number(std::sqrt(static_cast<double>(ground_distance_squared(entity->position, point))));
        break;
    }
    case GameObjectMethod::get_shield:
        // The shield's share left, like Get_Hull (scripts compare it with 0.33); none reads 0.
        call.return_number(entity->max_shield > 0.0F ? static_cast<double>(entity->shield / entity->max_shield) : 0.0);
        break;
    case GameObjectMethod::despawn:
        // The object leaves without dying (no death effects); in the next tick, like every order.
        (void)order({.order = BattleOrder::remove, .entities = {id}}, id, false);
        break;
    case GameObjectMethod::prevent_opportunity_fire:
    {
        // Prevent_Opportunity_Fire(true): fire only at ordered targets, the engine's hold_fire
        // stance; false returns to aggressive (a mapping onto engine stances, not the original's).
        const bool prevent = call.truthy(1);
        (void)order({.order = BattleOrder::set_stance, .entities = {id}, .stance = prevent ? Stance::hold_fire : Stance::aggressive}, id, false);
        break;
    }
    case GameObjectMethod::attack_move:
    case GameObjectMethod::guard_target:
    {
        // Attack_Move(destination[, threat]) and Guard_Target(anchor): a position, or an object that
        // is followed while it lives. The AI's threat argument is not used.
        const bool attack_move = method == GameObjectMethod::attack_move;
        const auto to = destination(call, 1, attack_move ? "Attack_Move" : "Guard_Target");
        if (!to)
        {
            call.return_nil();
            break;
        }
        const BattleCommand command{.order = attack_move ? BattleOrder::attack_move : BattleOrder::guard, .entities = {id}, .point = to->point, .target = to->object};
        call.return_object(order(command, id, attack_move && entity->max_speed <= 0.0F));
        break;
    }
    case GameObjectMethod::make_invulnerable:
    case GameObjectMethod::set_cannot_be_killed:
    case GameObjectMethod::suspend_locomotor:
    {
        // (true|false); applied in the next tick like every order.
        const auto condition = method == GameObjectMethod::make_invulnerable      ? BattleOrder::set_invulnerable
                               : method == GameObjectMethod::set_cannot_be_killed ? BattleOrder::set_cannot_be_killed
                                                                                  : BattleOrder::suspend_locomotor;
        (void)order({.order = condition, .entities = {id}, .flag = call.truthy(1)}, id, false);
        break;
    }
    case GameObjectMethod::take_damage:
    {
        // Take_Damage(amount): hull damage past shields and armor (stock scripts use it to kill:
        // 10000, 99999); an inference from those uses.
        const auto amount = call.number(1);
        if (!amount || !std::isfinite(*amount) || *amount < 0.0)
        {
            return make_error("Take_Damage expects a non-negative amount");
        }
        (void)order({.order = BattleOrder::damage, .entities = {id}, .amount = static_cast<float>(*amount)}, id, false);
        break;
    }
    case GameObjectMethod::teleport_and_face:
    case GameObjectMethod::teleport:
    {
        // Teleport_And_Face(object or position) takes an object's heading too (markers carry one);
        // Teleport only the place.
        const bool face = method == GameObjectMethod::teleport_and_face;
        const auto to = destination(call, 1, face ? "Teleport_And_Face" : "Teleport");
        if (!to)
        {
            break;
        }
        std::optional<float> heading;
        if (face && to->object.valid())
        {
            heading = view_.entity(to->object)->heading;
        }
        (void)order({.order = BattleOrder::teleport, .entities = {id}, .point = to->point, .heading = heading}, id, false);
        break;
    }
    case GameObjectMethod::play_sfx_event:
    {
        // Play_SFX_Event(name): the sound event at the object (presentation only).
        const auto name = call.string(1);
        if (!name || name->empty())
        {
            call.warn("Play_SFX_Event needs a sound event name");
            break;
        }
        sounds_.push_back({std::string(*name), id, entity->position});
        break;
    }
    case GameObjectMethod::is_category:
    {
        // Is_Category(name): whether the type's CategoryMask names it, ignoring case; a list
        // ("Fighter | Bomber") matches any of its names.
        const auto name = call.string(1);
        if (!name)
        {
            return make_error("Is_Category expects a category name");
        }
        const auto* info = view_.type_info(*view_.type_of(id));
        bool member = false;
        std::string part;
        for (const char character : std::string(*name) + "|")
        {
            if (character != '|')
            {
                part += character;
                continue;
            }
            const auto folded = ascii_lower(ascii_trim(part));
            member = member || (info != nullptr && std::ranges::find(info->categories, folded) != info->categories.end());
            part.clear();
        }
        call.return_boolean(member);
        break;
    }
    }
    return {};
}

void BattleScriptContext::service(const EntityId self)
{
    const auto* entity = view_.entity(self);
    for (const auto& proximity : proximities_)
    {
        if (proximity.self != self || entity == nullptr)
        {
            continue;
        }
        // Every object in range each time the object is serviced (the stock handlers cancel or
        // guard themselves); a scan of the snapshot, to be replaced by the spatial grid's query
        // when measured to matter.
        const float range_squared = proximity.range * proximity.range;
        for (const auto& other : view_.snapshot().entities)
        {
            if (other.id == self || (proximity.owner && other.owner != *proximity.owner) || ground_distance_squared(other.position, entity->position) > range_squared)
            {
                continue;
            }
            (void)host->queue_callback(proximity.callback, {ScriptValue::real_object(game_object(self), object_kinds[game_object_kind].name),
                                                            ScriptValue::real_object(game_object(other.id), object_kinds[game_object_kind].name)});
        }
    }
}

std::optional<BattleScriptContext::Destination> BattleScriptContext::destination(ScriptCall& call, const int argument, const std::string_view method) const
{
    const auto target = call.object(argument);
    if (target && target->kind == position_kind)
    {
        return Destination{target->point, {}};
    }
    if (target && target->kind == game_object_kind)
    {
        if (const auto* entity = view_.entity(entity_of(*target)))
        {
            return Destination{entity->position, entity->id};
        }
        call.warn(std::string(method) + " to a game object that no longer exists");
        return std::nullopt;
    }
    // While parts of the engine are stubs (task forces, planets), scripts hand them over; the order
    // is dropped rather than stopping the script.
    call.warn(std::string(method) + " needs a position or a game object");
    return std::nullopt;
}

ScriptObject BattleScriptContext::order(BattleCommand command, const EntityId entity, const bool rejected)
{
    if (blocks_.size() < max_blocks)
    {
        blocks_.emplace_back();
    }
    const auto index = next_block_;
    next_block_ = (next_block_ + 1U) % max_blocks;
    auto& block = blocks_[index];
    block = {block.generation + 1U, entity, command.target, command.order, 0, rejected};
    if (!rejected)
    {
        pending_.push_back({std::move(command), index});
    }
    return {block_kind, index, block.generation, {}};
}

std::pair<bool, bool> BattleScriptContext::block_state(const ScriptObject& object) const
{
    if (object.index >= blocks_.size() || blocks_[object.index].generation != object.generation)
    {
        // Its slot was reused: long over.
        return {true, false};
    }
    const auto& block = blocks_[object.index];
    if (block.rejected)
    {
        return {true, false};
    }
    if (block.serial == 0)
    {
        return {false, false};
    }
    const auto* entity = view_.entity(block.entity);
    if (entity == nullptr || entity->command_serial > block.serial)
    {
        // Dead, or a later order replaced this one.
        return {true, false};
    }
    if (entity->command_serial < block.serial)
    {
        return {false, false};
    }
    switch (block.kind)
    {
    case BattleOrder::attack:
    {
        const bool victim_alive = view_.entity(block.target) != nullptr;
        return {!victim_alive || entity->target != block.target, !victim_alive};
    }
    case BattleOrder::attack_move:
    {
        // Over once the standing order ended: arrived, or its object is gone.
        const bool over = entity->order == StandingOrder::none && !entity->moving;
        return {over, over};
    }
    case BattleOrder::guard:
        // A guard ends only when its object dies (or a later order replaces it).
        return {entity->order != StandingOrder::guard, false};
    default:
        return {!entity->moving, !entity->moving};
    }
}

Status BattleScriptContext::call_block(const ScriptObject& object, const BlockMethod method, ScriptCall& call) const
{
    const auto [finished, succeeded] = block_state(object);
    // IsFinished: the order is over; Result: it ended as ordered (an inference from how scripts
    // read it after BlockOnCommand).
    call.return_boolean(method == BlockMethod::is_finished ? finished : succeeded);
    return {};
}

void BattleScriptContext::take_sounds(std::vector<ScriptSoundRequest>& out)
{
    out.insert(out.end(), std::make_move_iterator(sounds_.begin()), std::make_move_iterator(sounds_.end()));
    sounds_.clear();
}

void BattleScriptContext::submit_orders(Battle& battle)
{
    for (auto& pending : pending_)
    {
        const auto serial = battle.submit(pending.command, BattleOrigin::script);
        if (pending.block)
        {
            blocks_[*pending.block].serial = serial ? *serial : 0;
            blocks_[*pending.block].rejected = !serial;
        }
    }
    pending_.clear();
}

// --- BattleScripts ---

BattleScripts::BattleScripts(const AssetReader& reader, ScriptHostOptions options) : reader_(reader), options_(std::move(options))
{}

BattleScripts::~BattleScripts() = default;

Result<std::size_t> BattleScripts::add(const Battle& battle, const std::string_view script, const std::optional<EntityId> object)
{
    view_.rebuild(battle);
    return start(script, object);
}

Result<std::size_t> BattleScripts::start(const std::string_view script, const std::optional<EntityId> object)
{
    auto context = std::make_unique<BattleScriptContext>(view_, object);
    auto options = options_;
    options.objects = context.get();
    EAW_TRY_ASSIGN(context->host, ScriptHost::create(reader_, options));
    auto& host = *context->host;
    EAW_TRY(host.load(script));
    EAW_TRY(host.run_definitions());
    if (object)
    {
        host.set_global("Object", ScriptValue::real_object(game_object(*object), object_kinds[game_object_kind].name));
    }
    if (host.has_function("main"))
    {
        EAW_TRY(host.start_thread("main"));
    }
    contexts_.push_back(std::move(context));
    ++stats_.started;
    return contexts_.size() - 1U;
}

void BattleScripts::run(Battle& battle)
{
    const auto started = std::chrono::steady_clock::now();
    view_.rebuild(battle);
    // The scripts of objects that are gone end with them.
    std::erase_if(contexts_,
                  [&](const std::unique_ptr<BattleScriptContext>& context)
                  {
                      return context->bound_object && view_.entity(*context->bound_object) == nullptr;
                  });
    // Entities that appeared get their type's script; the view keeps the list while scripts start.
    const std::vector<EntityId> appeared(view_.new_entities().begin(), view_.new_entities().end());
    for (const auto id : appeared)
    {
        const auto* info = battle.type_info(battle.world().type_name(id));
        const std::string_view script = info != nullptr ? std::string_view(info->lua_script) : std::string_view();
        if (script.empty() || std::ranges::find(failed_scripts_, script) != failed_scripts_.end())
        {
            continue;
        }
        if (const auto added = start(script, id); !added)
        {
            log_warning("script", with_context("script " + std::string(script) + " of " + std::string(battle.world().type_name(id)) + " not started", added.error()));
            failed_scripts_.emplace_back(script);
            ++stats_.failed;
        }
    }
    for (auto& context : contexts_)
    {
        context->host->tick();
    }
    sound_requests_.clear();
    for (auto& context : contexts_)
    {
        context->submit_orders(battle);
        context->take_sounds(sound_requests_);
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    ++stats_.phases;
    stats_.total_seconds += seconds;
    stats_.worst_phase_seconds = std::max(stats_.worst_phase_seconds, seconds);
}

const BattleScriptStats& BattleScripts::stats() const noexcept
{
    return stats_;
}

std::span<const ScriptSoundRequest> BattleScripts::sound_requests() const noexcept
{
    return sound_requests_;
}

std::size_t BattleScripts::size() const noexcept
{
    return contexts_.size();
}

const ScriptHost& BattleScripts::host(const std::size_t index) const
{
    EAW_ASSERT_MSG(index < contexts_.size(), "a battle script index out of range");
    return *contexts_[index]->host;
}

} // namespace eaw
