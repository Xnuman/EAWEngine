// Battles above the simulation: combat data from XML, replayable orders, order logs, runtime types.
#include "test_support.hpp"

#include "battle.hpp"
#include "battle_effects.hpp"
#include "battle_log.hpp"
#include "battle_scripts.hpp"
#include "particle_effects.hpp"
#include "projectile_visuals.hpp"
#include "scene_builder_internal.hpp"
#include "sandbox_options.hpp"
#include "unit_animation.hpp"

namespace {

using namespace eaw_test;

void test_combat_content()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-combat-content-test";
    const auto xml_root = root / "Data" / "XML";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(xml_root);
    {
        std::ofstream file(xml_root / "GameConstants.xml", std::ios::binary);
        file << "<GameConstants>"
                "<Damage_To_Armor_Mod> Damage_Laser, Armor_Tank, 0.5 </Damage_To_Armor_Mod>"
                "<Damage_To_Armor_Mod>Damage_Laser, Shield_Tank, 2</Damage_To_Armor_Mod>"
                "<Damage_To_Armor_Mod>Damage_Laser, Armor_Tank</Damage_To_Armor_Mod>"
                "<Object_Max_Speed_Multiplier_Land> 1.5 </Object_Max_Speed_Multiplier_Land><Object_Max_Health_Multiplier_Space>2</Object_Max_Health_Multiplier_Space>"
                "<Object_Max_Health_Multiplier_Land>-1</Object_Max_Health_Multiplier_Land>"
                "</GameConstants>";
    }
    {
        std::filesystem::create_directories(xml_root / "Enum");
        std::ofstream file(xml_root / "Enum" / "GameObjectCategoryType.xml", std::ios::binary);
        file << "<EnumDefinition><Infantry> 0x00000080 </Infantry><Vehicle>0x00000100</Vehicle><Air>0x200</Air><Broken>zz</Broken><All>0xFFFFFFFFFFFFFFFF</All><None>0</None></EnumDefinition>";
    }
    {
        std::ofstream file(xml_root / "Enum" / "GameObjectPropertiesType.xml", std::ios::binary);
        file << "<EnumDefinition><Turret>0x00000004</Turret><NotOpportunityTarget>0x00000100</NotOpportunityTarget></EnumDefinition>";
    }
    {
        std::ofstream file(xml_root / "Units.xml", std::ios::binary);
        file << "<Units>"
                "<Projectile Name=\"Proj_Base\"><Max_Speed>10</Max_Speed><Projectile_Max_Flight_Distance>400</Projectile_Max_Flight_Distance>"
                "<Projectile_Does_Shield_Damage>No</Projectile_Does_Shield_Damage><Damage_Type>Damage_Laser</Damage_Type></Projectile>"
                "<Projectile Name=\"Proj_Tank\"><Variant_Of_Existing_Type>Proj_Base</Variant_Of_Existing_Type><Projectile_Damage>7.5</Projectile_Damage></Projectile>"
                "<GroundVehicle "
                "Name=\"Tank\"><SFXEvent_Fire>Tank_Fire</SFXEvent_Fire><Tactical_Health>100</Tactical_Health><Shield_Points>50</Shield_Points><Shield_Refresh_Rate>4</Shield_Refresh_Rate>"
                "<Armor_Type> Armor_Tank </Armor_Type><Shield_Armor_Type>Shield_Tank</Shield_Armor_Type><Ranged_Target_Z_Adjust>12</Ranged_Target_Z_Adjust>"
                "<Projectile_Types>Proj_Tank, Proj_Base</Projectile_Types><Targeting_Max_Attack_Distance>200</Targeting_Max_Attack_Distance>"
                "<Projectile_Fire_Recharge_Seconds>1.5</Projectile_Fire_Recharge_Seconds><Projectile_Fire_Pulse_Count>3</Projectile_Fire_Pulse_Count>"
                "<Projectile_Fire_Pulse_Delay_Seconds>0.2</Projectile_Fire_Pulse_Delay_Seconds><CategoryMask>Vehicle | Air</CategoryMask>"
                "<Targeting_Fire_Inaccuracy>Infantry, 3</Targeting_Fire_Inaccuracy><Targeting_Fire_Inaccuracy>Air, 70</Targeting_Fire_Inaccuracy>"
                "<Targeting_Fire_Inaccuracy_Fixed_Radius>50</Targeting_Fire_Inaccuracy_Fixed_Radius></GroundVehicle>"
                "<GroundStructure Name=\"Wall\"><Tactical_Health>-5</Tactical_Health></GroundStructure>"
                "<Priority_Set Name=\"AntiTank\"><Attack_Priorities>Turret, 2.0, Vehicle, 1.0, Bogus, 3</Attack_Priorities><Category_Exclusions>Air</Category_Exclusions>"
                "<Property_Exclusions>NotOpportunityTarget</Property_Exclusions><Unit_Exclusions>Wall, Tank</Unit_Exclusions></Priority_Set>"
                "<GroundVehicle Name=\"Picky\"><Targeting_Priority_Set>AntiTank</Targeting_Priority_Set><Property_Flags>Turret</Property_Flags></GroundVehicle>"
                "<Container Name=\"Slow_Team\"><Max_Speed>.5</Max_Speed><Override_Acceleration>.1</Override_Acceleration></Container>"
                "<GroundCompany Name=\"Slow_Company\"><Create_Team_Type>Slow_Team</Create_Team_Type></GroundCompany>"
                "<Prop Name=\"Rock\"><Custom_Hard_XExtent>24</Custom_Hard_XExtent><Custom_Hard_YExtent>30</Custom_Hard_YExtent><Custom_Hard_XExtent_Offset>3</Custom_Hard_XExtent_Offset>"
                "<Custom_Hard_YExtent_Offset>-10</Custom_Hard_YExtent_Offset></Prop>"
                "<Prop Name=\"Pad\"><Obstacle_Width>40</Obstacle_Width><Obstacle_Height>20</Obstacle_Height><Obstacle_X_Offset>0</Obstacle_X_Offset><Obstacle_Y_Offset>-10</Obstacle_Y_Offset>"
                "<Custom_Hard_XExtent>5</Custom_Hard_XExtent><Custom_Hard_YExtent>5</Custom_Hard_YExtent></Prop>"
                "<GroundStructure Name=\"On_Pad\"><Obstacle_Proxy_Type>Pad</Obstacle_Proxy_Type></GroundStructure>"
                "<Projectile Name=\"Proj_Paced\"><Projectile_Fire_Recharge_Seconds>6</Projectile_Fire_Recharge_Seconds><Projectile_Fire_Pulse_Count>2</Projectile_Fire_Pulse_Count></Projectile>"
                "<GroundVehicle Name=\"Paced\"><Projectile_Types>Proj_Paced, Proj_Base</Projectile_Types><Targeting_Max_Attack_Distance>50</Targeting_Max_Attack_Distance></GroundVehicle>"
                "<HardPoint Name=\"HP_Gun\"><Fire_SFXEvent> Turbo </Fire_SFXEvent><Type> HARD_POINT_WEAPON_LASER </Type><Fire_Projectile_Type>Proj_Tank</Fire_Projectile_Type>"
                "<Fire_Min_Recharge_Seconds>3</Fire_Min_Recharge_Seconds><Fire_Max_Recharge_Seconds>4</Fire_Max_Recharge_Seconds><Fire_Pulse_Count>7</Fire_Pulse_Count>"
                "<Fire_Pulse_Delay_Seconds>0.2</Fire_Pulse_Delay_Seconds><Fire_Range_Distance>2000</Fire_Range_Distance><Damage_Type>Damage_Unknown</Damage_Type></HardPoint>"
                "<HardPoint Name=\"HP_Shield\"><Type>HARD_POINT_SHIELD_GENERATOR</Type><Health>375</Health><Attachment_Bone>HP_Bone</Attachment_Bone></HardPoint>"
                "<SpaceUnit Name=\"Cruiser\"><Tactical_Health>500</Tactical_Health><Targeting_Max_Attack_Distance>1500</Targeting_Max_Attack_Distance>"
                "<HardPoints>HP_Gun, HP_Shield,\n HP_Gun HP_Missing</HardPoints></SpaceUnit>"
                "<GroundVehicle Name=\"Broken_Gun\"><Tactical_Health>10</Tactical_Health><Projectile_Types>Proj_Missing</Projectile_Types>"
                "<Targeting_Max_Attack_Distance>100</Targeting_Max_Attack_Distance></GroundVehicle>"
                "</Units>";
    }
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));
    const auto database = eaw::XmlAssetDatabase::build(vfs);
    const auto catalog = eaw::GameObjectCatalog::build(database);
    const auto content = eaw::load_combat_content(vfs);
    require(content.damage_table.size() == 2 && content.warnings.size() == 3 && content.warnings[0].find("Damage_To_Armor_Mod") != std::string::npos &&
                content.warnings[1].find("Object_Max_Health_Multiplier_Land") != std::string::npos && content.warnings[2].find("Broken") != std::string::npos,
            "damage modifiers must be read and malformed entries reported");
    require(content.category_bits.size() == 5 && content.categories("Infantry | air, Unknown") == 0x280U && content.categories("All") == 0xffffffffU && content.categories("None") == 0,
            "category bits must be read from the enum and combined from a list");
    require(content.max_speed_multiplier.of(true) == 1.5F && content.max_speed_multiplier.of(false) == 1.0F && content.max_health_multiplier.of(false) == 2.0F &&
                content.max_health_multiplier.of(true) == 1.0F,
            "the per-mode speed and health multipliers must be read, 1 when absent or invalid");
    const auto laser = content.type_ids.at("damage_laser");
    const auto tank_armor = content.type_ids.at("armor_tank");
    require(content.damage_table.modifier(laser, tank_armor) == 0.5F && content.damage_table.modifier(laser, content.type_ids.at("shield_tank")) == 2.0F,
            "damage modifiers must map type names to ids");

    const auto definition = [&](const std::string_view name)
    {
        return catalog.find(name).front();
    };
    std::vector<std::string> warnings;
    const auto tank = eaw::combat_profile(catalog, *definition("Tank"), content, warnings);
    require(tank.health == 100.0F && tank.shield == 50.0F && tank.shield_regen_per_second == 4.0F && tank.armor_type == tank_armor && tank.aim_height == 12.0F && tank.weapons.size() == 1,
            "a type's health, shields, armor, and weapon must be read");
    const auto& weapon = tank.weapons.front();
    require(tank.categories == 0x300U && weapon.inaccuracy.size() == 3 && weapon.inaccuracy[0].categories == 0x80U && weapon.inaccuracy[0].distance == 3.0F && weapon.inaccuracy[1].distance == 70.0F &&
                weapon.inaccuracy[2].categories == eaw::all_categories && weapon.inaccuracy[2].distance == 50.0F,
            "a type's categories and its weapon's inaccuracy by target category must be read");
    require(weapon.range == 200.0F && weapon.recharge_seconds == 1.5F && weapon.pulse_count == 3 && weapon.pulse_delay_seconds == 0.2F && weapon.damage == 7.5F && weapon.damage_type == laser &&
                weapon.projectile_speed == 10.0F * eaw::provisional_max_speed_scale && weapon.max_flight_distance == 400.0F && !weapon.damages_shield && weapon.damages_health,
            "the first projectile type must supply the shot, through its variant chain");
    require(warnings.empty(), "a well-formed type must not warn");
    std::vector<std::string> picky_warnings;
    const auto picky = eaw::combat_profile(catalog, *definition("Picky"), content, picky_warnings);
    const auto& wanted = picky.targeting;
    require(picky.properties == 0x4U && picky.type_key == eaw::game_object_name_crc("Picky") && wanted.ranks.size() == 2 && wanted.ranks[0].categories == 0x100U &&
                wanted.ranks[1].properties == 0x4U && wanted.excluded_categories == 0x200U && wanted.excluded_properties == 0x100U && wanted.excluded_types.size() == 2 && picky_warnings.size() == 1 &&
                picky_warnings[0].find("Bogus") != std::string::npos,
            "a targeting priority set must be read in rank order with its exclusions");
    require(wanted.rank(0x100U, 0, 0) == 1.0F && wanted.rank(0x80U, 0x4U, 0) == 2.0F && wanted.rank(0x200U, 0, 0) < 0.0F && wanted.rank(0x80U, 0x100U, 0) < 0.0F &&
                wanted.rank(0x80U, 0, eaw::game_object_name_crc("Tank")) < 0.0F && wanted.rank(0x80U, 0, 0) == eaw::TargetingPriorities::unlisted_rank && wanted.rank(0x300U, 0, 0) == 1.0F,
            "a match must rank, exclusions must apply only to unmatched targets, and the rest must rank last");
    const auto team = eaw::scene_build::type_team_movement(catalog, *definition("Slow_Company"), 2.0F);
    require(team.max_speed && *team.max_speed == 0.5F * eaw::provisional_max_speed_scale * 2.0F && team.acceleration && *team.acceleration == 0.1F * eaw::provisional_acceleration_scale &&
                !team.turn_rate && !team.deceleration && !eaw::scene_build::type_team_movement(catalog, *definition("Tank"), 1.0F).max_speed,
            "a company's team must give its members the team's speed and acceleration, and only what it sets");
    {
        // Rotated a quarter turn and moved: the type's forward (x) maps to the model's -Y.
        auto placement = eaw::Matrix4::identity();
        placement.values[0] = 0.0F;
        placement.values[1] = 1.0F;
        placement.values[4] = -1.0F;
        placement.values[5] = 0.0F;
        placement.values[12] = 100.0F;
        placement.values[13] = 200.0F;
        const auto near = [](const float a, const float b)
        {
            return std::fabs(a - b) < 1.0e-3F;
        };
        const auto rock = eaw::scene_build::type_ground_footprint(catalog, *definition("Rock"), placement, eaw::passability::obstacle);
        require(rock && near(rock->center.x, 103.0F) && near(rock->center.y, 190.0F) && rock->half_length == 30.0F && rock->half_width == 24.0F && near(rock->heading, 1.5707964F),
                "hard extents must be half extents around their offset, the type's x along the model's forward");
        const auto pad = eaw::scene_build::type_ground_footprint(catalog, *definition("Pad"), eaw::Matrix4::identity(), eaw::passability::obstacle);
        const auto on_pad = eaw::scene_build::type_ground_footprint(catalog, *definition("On_Pad"), eaw::Matrix4::identity(), eaw::passability::obstacle);
        require(pad && near(pad->center.x, 0.0F) && near(pad->center.y, -20.0F) && pad->half_length == 10.0F && pad->half_width == 20.0F && on_pad && near(on_pad->center.y, -20.0F) &&
                    on_pad->half_width == 20.0F && !eaw::scene_build::type_ground_footprint(catalog, *definition("Tank"), eaw::Matrix4::identity(), eaw::passability::obstacle),
                "an obstacle rectangle must win over hard extents, a proxy type must lend its own, and a type without either must give none");
    }
    eaw::EntitySpawn member{.max_speed = 30.0F, .turn_rate = 2.0F};
    eaw::apply_team_movement(team, member);
    require(member.max_speed == *team.max_speed && member.turn_rate == 2.0F && member.movement.acceleration == *team.acceleration, "team movement must replace only what the team sets");
    const auto paced = eaw::combat_profile(catalog, *definition("Paced"), content, warnings);
    require(paced.weapons.size() == 1 && paced.weapons[0].recharge_seconds == 6.0F && paced.weapons[0].pulse_count == 2 && tank.weapons[0].recharge_seconds == 1.5F,
            "a shooter without its own cadence must take its projectile's");
    require(warnings.empty(), "a well-formed type must not warn");
    const auto wall = eaw::combat_profile(catalog, *definition("Wall"), content, warnings);
    require(wall.health == 0.0F && wall.weapons.empty() && warnings.size() == 1 && warnings[0].find("Tactical_Health") != std::string::npos, "a negative health must be reported and ignored");
    const auto broken = eaw::combat_profile(catalog, *definition("Broken_Gun"), content, warnings);
    require(broken.health == 10.0F && broken.weapons.empty() && warnings.size() == 2 && warnings[1].find("Proj_Missing") != std::string::npos, "an undefined projectile type must be reported");

    // Weapon hardpoints become weapons (other hardpoint types do not); the recharge is the midpoint of
    // the min/max range and a hardpoint's Damage_Type overrides its projectile's.
    const auto cruiser = eaw::combat_profile(catalog, *definition("Cruiser"), content, warnings);
    std::vector<std::string> sturdy_warnings;
    const auto sturdy = eaw::combat_profile(catalog, *definition("Cruiser"), content, sturdy_warnings, nullptr, 1.0F, 2.0F);
    require(sturdy.health == 1000.0F && sturdy.hardpoints[1].health == 750.0F, "the health multiplier must scale the object and its hardpoints");
    require(cruiser.weapons.size() == 2 && cruiser.range() == 2000.0F, "every weapon hardpoint must become a weapon");
    require(cruiser.weapons[0].fire_sound == eaw::game_object_name_crc("TURBO") && tank.weapons.front().fire_sound == eaw::game_object_name_crc("Tank_Fire"),
            "a weapon hardpoint must play its Fire_SFXEvent, a unit's weapon its SFXEvent_Fire");
    using Kind = eaw::HardpointProfile::Kind;
    require(cruiser.hardpoints.size() == 3 && cruiser.hardpoints[0].kind == Kind::weapon && cruiser.hardpoints[1].kind == Kind::shield_generator && cruiser.hardpoints[1].health == 375.0F &&
                cruiser.hardpoints[0].health == 0.0F && cruiser.weapons[0].hardpoint == 0 && cruiser.weapons[1].hardpoint == 2 && cruiser.weapons[0].cone_half_angle >= 3.14F,
            "every hardpoint must become a part (health, kind), weapons must name their hardpoint, and without bones cover all around");
    {
        // Hardpoint positions come from model bones and scale with the type's Scale_Factor. Models face
        // -Y, so a bone at (4, -6, 2) sits 6 forward, 4 left, and 2 up.
        eaw::AloModelData model;
        eaw::AloBone bone;
        bone.name = "HP_Bone";
        bone.transform = {1.0F, 0.0F, 0.0F, 4.0F, 0.0F, 1.0F, 0.0F, -6.0F, 0.0F, 0.0F, 1.0F, 2.0F};
        model.bones.push_back(bone);
        std::vector<std::string> scale_warnings;
        const auto skeleton = eaw::load_skeleton(model);
        const auto unscaled = eaw::combat_profile(catalog, *definition("Cruiser"), content, scale_warnings, &skeleton);
        const auto scaled = eaw::combat_profile(catalog, *definition("Cruiser"), content, scale_warnings, &skeleton, 2.0F);
        const auto& part = unscaled.hardpoints[1].offset;
        const auto& doubled = scaled.hardpoints[1].offset;
        require(part.forward == 6.0F && part.left == 4.0F && part.up == 2.0F && doubled.forward == 12.0F && doubled.left == 8.0F && doubled.up == 4.0F,
                "hardpoint positions must come from bones and scale with the model");
    }
    const auto& turbolaser = cruiser.weapons.front();
    require(turbolaser.recharge_seconds == 3.5F && turbolaser.pulse_count == 7 && turbolaser.pulse_delay_seconds == 0.2F && turbolaser.damage == 7.5F && turbolaser.damage_type == 0 &&
                turbolaser.projectile_speed == 10.0F * eaw::provisional_max_speed_scale,
            "a weapon hardpoint must read its fire fields and its projectile");
    require(std::ranges::any_of(warnings,
                                [](const std::string& warning)
                                {
                                    return warning.find("HP_Missing") != std::string::npos;
                                }),
            "an undefined hardpoint must be reported");

    const auto hostility = eaw::provisional_map_hostility();
    require(hostility.hostile(0, 1) && hostility.hostile(2, 6) && !hostility.hostile(eaw::neutral_map_owner, 1) && !hostility.hostile(1, 1), "map owners must be hostile except the neutral slot");
    std::filesystem::remove_all(root, ignored);
}

// Battles: orders in replayable terms become simulation commands at the next tick and are recorded
// with that tick; invalid orders are rejected without effect; the same orders reproduce the same
// battle.
void test_battle_orders()
{
    eaw::SpawnableType fighter;
    fighter.type_name = "Fighter";
    fighter.max_speed = 120.0F;
    fighter.min_speed = 75.0F;
    fighter.turn_rate = 1.5F;
    fighter.radius = 5.0F;
    fighter.combat.health = 50.0F;
    fighter.combat.weapons = {eaw::WeaponProfile{.range = 400.0F, .recharge_seconds = 0.5F, .damage = 2.0F}};
    eaw::SpawnableType squadron;
    squadron.type_name = "Fighter_Squadron";
    squadron.members = {0, 0, 0};
    const std::array<eaw::SpawnableType, 2> types{fighter, squadron};
    eaw::BattleMap map;
    map.spawnable = types;
    map.hostility.set(1, 2, true);

    const auto run = [&](const bool record)
    {
        eaw::Battle battle(map, "TEST.TED", 1.0 / 30.0);
        expect_ok(battle.submit({.order = eaw::BattleOrder::spawn, .point = {0.0F, 0.0F, 0.0F}, .type = "Fighter_Squadron", .count = 2, .owner = 1}), "squadron spawn");
        expect_ok(battle.submit({.order = eaw::BattleOrder::spawn, .point = {300.0F, 0.0F, 0.0F}, .type = "Fighter", .count = 4, .owner = 2}), "fighter spawn");
        // Rejected orders change nothing and are not recorded.
        require(!battle.submit({.order = eaw::BattleOrder::spawn, .type = "Unknown"}), "an unknown type must be rejected");
        require(!battle.submit({.order = eaw::BattleOrder::spawn, .type = "Fighter", .count = 0}), "a zero count must be rejected");
        require(!battle.submit({.order = eaw::BattleOrder::spawn, .type = "Fighter", .owner = eaw::Hostility::max_owners}), "an owner beyond the table must be rejected");
        require(!battle.submit({.order = eaw::BattleOrder::move, .point = {1.0F, 2.0F, 3.0F}}), "an order without entities must be rejected");
        require(!battle.submit({.order = eaw::BattleOrder::move, .entities = {eaw::EntityId{1, 1}}, .point = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}}),
                "a non-finite point must be rejected");
        battle.step();
        require(battle.world().snapshot().entities.size() == 10, "spawn orders must apply at the next tick (two squadrons of three and four fighters)");
        if (record)
        {
            require(battle.log().entries.size() == 2 && battle.log().entries[0].tick == 1 && battle.log().entries[1].command.count == 4, "accepted orders must be recorded with their tick");
        }
        std::vector<eaw::EntityId> squadron_ids;
        for (const auto& entity : battle.world().snapshot().entities)
        {
            if (entity.owner == 1)
            {
                squadron_ids.push_back(entity.id);
            }
        }
        expect_ok(battle.submit({.order = eaw::BattleOrder::move, .entities = squadron_ids, .point = {600.0F, 200.0F, 0.0F}}), "move order");
        for (int tick = 0; tick < 300; ++tick)
        {
            battle.step();
        }
        return std::pair{battle.world().state_hash(), battle.world().stats().kills};
    };
    const auto first = run(true);
    const auto second = run(false);
    require(first.first == second.first && first.second == second.second, "the same orders must reproduce the same battle");
    require(first.second > 0, "the test battle must be a real fight");
}

// Battle logs: the text form round-trips every order bit-exactly, malformed logs are rejected with
// their line, and replaying a recorded battle's log reproduces its state hash.
void test_battle_log()
{
    eaw::BattleLog log;
    log.map = "DATA/ART/MAPS/SOME MOD MAP.TED";
    log.entries.push_back({1, {.order = eaw::BattleOrder::spawn, .point = {0.1F, -0.0F, 1.0e-30F}, .type = "Rebel_X-Wing_Squadron", .count = 2, .owner = 1}});
    log.entries.push_back({40, {.order = eaw::BattleOrder::move, .entities = {{17, 1}, {18, 3}}, .point = {600.25F, -3.4028235e38F, 0.333333343F}}});
    log.entries.push_back({40, {.order = eaw::BattleOrder::attack, .entities = {{17, 1}}, .target = {23, 2}}});
    log.entries.push_back({95, {.order = eaw::BattleOrder::stop, .entities = {{18, 3}}}});
    log.entries.push_back({96, {.order = eaw::BattleOrder::set_stance, .entities = {{18, 3}}, .stance = eaw::Stance::hold_position}});
    log.end_tick = 3000;
    log.end_hash = 0x9f3c0a1e5b7d2c44ULL;
    const auto text = eaw::format_battle_log(log);
    const auto parsed = eaw::parse_battle_log(text);
    require(parsed.ok(), "a formatted battle log must parse: " + (parsed.ok() ? std::string{} : parsed.error().message));
    require(eaw::format_battle_log(*parsed) == text, "a battle log must round-trip through its text form");
    const auto& point = parsed->entries[0].command.point;
    require(point.x == 0.1F && std::signbit(point.y) && point.z == 1.0e-30F && parsed->entries[1].command.point.y == -3.4028235e38F, "battle log numbers must read back bit-identically");
    require(parsed->map == log.map && parsed->entries[1].command.entities[1] == eaw::EntityId{18, 3} && parsed->entries[4].command.stance == eaw::Stance::hold_position && parsed->end_tick == 3000U &&
                parsed->end_hash == 0x9f3c0a1e5b7d2c44ULL,
            "battle log fields must survive the round trip");
    // Comments, blank lines, and CRLF line ends are accepted.
    require(eaw::parse_battle_log("EAWEngine battle log 1\r\n# a comment\r\n\r\n5 stop 1.1\r\n").ok(), "comments, blank lines, and CRLF must be accepted");
    const auto rejects = [](const std::string& body, const std::string& what, const std::string& line)
    {
        const auto result = eaw::parse_battle_log("EAWEngine battle log 1\n" + body);
        require(!result.ok() && result.error().message.find("line " + line) != std::string::npos, "a battle log with " + what + " must be rejected naming line " + line);
    };
    require(!eaw::parse_battle_log("").ok() && !eaw::parse_battle_log("EAWEngine battle log 2\n").ok(), "an empty or unknown-version log must be rejected");
    rejects("5 stop 1.1\n4 stop 1.1\n", "decreasing ticks", "3");
    rejects("5 dance 1.1\n", "an unknown order", "2");
    rejects("5 stop 1\n", "an id without a generation", "2");
    rejects("5 stop\n", "an order without entities", "2");
    rejects("5 move 1 nan 3 1.1\n", "a non-finite point", "2");
    rejects("5 spawn X-Wing 2 1 0 0\n", "a short spawn", "2");
    rejects("5 stance sleepy 1.1\n", "an unknown stance", "2");
    rejects("end 10\n12 stop 1.1\n", "orders after the end", "3");
    rejects("20 stop 1.1\nend 10\n", "an end before the last order", "3");
    rejects("end 10 xyz\n", "an invalid hash", "2");

    // Record a battle, write and read its log, and replay it into a fresh battle.
    eaw::SpawnableType fighter;
    fighter.type_name = "Fighter";
    fighter.max_speed = 120.0F;
    fighter.min_speed = 75.0F;
    fighter.turn_rate = 1.5F;
    fighter.radius = 5.0F;
    fighter.combat.health = 30.0F;
    fighter.combat.weapons = {eaw::WeaponProfile{.range = 400.0F, .recharge_seconds = 0.5F, .damage = 2.0F}};
    eaw::BattleMap map;
    map.spawnable = std::span(&fighter, 1);
    map.hostility.set(1, 2, true);
    eaw::Battle recorded(map, "TEST.TED");
    expect_ok(recorded.submit({.order = eaw::BattleOrder::spawn, .point = {0.1F, 0.2F, 0.3F}, .type = "Fighter", .count = 3, .owner = 1}), "spawn");
    expect_ok(recorded.submit({.order = eaw::BattleOrder::spawn, .point = {350.7F, 10.1F, 0.3F}, .type = "Fighter", .count = 3, .owner = 2}), "spawn");
    for (int tick = 0; tick < 50; ++tick)
    {
        recorded.step();
    }
    const auto first = recorded.world().snapshot().entities.front().id;
    expect_ok(recorded.submit({.order = eaw::BattleOrder::set_stance, .entities = {first}, .stance = eaw::Stance::hold_fire}), "stance");
    expect_ok(recorded.submit({.order = eaw::BattleOrder::move, .entities = {first}, .point = {-500.3F, 77.7F, 0.0F}}), "move");
    expect_ok(recorded.submit({.order = eaw::BattleOrder::attack_move, .entities = {recorded.world().snapshot().entities[1].id}, .point = {120.5F, -40.25F, 0.0F}}), "attack-move");
    expect_ok(recorded.submit({.order = eaw::BattleOrder::guard, .entities = {recorded.world().snapshot().entities[2].id}, .target = first}), "guard");
    const auto second = recorded.world().snapshot().entities[1].id;
    expect_ok(recorded.submit({.order = eaw::BattleOrder::set_invulnerable, .entities = {second}, .flag = true}), "invulnerable");
    expect_ok(recorded.submit({.order = eaw::BattleOrder::set_cannot_be_killed, .entities = {second}, .flag = true}), "cannot be killed");
    expect_ok(recorded.submit({.order = eaw::BattleOrder::suspend_locomotor, .entities = {second}, .flag = false}), "suspend");
    expect_ok(recorded.submit({.order = eaw::BattleOrder::damage, .entities = {recorded.world().snapshot().entities[3].id}, .amount = 12.5F}), "damage");
    expect_ok(recorded.submit({.order = eaw::BattleOrder::teleport, .entities = {recorded.world().snapshot().entities[4].id}, .point = {10.5F, -20.25F, 0.0F}, .heading = 0.75F}), "teleport");
    expect_ok(recorded.submit({.order = eaw::BattleOrder::teleport, .entities = {second}, .point = {-30.0F, 5.0F, 0.0F}}), "teleport without heading");
    expect_ok(recorded.submit({.order = eaw::BattleOrder::remove, .entities = {recorded.world().snapshot().entities.back().id}}), "remove");
    for (int tick = 0; tick < 250; ++tick)
    {
        recorded.step();
    }
    auto log_out = recorded.log();
    log_out.end_tick = recorded.world().stats().tick;
    log_out.end_hash = recorded.world().state_hash();
    const auto reread = eaw::parse_battle_log(eaw::format_battle_log(log_out));
    require(reread.ok(), "a recorded log must parse");
    eaw::Battle replayed(map, reread->map);
    std::size_t next = 0;
    while (replayed.world().stats().tick < *reread->end_tick)
    {
        for (; next < reread->entries.size() && reread->entries[next].tick <= replayed.world().stats().tick; ++next)
        {
            expect_ok(replayed.submit(reread->entries[next].command), "replayed order");
        }
        replayed.step();
    }
    require(replayed.world().state_hash() == *reread->end_hash, "replaying a recorded log must reproduce the battle's state hash");
    const auto log_text = eaw::format_battle_log(log_out);
    require(log_text.find(" invulnerable 1 ") != std::string::npos && log_text.find(" unkillable 1 ") != std::string::npos && log_text.find(" suspend 0 ") != std::string::npos &&
                log_text.find(" damage 12.5 ") != std::string::npos && log_text.find(" teleport 10.5 -20.25 0 0.75 ") != std::string::npos &&
                log_text.find(" teleport -30 5 0 - ") != std::string::npos,
            "the condition, damage, and teleport orders must be written to the log");
    require(log_text.find(" remove ") != std::string::npos && log_text.find(" attack_move 0.0 ") != std::string::npos && log_text.find(" guard ") != std::string::npos,
            "remove, attack-move, and guard orders must be written to the log");
}

// Types added at runtime: the battle offsets company members, presentation draws spawned entities
// of the new types from their meshes, and the animator places the new rigs after the old ones.
void test_runtime_spawnable_registration()
{
    eaw::SpawnableType trooper;
    trooper.type_name = "Trooper";
    trooper.max_speed = 10.0F;
    eaw::BattleMap map;
    map.spawnable = std::span(&trooper, 1);
    eaw::Battle battle(map, "TEST.TED", 0.1);
    // A package: a member type and its squad (members relative to the package).
    eaw::SpawnableType walker;
    walker.type_name = "Walker";
    walker.mesh = 1;
    walker.max_speed = 5.0F;
    walker.team_color = {0.0F, 1.0F, 0.0F, 1.0F};
    eaw::SpawnableType squad;
    squad.type_name = "Walker_Squad";
    squad.mesh = 1;
    squad.members = {0, 0};
    const std::array<eaw::SpawnableType, 2> package{walker, squad};
    const auto first = battle.add_spawnable(package);
    require(first == 1 && battle.spawnable().size() == 3 && battle.spawnable()[2].members == std::vector<std::size_t>{1, 1}, "added company members must point at the added member type");
    expect_ok(battle.submit({.order = eaw::BattleOrder::spawn, .point = {50.0F, 0.0F, 0.0F}, .type = "Walker_Squad", .count = 1, .owner = 2}), "spawn of an added squad");
    battle.step();
    require(battle.world().snapshot().entities.size() == 2 && battle.world().snapshot().entities[0].visual == eaw::spawnable_visual(1),
            "an added squad must spawn its members with the member type's visual key");

    eaw::VisualScene scene;
    scene.meshes.resize(1);
    scene.meshes[0].name = "TROOPER.ALO";
    eaw::ScenePresentation presentation(scene, {}, {}, std::span(&trooper, 1));
    eaw::VisualMesh walker_mesh;
    walker_mesh.name = "WALKER.ALO";
    presentation.add_meshes(std::span(&walker_mesh, 1));
    presentation.add_spawnable(package);
    std::vector<eaw::VisualInstance> instances;
    std::vector<eaw::EntityId> ids;
    presentation.build(battle.world().snapshot().entities, {}, instances, ids);
    require(instances.size() == 2 && instances[0].mesh == 1 && instances[0].team_color[1] == 1.0F && presentation.mesh_name(1) == "WALKER.ALO",
            "presentation must draw entities of added types from their added meshes");

    eaw::VisualRig base_rig;
    base_rig.palette_size = 3;
    const std::vector<eaw::Matrix4> base_rest(3, eaw::Matrix4::identity());
    eaw::UnitAnimator animator(std::span(&base_rig, 1), base_rest);
    eaw::VisualRig added_rig;
    added_rig.rest_offset = 0;
    added_rig.palette_size = 2;
    const std::vector<eaw::Matrix4> added_rest(2, eaw::Matrix4::identity());
    animator.add_rigs(std::span(&added_rig, 1), added_rest);
    std::vector<eaw::VisualInstance> drawn(1);
    drawn[0].mesh = 1;
    std::vector<eaw::EntityId> none(1);
    std::vector<eaw::Matrix4> palette;
    animator.animate({}, none, drawn, 1.0, 1U << 20U, palette);
    require(animator.rig_count() == 2 && palette.size() == 5 && drawn[0].bone_offset == 3, "added rigs must take their rest poses after the existing ones");
}

// Scripts in battles: a script finds objects, reads them, and orders a unit through a block it waits
// on; objects are one Lua value each, an immobile unit's move is refused at once, and names the
// bridge does not implement stay stubs. Script orders are not logged, and replaying the player
// orders with the same script running again reproduces the battle's state hash.
void test_battle_scripts()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-battle-scripts-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root / "Data" / "Scripts" / "GameObject");
    {
        std::ofstream file(root / "Data" / "Scripts" / "GameObject" / "Mover.lua", std::ios::binary);
        file << "function PumpEvents() coroutine.yield(true) end\n"
                "function Base_Definitions() ServiceRate = 0.1 end\n"
                "function main()\n"
                "  local walker = Find_First_Object(\"walker\")\n"
                "  local beacon = Find_First_Object(\"Beacon\")\n"
                "  print(\"type \" .. walker.Get_Type().Get_Name() .. \" owner \" .. walker.Get_Owner().Get_ID() .. \" hull \" .. walker.Get_Hull())\n"
                "  local same = Find_All_Objects_Of_Type(Find_Object_Type(\"WALKER\"))[1] == walker\n"
                "  print(\"same \" .. tostring(same) .. \" mine \" .. table.getn(Find_All_Objects_Of_Type(\"Walker\", walker.Get_Owner())) .. \" theirs \" .. "
                "table.getn(Find_All_Objects_Of_Type(\"Walker\", beacon.Get_Owner())))\n"
                "  print(\"unknown \" .. tostring(Find_Object_Type(\"No_Such_Type\")) .. \" \" .. type(walker))\n"
                "  local block = walker.Move_To(beacon)\n"
                "  print(\"at once \" .. tostring(block.IsFinished()))\n"
                "  while not block.IsFinished() do PumpEvents() end\n"
                "  print(\"arrived \" .. tostring(block.Result()))\n"
                "  local sweep = walker.Attack_Move(walker.Get_Position())\n"
                "  while not sweep.IsFinished() do PumpEvents() end\n"
                "  print(\"swept \" .. tostring(sweep.Result()))\n"
                "  local guard = walker.Guard_Target(beacon)\n"
                "  PumpEvents() PumpEvents()\n"
                "  print(\"guarding \" .. tostring(guard.IsFinished()))\n"
                "  print(\"stub order \" .. tostring(walker.Attack_Move(MainForce)))\n"
                "  local refused = beacon.Move_To(walker)\n"
                "  print(\"immobile \" .. tostring(refused.IsFinished()) .. \" \" .. tostring(refused.Result()))\n"
                "  print(\"stub \" .. tostring(walker.Highlight(true) ~= nil))\n"
                "  walker.Make_Invulnerable(true)\n"
                "  walker.Teleport_And_Face(beacon)\n"
                "  walker.Take_Damage(500)\n"
                "  PumpEvents() PumpEvents()\n"
                "  print(\"teleported \" .. walker.Get_Distance(beacon) .. \" hull \" .. walker.Get_Hull())\n"
                "  walker.Make_Invulnerable(false)\n"
                "  walker.Take_Damage(10000)\n"
                "  PumpEvents() PumpEvents()\n"
                "  print(\"after damage \" .. tostring(walker.Is_Valid()))\n"
                "end\n";
    }
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));

    eaw::SpawnableType walker;
    walker.type_name = "Walker";
    walker.max_speed = 60.0F;
    walker.radius = 1.0F;
    walker.combat.health = 100.0F;
    eaw::SpawnableType beacon;
    beacon.type_name = "Beacon";
    beacon.radius = 1.0F;
    const std::array<eaw::SpawnableType, 2> types{walker, beacon};
    eaw::BattleMap map;
    map.spawnable = types;
    map.hostility.set(1, 2, true);
    constexpr std::uint64_t ticks = 300;

    // One run: the players' orders (here: the spawns) at tick 0, the script from tick 1, and the
    // driver's order step, scripts, players. Returns the final hash and the script's output.
    const auto run = [&](const eaw::BattleLog* replay)
    {
        eaw::Battle battle(map, "TEST.TED");
        if (replay == nullptr)
        {
            expect_ok(battle.submit({.order = eaw::BattleOrder::spawn, .point = {0.0F, 0.0F, 0.0F}, .type = "Walker", .owner = 1}), "spawning the walker");
            expect_ok(battle.submit({.order = eaw::BattleOrder::spawn, .point = {300.0F, 40.0F, 0.0F}, .type = "Beacon", .owner = 2}), "spawning the beacon");
        }
        else
        {
            for (const auto& entry : replay->entries)
            {
                expect_ok(battle.submit(entry.command), "replaying an order");
            }
        }
        battle.step();
        eaw::BattleScripts scripts(vfs);
        expect_ok(scripts.add(battle, "Mover"), "starting the script");
        for (std::uint64_t tick = 1; tick < ticks; ++tick)
        {
            battle.step();
            scripts.run(battle);
        }
        require(scripts.host(0).errors().empty(), "the script must run without errors: " + (scripts.host(0).errors().empty() ? std::string{} : scripts.host(0).errors().front().message));
        return std::tuple{battle.world().state_hash(), scripts.host(0).output(), battle.log()};
    };
    const auto [hash, output, log] = run(nullptr);
    const auto printed = [&](const std::string_view line)
    {
        return std::ranges::find(output, line) != output.end();
    };
    require(printed("type Walker owner 1 hull 1") && printed("same true mine 1 theirs 0") && printed("unknown nil userdata"),
            "scripts must read types, owners, and hull, filter by owner, see one value per object, and get nil for unknown types");
    require(printed("at once false") && printed("arrived true"), "a move must return a block that finishes when the unit arrives");
    require(printed("swept true") && printed("guarding false") && printed("stub order nil"),
            "an attack-move block must finish on arrival, a guard block stay open, and a stub destination drop the order");
    require(printed("teleported 0 hull 1") && printed("after damage false"), "scripts must teleport units, make them invulnerable, and kill them with Take_Damage");
    require(printed("immobile true false") && printed("stub true"), "an immobile unit's move must be refused at once, and unimplemented methods must stay stubs");
    require(log.entries.size() == 2, "script orders must not be recorded in the battle log");
    const auto [replayed_hash, replayed_output, replayed_log] = run(&log);
    require(replayed_hash == hash && replayed_output == output, "replaying the player orders with the script running again must reproduce the battle");
    std::filesystem::remove_all(root, ignored);
}

// Game-object scripts: a placed object's type script starts with Object bound to it and finds
// objects by hint; a spawned entity gets its type's script when it appears; a proximity handler
// runs when another object comes within range as the object is serviced, until it cancels itself;
// and a script that cannot be loaded is reported once, not retried for every entity.
void test_battle_object_scripts()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-battle-object-scripts-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    const auto scripts_folder = root / "Data" / "Scripts" / "GameObject";
    std::filesystem::create_directories(scripts_folder);
    const auto write = [&](const char* name, const std::string_view text)
    {
        std::ofstream file(scripts_folder / name, std::ios::binary);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    };
    // The framework's PumpEvents in miniature: service the object, yield, then run the events.
    write("Pump.lua", "function PumpEvents()\n"
                      "  Object.Service_Wrapper()\n"
                      "  coroutine.yield(true)\n"
                      "  local event = GetEvent()\n"
                      "  while event do\n"
                      "    local params = GetEvent.Params()\n"
                      "    if params then event(unpack(params)) else event() end\n"
                      "    event = GetEvent()\n"
                      "  end\n"
                      "end\n"
                      "function Base_Definitions() ServiceRate = 0.1 end\n");
    write("Guard.lua", "require(\"Pump\")\n"
                       "function Near(self, other)\n"
                       "  print(\"near \" .. other.Get_Type().Get_Name())\n"
                       "  self.Cancel_Event_Object_In_Range(Near)\n"
                       "  other.Despawn()\n"
                       "end\n"
                       "function main()\n"
                       "  print(\"hint \" .. tostring(Find_Hint(\"beacon\", \"GATE\") == Object) .. \" \" .. table.getn(Find_All_Objects_With_Hint(\"gate\")) .. \" \" .. "
                       "tostring(Find_Hint(\"Walker\", \"gate\")))\n"
                       "  Object.Event_Object_In_Range(Near, 50)\n"
                       "  while true do PumpEvents() end\n"
                       "end\n");
    write("Walker_Script.lua", "require(\"Pump\")\n"
                               "function main()\n"
                               "  print(\"walker \" .. Object.Get_Type().Get_Name() .. \" \" .. Get_Game_Mode() .. \" \" .. tostring(Object.Is_Category(\"INFANTRY\")) .. \" \" .. "
                               "tostring(Object.Is_Category(\"Vehicle | Infantry\")) .. \" \" .. tostring(Object.Is_Category(\"Vehicle\")) .. \" \" .. "
                               "Object.Get_Distance(Find_Hint(\"Beacon\", \"gate\")))\n"
                               "  print(\"shield \" .. Object.Get_Shield() .. \" hero \" .. tostring(Object.Get_Type().Is_Hero()) .. \" rating \" .. "
                               "Object.Get_Type().Get_Combat_Rating())\n"
                               "  Object.Prevent_Opportunity_Fire(true)\n"
                               "  Object.Play_SFX_Event(\"Walker_Hello\")\n"
                               "  print(tostring(Script))\n"
                               "  _ScriptMessage(\"debug line\")\n"
                               "  while true do PumpEvents() end\n"
                               "end\n");
    eaw::VirtualFileSystem vfs;
    vfs.mount(std::make_unique<eaw::DirectoryMount>("test", root));

    eaw::SpawnableType walker;
    walker.type_name = "Walker";
    walker.max_speed = 60.0F;
    walker.radius = 1.0F;
    walker.script.lua_script = "Walker_Script";
    walker.script.categories = {"infantry", "landhero"};
    walker.script.combat_rating = 42.0F;
    walker.combat.shield = 10.0F;
    eaw::SpawnableType broken;
    broken.type_name = "Broken";
    broken.script.lua_script = "No_Such_Script";
    const std::array<eaw::SpawnableType, 2> types{walker, broken};
    eaw::MapEntitySpawn beacon;
    beacon.spawn.type_name = "Beacon";
    beacon.spawn.owner = 2;
    beacon.spawn.position = {300.0F, 0.0F, 0.0F};
    beacon.script.lua_script = "Guard";
    beacon.hint = "Gate";
    eaw::BattleMap map;
    map.entities = std::span(&beacon, 1);
    map.spawnable = types;

    eaw::Battle battle(map, "TEST.TED");
    require(battle.hinted_entities().size() == 1 && battle.type_info("BEACON")->lua_script == "Guard" && battle.type_info("walker")->lua_script == "Walker_Script",
            "the battle must keep hints and type scripts");
    eaw::BattleScripts scripts(vfs);
    scripts.run(battle);
    require(scripts.size() == 1, "a placed object's type script must start with the battle");
    expect_ok(battle.submit({.order = eaw::BattleOrder::spawn, .point = {0.0F, 0.0F, 0.0F}, .type = "Walker", .owner = 1}), "spawning the walker");
    expect_ok(battle.submit({.order = eaw::BattleOrder::spawn, .point = {0.0F, 300.0F, 0.0F}, .type = "Broken", .count = 2, .owner = 1}), "spawning two broken objects");
    battle.step();
    scripts.run(battle);
    require(scripts.size() == 2, "a spawned entity must get its type's script, and a missing script must start nothing");
    const auto walker_id = battle.world().snapshot().entities[1].id;
    const auto sounds = scripts.sound_requests();
    require(sounds.size() == 1 && sounds[0].event == "Walker_Hello" && sounds[0].object == walker_id && sounds[0].position.x == 0.0F,
            "Play_SFX_Event must ask for the sound at the object, for the driver to play");
    battle.step();
    scripts.run(battle);
    require(scripts.sound_requests().empty(), "sound requests must last one script phase");
    for (int tick = 0; tick < 10; ++tick)
    {
        battle.step();
        scripts.run(battle);
    }
    // Read the walker's script before the beacon's proximity handler despawns the walker.
    const auto walker_output = scripts.host(1).output();
    require(std::ranges::count(walker_output, "walker Walker Space true true false 300") == 1,
            "a spawned entity's script must see its own object, the game mode, its categories, and distances in the ground plane");
    require(std::ranges::count(walker_output, "shield 1 hero true rating 42") == 1, "scripts must read shields, the hero flag, and the combat rating");
    require(std::ranges::count(walker_output, "WALKER_SCRIPT.LUA") == 1 && std::ranges::count(walker_output, "debug line") == 1,
            "Script must name the script, and debug messages must reach the output");
    require(battle.world().snapshot().entities[1].id == walker_id && scripts.host(1).errors().empty(), "the walker's script must run without errors");
    expect_ok(battle.submit({.order = eaw::BattleOrder::move, .entities = {walker_id}, .point = {300.0F, 0.0F, 0.0F}}), "moving the walker to the beacon");
    for (int tick = 0; tick < 300; ++tick)
    {
        battle.step();
        scripts.run(battle);
    }
    const auto& guard = scripts.host(0).output();
    const auto count = [&](const std::string_view line)
    {
        return std::ranges::count(guard, line);
    };
    require(count("hint true 1 nil") == 1, "Find_Hint must find the hinted object by type and hint ignoring case, and nothing for another type");
    require(count("near Walker") == 1, "the proximity handler must run once the walker is in range, and not after it cancelled itself");
    require(scripts.host(0).errors().empty(), "the beacon's script must run without errors");
    require(!battle.world().alive(walker_id) && scripts.size() == 1, "a despawned object must leave the battle, and its script end with it");
    std::filesystem::remove_all(root, ignored);
}

const ModuleRegistrar registrar("battle", {
                                              {"test_combat_content", &test_combat_content},
                                              {"test_battle_scripts", &test_battle_scripts},
                                              {"test_battle_object_scripts", &test_battle_object_scripts},
                                              {"test_battle_orders", &test_battle_orders},
                                              {"test_battle_log", &test_battle_log},
                                              {"test_runtime_spawnable_registration", &test_runtime_spawnable_registration},
                                          });

} // namespace
