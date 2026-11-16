# Game-object tag status

The working list for implementing the tags of [xml-object-tags.md](xml-object-tags.md): every tag of
its catalogue with what the engine does with it. The catalogue holds the evidence (users, values,
meaning); this file holds only the status, and is updated in the same change as the code. Work goes
through the `gap` and `partial` rows family by family; `needs` rows wait for their system. After
changing rows, `scripts/update-tag-status.ps1` recounts the summary and the family lines.

| Status | Meaning |
| --- | --- |
| `done` | Read, and the behavior the catalogue describes is implemented. |
| `partial` | Read, but part of the behavior is missing (the note says which). |
| `gap` | An existing engine system should honor it and does not yet. |
| `needs <system>` | Waits for a system the engine does not have (see the list below). |
| `n/a` | Not to implement: editor-only, debug, dead, or an implementation artifact. |

The first classification (2026-11-11) was made from the catalogue: tags the code reads by name,
then each catalogue subsection's subject, then per-tag corrections. Rows are corrected as work
reaches them; a `gap` that turns out to need a new system moves to `needs`, and the reverse.
A second pass over the gameplay rows (2026-11-13) moved 22 such rows: holstering, the player's
announcements and faction filters (player layer), hero teams, and wrecks that block movement
(wreck entities, with the simulation rewrite).
Ability fields and classes are listed as the catalogue lists them (one row per field or class).

## Summary

| Status | Tags |
| --- | --- |
| `done` | 187 |
| `partial` | 24 |
| `gap` | 181 |
| `needs` | 705 |
| `n/a` | 20 |
| total | 1117 |

Systems that `needs` rows wait for, by tag count:

| System | Tags |
| --- | --- |
| abilities | 149 |
| galactic | 119 |
| production | 91 |
| status effects | 55 |
| UI | 48 |
| super weapons | 34 |
| hero clash | 23 |
| battle rules | 20 |
| deploying | 19 |
| unit spawning | 17 |
| fog of war | 16 |
| radar | 14 |
| AI | 11 |
| cinematics | 11 |
| player layer | 11 |
| garrisons | 9 |
| transports | 9 |
| holstering | 8 |
| space hazards | 8 |
| tactical overview | 8 |
| secondary locomotor | 5 |
| reinforcement | 4 |
| base power | 3 |
| hero teams | 3 |
| stealth | 3 |
| repair | 2 |
| retreat | 2 |
| wreck entities | 2 |
| weather | 1 |

## Movement, locomotion, formations, and collision

40 done, 7 partial, 40 gap, 19 needs, 7 n/a.

### Speed and acceleration

| Tag | Status | Note |
| --- | --- | --- |
| `Max_Speed` | `done` |  |
| `Min_Speed` | `partial` | read only for FIGHTER_LOCOMOTOR types |
| `Alternate_Max_Speed` | `needs secondary locomotor` | jetpack and phase heroes |
| `OverrideAcceleration` | `done` | times the space speed multiplier in space (recorded) |
| `OverrideDeceleration` | `done` |  |
| `Override_Acceleration` | `done` |  |
| `Override_Deceleration` | `done` |  |
| `Max_Thrust` | `gap` |  |
| `LateralAcceleration` | `gap` |  |
| `Mass` | `gap` |  |
| `Defend_Mode_Speed_Multiplier` | `needs abilities` | defend mode |
| `On_Fire_Speed_Modifier` | `needs status effects` | burning |
| `Hyperspace` | `needs reinforcement` | hyperspace arrival |
| `Hyperspace_Speed` | `needs reinforcement` | hyperspace arrival |
| `Can_Hyperspace_Without_Activating_Ability` | `needs galactic` |  |
| `Disallows_Hyperspace_Retreat` | `needs galactic` |  |
| `Hyperspace_Fleet_Reveal_Range` | `needs galactic` |  |
| `Space_Full_Stop_Command` | `needs UI` | command button |

### Turning and flight attitude

| Tag | Status | Note |
| --- | --- | --- |
| `Max_Rate_Of_Turn` | `done` | times the space speed multiplier in space (recorded); land unscaled (not observed) |
| `Alternate_Max_Rate_Of_Turn` | `needs secondary locomotor` | jetpack and phase heroes |
| `Stopped_Rate_Of_Turn` | `done` |  |
| `Min_Speed_Fraction_For_Turn` | `done` |  |
| `Close_Enough_Angle_For_Move_Start` | `done` | the default (45) disagrees with GameConstants (180); see the roadmap. Walkers with turn clips use it for a new destination while walking as well (2026-11-14) |
| `Begin_Turn_Towards_Distance` | `gap` |  |
| `Bank_Turn_Angle` | `partial` | limits the flyers' bank; capital ships do not bank yet |
| `Max_Rate_Of_Roll` | `gap` | needs per-entity bank state in presentation |
| `Max_Lift` | `gap` | likely a vertical speed limit, but several fighters say 0.0f |
| `MaxFacingLookAheadFrames` | `gap` |  |
| `UseSecondaryFacing` | `gap` |  |
| `SecondaryTurnAngle` | `gap` |  |
| `SecondaryTurnInPlaceROTCoefficient` | `gap` |  |
| `MinSecondaryTurnROTCoefficient` | `gap` |  |
| `MaxSecondaryTurnROTCoefficient` | `gap` |  |
| `SecondaryTurnLookaheadDistance` | `gap` |  |
| `Has_Pre_Turn_Anim` | `done` | turns in place played as clips: BEGIN, whole loop, HALF, and QUARTER cycles, END (the simulation plans them; 2026-11-14) |
| `Walk_Transition` | `done` | walks played as clips: MOVESTART, MOVE strides, a MOVE_END clip by the stride's part (the simulation plans them; 2026-11-14) |
| `Locomotor_Has_Animation_Priority` | `done` | both of the above; since the owner's decision of 2026-11-14 every walker with transition clips is played the same way, so the field no longer changes the outcome (what it means in the original is not observed) |
| `Should_Cause_Limited_Turrets_To_Turn` | `done` | no: the hull does not turn to a target beyond the arc |
| `Movement_Directions_Count` | `gap` |  |

### Locomotors and movement class

| Tag | Status | Note |
| --- | --- | --- |
| `MovementClass` | `done` |  |
| `Primary_Locomotor_Name` | `needs secondary locomotor` | jetpack and phase heroes |
| `Secondary_Locomotor_Name` | `needs secondary locomotor` | jetpack and phase heroes |
| `Uses_Multiple_Locomotors` | `needs secondary locomotor` | jetpack and phase heroes |
| `Hover_Offset` | `done` |  |
| `Hover_Height` | `gap` |  |
| `Prepare_Strafe_Height` | `gap` |  |
| `Strafe_Distance` | `gap` |  |
| `Minimum_Follow_Distance` | `gap` |  |
| `Avoid_Enemy_Exclusion_Range` | `gap` |  |
| `Avoidance_Disabled` | `gap` |  |
| `PathFrameDelay` | `n/a` | implementation artifact (path update cadence) |
| `MovementPredictionInterval` | `gap` |  |
| `MovementBoxExpansionFactor` | `gap` |  |
| `Snap_Movement_Orders_To_Center` | `gap` |  |
| `Autonomous_Move_Extension_Vs_Attacker` | `gap` |  |
| `Max_Distance_From_Spawner` | `needs unit spawning` |  |
| `Is_Stationary_When_Attacking` | `partial` | not read: every ground unit without a TURRET behavior stops to attack, as most infantry do in the original (owner's recollection), which covers the 11 Yes users; the one explicit No (Dark Trooper phase I, melee) is to check |
| `Destroy_When_Stunned_Over_Water` | `needs status effects` | stun |
| `Stationary_Space_Layer` | `gap` |  |

### Space layers and space obstacles

| Tag | Status | Note |
| --- | --- | --- |
| `Space_Layer` | `done` |  |
| `Space_Obstacle_Offset` | `gap` | 0 0 0 in nearly all stock uses |
| `Space_Obstacle_Radius` | `done` | ships steer around it; fields, nebulae, and ion storms excepted |
| `Debris_Movement_Vector` | `done` | presentation only (debris.hpp); units per 30 Hz frame in the frame of the unit it broke off (an engine choice) |

### Footprint, extents, and passability

| Tag | Status | Note |
| --- | --- | --- |
| `Custom_Soft_Footprint_Radius` | `done` |  |
| `Custom_Footprint_Radius` | `done` | separation radius when no soft radius is given |
| `Custom_Hard_XExtent` | `partial` | obstacle footprints only; mobile units and death clones do not use it yet |
| `Custom_Hard_YExtent` | `partial` | as Custom_Hard_XExtent |
| `Custom_Hard_XExtent_Offset` | `partial` | as Custom_Hard_XExtent |
| `Custom_Hard_YExtent_Offset` | `partial` | as Custom_Hard_XExtent |
| `Collision_Box_Modifier` | `gap` |  |
| `Collision_Mesh` | `gap` |  |
| `Obstacle_Width` | `done` | obstacle footprint rectangle |
| `Obstacle_Height` | `done` |  |
| `Obstacle_X_Offset` | `done` |  |
| `Obstacle_Y_Offset` | `done` |  |
| `Obstacle_Proxy_Type` | `done` |  |
| `OverridePassability` | `done` |  |
| `SecondaryOccupationPassability` | `gap` |  |
| `Fine_Tune_Occupied_Passability` | `n/a` | never enabled in stock |
| `OccupationStyle` | `n/a` | one value (1x1) in stock |
| `UnitCollisionClass` | `done` |  |
| `MaxJiggleDistance` | `gap` |  |
| `MinimumPushReturnDistance` | `gap` |  |

### Crushing

| Tag | Status | Note |
| --- | --- | --- |
| `Is_Squashable` | `done` | crushers: vehicle collision classes (an engine choice) |
| `Is_Squashable_By_Supercrusher` | `done` |  |
| `Is_Supercrusher` | `done` |  |
| `Squash_Damage_Type` | `gap` | one user (Dust_Devil); the crush kills outright |
| `Cache_Crusher_Boxes` | `n/a` | implementation artifact (a cache) |

### Formations

| Tag | Status | Note |
| --- | --- | --- |
| `FormationOrder` | `done` |  |
| `FormationSpacing` | `done` |  |
| `FormationGrouping` | `done` | Solo units take no slot and move on their own (from the name and users) |
| `FormationRaggedness` | `done` | times the slot pitch (scale an engine choice, to tune) |
| `Formation_Priority` | `gap` | meaning unknown (precedence when formations combine?) |
| `FormationPriority` | `gap` |  |
| `Formation_Prority` | `n/a` | single-use alternate spelling; read together with FormationPriority |
| `Formation_Formup_Wait_Style` | `n/a` | no formup step; GameConstants ShouldSkipLandFormup |
| `WaitsForFormationFormup` | `n/a` | as Formation_Formup_Wait_Style |
| `Ignore_For_Reoptimization` | `gap` |  |

### Animation rates tied to movement

| Tag | Status | Note |
| --- | --- | --- |
| `Movement_Animation_Speed` | `done` | speed (Max_Speed units) at which the move clip plays at 1x; measured with `gait` (compatibility-research.md, "Animation speeds") |
| `Walk_Animation_Speed` | `done` | as Movement_Animation_Speed, for the walk clip |
| `Rotation_Animation_Speed` | `done` | turn rate (degrees per frame) at which turn and turn-transition clips play at 1x; by analogy with Movement_Animation_Speed, it equals Max_Rate_Of_Turn on most types with turn clips |
| `Crouch_Animation_Speed` | `needs abilities` | crouch clips play under Spread Out |
| `Tread_Scroll_Rate` | `gap` |  |
| `Cinematic_Anim_Speed` | `needs cinematics` |  |

### Off-topic name matches

| Tag | Status | Note |
| --- | --- | --- |
| `Turret_Rotate_Extent_Degrees` | `done` | half each way (a guess); aim only, not firing arcs |
| `Turret_Elevate_Extent_Degrees` | `done` | half each way (a guess) |
| `Turret_Rotate_Speed` | `done` | per frame (a guess) |
| `Location_Follows_Terrain` | `gap` |  |
| `Min_Height_Above_Terrain` | `gap` |  |
| `MinHeightAboveTerrain` | `gap` |  |
| `Use_Splines` | `needs cinematics` |  |
| `Particle_Attach_To_Collision` | `gap` |  |
| `Secondary_Objective` | `needs battle rules` | objective marker |

## Health, shields, armor, weapons, targeting, turrets, and death

55 done, 3 partial, 42 gap, 35 needs.

### Health, shields, armor, and energy

| Tag | Status | Note |
| --- | --- | --- |
| `Tactical_Health` | `done` |  |
| `Shield_Points` | `done` |  |
| `Shield_Refresh_Rate` | `done` | points per ShieldRechargeIntervalInSecs, stepped (recorded) |
| `Armor_Type` | `done` |  |
| `Shield_Armor_Type` | `done` |  |
| `Energy_Capacity` | `needs abilities` | energy pool |
| `Energy_Refresh_Rate` | `needs abilities` | energy pool |
| `Autoresolve_Health` | `needs galactic` | auto-resolve |
| `Immune_To_Damage` | `done` |  |
| `GUI_Hide_Health_Bar` | `needs UI` |  |
| `Out_Of_Combat_Defense_Adjustment` | `gap` |  |
| `Defend_Mode_Shield_Regen_Multiplier` | `needs abilities` | defend mode |
| `Defend_Mode_Energy_Regen_Multiplier` | `needs abilities` | defend mode |
| `Defend_Mode_Weapon_Delay_Multiplier` | `needs abilities` | defend mode |
| `Surface_Type_Cover_Damage_Shield` | `gap` |  |
| `Is_Combustible` | `needs status effects` | burning |
| `Burning_Damage_Per_Second` | `needs status effects` | burning |
| `Targeting_Allowed_When_Burning` | `needs status effects` | burning |

### Ground base shields and shield presentation

| Tag | Status | Note |
| --- | --- | --- |
| `Base_Shield_Radius` | `gap` |  |
| `Base_Shield_Always_Off` | `gap` |  |
| `Base_Shield_Penetration_Particle` | `gap` |  |
| `Shield_Normal_Color` | `gap` |  |
| `Shield_Upgraded_Color` | `gap` |  |
| `Shield_On_Anim` | `gap` |  |
| `Shield_Off_Anim` | `gap` |  |
| `Shield_Requires_Base_Power` | `needs base power` |  |
| `Shield_Sub_Mesh` | `gap` |  |
| `Shield_Hit_Particles` | `gap` |  |
| `Damage_Hit_Particles` | `gap` |  |

### Hardpoint lists and hardpoint parts

| Tag | Status | Note |
| --- | --- | --- |
| `HardPoints` | `done` |  |
| `Health` | `done` |  |
| `Is_Destroyable` | `done` |  |
| `Is_Targetable` | `done` |  |
| `Damage_Particles` | `done` |  |
| `Damage_Decal` | `done` |  |
| `Death_Explosion_Particles` | `done` | the first entry |
| `Death_Breakoff_Prop` | `done` | debris broken off a destroyed hardpoint, presentation only (also on land, where the original is said to leave it out) |
| `Repair_Amount_Per_Frame` | `needs repair` |  |
| `Repair_Cost_Per_Frame` | `needs repair` |  |
| `Should_Be_Destroyed_When_All_Hardpoints_Destroyed` | `done` | default yes; the hull follows its parts (recorded) |
| `Shield_Generator_Hardpoint` | `needs cinematics` |  |
| `Weapon_Hardpoint` | `needs cinematics` |  |

### Hardpoint weapons

| Tag | Status | Note |
| --- | --- | --- |
| `Fire_Projectile_Type` | `done` |  |
| `Fire_Range_Distance` | `done` |  |
| `Fire_Min_Recharge_Seconds` | `done` |  |
| `Fire_Max_Recharge_Seconds` | `done` |  |
| `Fire_Pulse_Count` | `done` |  |
| `Fire_Pulse_Delay_Seconds` | `done` |  |
| `Fire_Bone_A` | `done` |  |
| `Fire_Bone_B` | `done` | alternates with A (inference) |
| `Fire_Cone_Width` | `done` |  |
| `Fire_Cone_Height` | `gap` |  |
| `Fire_Inaccuracy_Distance` | `done` |  |
| `Fire_Category_Restrictions` | `done` | on hardpoints and units |
| `Randomize_Between_Fire_Bones` | `gap` |  |
| `Full_Salvo_Weapon_Delay_Multiplier` | `needs abilities` | energy pool |
| `Requires_Manual_Target_Assignment` | `gap` |  |
| `Manual_Hardpoint_Firing_Cooldown_Secs` | `gap` |  |
| `Allow_Opportunity_Fire_When_Targeting` | `gap` |  |
| `Allow_Opportunity_Fire_When_Idle` | `gap` |  |

### Mode-dependent firing

| Tag | Status | Note |
| --- | --- | --- |
| `Fire_When_In_Normal_Attack_Mode` | `needs abilities` |  |
| `Fire_When_In_Rocket_Attack_Mode` | `needs abilities` |  |
| `Fire_Weapon_When_In_Normal_Attack_Mode` | `needs abilities` |  |
| `Fire_Weapon_When_In_Rocket_Attack_Mode` | `needs abilities` |  |
| `Fire_When_In_Power_To_Weapons_Mode` | `needs abilities` |  |
| `Power_To_Weapons_Mode_Weapon_Delay_Multiplier` | `needs abilities` |  |
| `Should_Attacker_Hold_Fire_For_Special_Ability` | `needs abilities` |  |

### Targeting and accuracy

| Tag | Status | Note |
| --- | --- | --- |
| `Targeting_Max_Attack_Distance` | `done` |  |
| `Targeting_Min_Attack_Distance` | `done` | the unit's own weapon, not hardpoints |
| `Ranged_Target_Z_Adjust` | `done` |  |
| `Target_Bones` | `gap` |  |
| `Targeting_Priority_Set` | `done` |  |
| `Targeting_Fire_Inaccuracy` | `done` |  |
| `Targeting_Fire_Inaccuracy_Fixed_Radius` | `done` |  |
| `Targeting_Stickiness_Time_Threshold` | `done` | re-weighs automatic targets |
| `Targeting_Land_Model_Stay_Horiz_Flat` | `gap` |  |
| `Target_Stealth_Units` | `needs stealth` |  |
| `Is_Valid_Target` | `done` | never chosen or kept as a target |
| `Collidable_By_Projectile_Living` | `done` |  |
| `Collidable_By_Projectile_Dead` | `gap` |  |
| `Attack_Category_Restrictions` | `done` | applies to all the unit's weapons |
| `Attack_Animation_Is_Overlay` | `done` | ATTACK and ATTACKIDLE add their bones' change since their first frame to the locomotion (2026-11-14; an engine reading: the pod walker's attack recoils its whole body) |

### Turrets

| Tag | Status | Note |
| --- | --- | --- |
| `Turret_Bone_Name` | `done` | unit turrets; hardpoint turrets not yet |
| `Barrel_Bone_Name` | `done` | barrel pitch; the turret weapon fires from it |
| `Apply_Y_Turret_Rotate_To_Axis` | `gap` | yaw is about the model up axis instead |
| `Apply_Z_Turret_Rotate_To_Axis` | `gap` | pitch is about the model horizontal instead |
| `Turret_Rest_Angle` | `gap` |  |
| `Turret_Rotation_Offset` | `gap` |  |
| `Is_Turret` | `gap` |  |
| `Turret_XY_Only` | `gap` |  |
| `Turret_Targets_Ground_Vehicles` | `gap` |  |
| `Turret_Targets_Ground_Infantry` | `gap` |  |
| `Turret_Targets_Air_Vehicles` | `gap` |  |
| `Turret_Targets_Anything_Else` | `gap` |  |
| `Ground_Vehicle_Turret_Target` | `gap` |  |
| `Ground_Infantry_Turret_Target` | `gap` |  |
| `Air_Vehicle_Turret_Target` | `gap` |  |
| `Fires_Forward` | `gap` |  |

### Engagement ranges and escorts

| Tag | Status | Note |
| --- | --- | --- |
| `Guard_Chase_Range` | `done` | the guard leash |
| `Idle_Chase_Range` | `gap` | idle units do not chase attackers yet |
| `Attack_Move_Response_Range` | `done` |  |
| `Harass_Enemy_Exclusion_Range` | `needs AI` |  |
| `Is_Escort` | `needs AI` |  |
| `Space_Escort_Unit_Types` | `needs AI` |  |

### Death, death clones, and wrecks

| Tag | Status | Note |
| --- | --- | --- |
| `Death_Explosions` | `done` |  |
| `Death_Explosions_End` | `done` | played where the wreck lay when it goes; wrecks of meshes without a DIE clip leave nothing to end |
| `Death_Clone` | `partial` | the clone is spawned; damage-type-specific clones are not chosen |
| `Death_Clone_Is_Obstacle` | `needs wreck entities` | needs wrecks as simulation entities |
| `IsDeathClone` | `gap` |  |
| `IsDeathCloneObstacle` | `needs wreck entities` | as Death_Clone_Is_Obstacle |
| `Death_Persistence_Duration` | `done` |  |
| `Death_Fade_Time` | `gap` | needs a per-instance fade in the renderer |
| `Remove_Upon_Death` | `done` | no corpse |
| `Death_Leave_Hulk_Behind` | `gap` |  |
| `Death_Projectiles` | `done` | the projectile's blast where it dies |
| `Retreat_Self_Destruct_Explosion` | `needs retreat` |  |
| `Spin_Away_On_Death` | `partial` | delayed death out of control; the spin is not drawn |
| `Spin_Away_On_Death_Chance` | `done` |  |
| `Spin_Away_On_Death_Time` | `done` |  |
| `Spin_Away_On_Death_Explosion` | `done` |  |
| `Damaged_Smoke_Asset_Name` | `gap` |  |
| `Destruction_Survivors` | `done` | each listed type once (all stock weights are 1.0) |
| `Minimum_Destruction_Survivor_Count` | `done` | the count spawned |
| `Maximum_Destruction_Survivor_Count` | `partial` | not read: the minimum is spawned (deterministic) |
| `Death_By_TSW_Replacements` | `needs super weapons` |  |

### Break-off debris (on the props `Death_Breakoff_Prop` names)

| Tag | Status | Note |
| --- | --- | --- |
| `Debris_Attached_Particle` | `done` | the trail of the debris piece |
| `Debris_Facing_Rotate_Vector` | `done` | degrees per 30 Hz frame (an engine choice) |
| `Debris_Min_Lifetime_Seconds` | `done` |  |
| `Debris_Max_Lifetime_Seconds` | `done` |  |

### Other single-purpose tags

| Tag | Status | Note |
| --- | --- | --- |
| `Combat_Distance` | `needs hero clash` |  |
| `Damage_Percentage` | `needs hero clash` |  |
| `Auto_Destruct_Time` | `gap` |  |
| `Auto_Destruct_Fade_Time` | `gap` |  |
| `Weapon_Type` | `needs super weapons` | planetary ion cannon |
| `Weapon_Quantity` | `needs super weapons` | planetary ion cannon |

## Projectiles and damage delivery

31 done, 3 partial, 10 gap, 49 needs, 2 n/a.

### Firing cadence (on the shooter)

| Tag | Status | Note |
| --- | --- | --- |
| `Projectile_Types` | `partial` | only the first entry fires; the second waits for SWAP_WEAPONS |
| `Projectile_Fire_Recharge_Seconds` | `done` |  |
| `Projectile_Fire_Pulse_Count` | `done` |  |
| `Projectile_Fire_Pulse_Delay_Seconds` | `done` |  |
| `Projectile_Appearance_Delay_Frames` | `done` | first number of the two-number form |

### Damage and damage type

| Tag | Status | Note |
| --- | --- | --- |
| `Projectile_Damage` | `partial` | the per-shooter value differs from the projectile on 25 objects; unverified which wins |
| `Damage_Type` | `done` |  |
| `Projectile_Does_Hitpoint_Damage` | `done` |  |
| `Projectile_Does_Shield_Damage` | `done` |  |
| `Projectile_Does_Energy_Damage` | `needs abilities` | energy pool |
| `Projectile_Energy_Per_Shot` | `needs abilities` | energy pool |
| `Projectile_Damages_Random_Hard_Points` | `done` |  |
| `Friendly_Damage_Amount` | `done` | the blast's damage to the shooter's own entities |
| `Projectile_Damage_Delay_Secs` | `gap` |  |
| `Max_Secs_For_AE_Delayed_Damage` | `n/a` | 0 in every stock use |
| `Damage` | `needs AI` | obsolete AI strength rating (the code literal is unrelated) |
| `Damage_Amount` | `needs hero clash` |  |
| `Projectile_Block_Chance_Modifier` | `needs abilities` | deflection |
| `Projectile_Redirect_Chance_Modifier` | `needs abilities` | deflection |

### Area blast

| Tag | Status | Note |
| --- | --- | --- |
| `Projectile_Blast_Area_Damage` | `done` | hostiles only |
| `Projectile_Blast_Area_Range` | `done` | measured to the footprint |
| `Projectile_Blast_Area_Dropoff` | `done` |  |
| `Projectile_Blast_Area_Dropoff_Tiers` | `done` | equal rings (inference) |
| `Projectile_Blast_Area_Immune_Faction` | `needs player layer` | blasts spare friends already; factions are not in the simulation |

### Flight and guidance

| Tag | Status | Note |
| --- | --- | --- |
| `Projectile_Category` | `gap` | shapes follow the gravity and curve tags instead |
| `Projectile_Max_Flight_Distance` | `partial` | 0 means "detonate at once" in the data (death and ability blasts, which the engine detonates where they start), "unlimited" for fired shots |
| `Projectile_Max_Lifetime` | `done` |  |
| `Projectile_Acceleration_Per_Frame` | `n/a` | 0 in both stock uses; comments say it must stay 0 |
| `Projectile_Max_Scan_Range` | `done` | retargets when the target dies |
| `Projectile_Target_Point_On_Terrain` | `gap` |  |
| `Explode_When_Reached_Target_Radius` | `gap` |  |
| `Projectile_Rocket_Straight_Distance` | `done` | drawn sway (presentation) |
| `Projectile_Rocket_Curve_Distance` | `gap` | the sway is one half sine, not curve points |
| `Projectile_Rocket_Curve_Offset` | `done` | half-sine sway, at most 50 |
| `Projectile_Grenade_Gravity` | `done` | drawn lob (presentation) |
| `Projectile_Grenade_Gravity_Lob_Mod` | `done` |  |
| `Projectile_Grenade_Can_Lob_Slower` | `gap` |  |
| `Projectile_Grenade_Sticks_On_Collision` | `gap` |  |
| `Projectile_Bomb_Fall_Accel_Rate` | `gap` |  |

### Detonation side effects

| Tag | Status | Note |
| --- | --- | --- |
| `Projectile_Stun_On_Detonation` | `needs status effects` |  |
| `Projectile_Stun_Duration_Frames` | `needs status effects` |  |
| `Projectile_Stun_Radius` | `needs status effects` |  |
| `Projectile_Stun_Spawn_Effect` | `needs status effects` |  |
| `Projectile_Stun_Victims_Category_Mask` | `needs status effects` |  |
| `Projectile_Stun_Victims_Unit_Types` | `needs status effects` |  |
| `Projectile_Ion_Stun_On_Detonation` | `needs status effects` |  |
| `Projectile_Ion_Stun_Duration` | `needs status effects` |  |
| `Projectile_Ion_Stun_Radius` | `needs status effects` |  |
| `Projectile_Ion_Stun_Speed_Reduction_Percent` | `needs status effects` |  |
| `Projectile_Ion_Stun_Shot_Rate_Reduction_Percent` | `needs status effects` |  |
| `Projectile_Ion_Stun_Stack_Duration` | `needs status effects` |  |
| `Projectile_Disable_Engines_Duration` | `needs status effects` |  |
| `Projectile_Disables_Engines_When_Power_Drained` | `needs status effects` |  |
| `Projectile_Instant_Heal_On_Detonation` | `needs status effects` |  |
| `Projectile_Instant_Heal_Health_Increase` | `needs status effects` |  |
| `Projectile_Instant_Heal_Radius` | `needs status effects` |  |
| `Projectile_Instant_Heal_Duration_Frames` | `needs status effects` |  |
| `Projectile_Instant_Heal_Spawn_Effect` | `needs status effects` |  |
| `Projectile_Instant_Heal_Targets_Category_Mask` | `needs status effects` |  |
| `Projectile_Cause_Invulnerability_On_Detonation` | `needs status effects` |  |
| `Projectile_Cause_Invulnerability_Duration_Frames` | `needs status effects` |  |
| `Projectile_Cause_Invulnerability_Max_Targets` | `needs status effects` |  |
| `Projectile_Cause_Invulnerability_Radius` | `needs status effects` |  |
| `Projectile_Cause_Invulnerability_Spawn_Effect` | `needs status effects` |  |
| `Projectile_Cause_Invulnerability_Targets_Category_Mask` | `needs status effects` |  |
| `Projectile_Convert_Enemy_On_Detonation` | `needs status effects` |  |
| `Projectile_Convert_Enemy_Radius` | `needs status effects` |  |
| `Projectile_Convert_Enemy_Spawn_Effect` | `needs status effects` |  |
| `Projectile_Convert_Enemy_Targets_Category_Mask` | `needs status effects` |  |
| `Projectile_Weaken_Enemy_On_Detonation` | `needs status effects` |  |
| `Projectile_Weaken_Enemy_Radius` | `needs status effects` |  |
| `Projectile_Weaken_Enemy_Duration_Seconds` | `needs status effects` |  |
| `Projectile_Weaken_Enemy_Cause_Damage_Reduction_Percent` | `needs status effects` |  |
| `Projectile_Weaken_Enemy_Take_Damage_Increase_Percent` | `needs status effects` |  |
| `Projectile_Weaken_Enemy_Spawn_Effect` | `needs status effects` |  |
| `Projectile_Weaken_Enemy_Targets_Category_Mask` | `needs status effects` |  |
| `Projectile_Combat_Mod` | `needs status effects` |  |
| `Projectile_Combat_Mod_Value` | `needs status effects` |  |
| `Projectile_Combat_Mod_Duration` | `needs status effects` |  |
| `Projectile_Combat_Mod_Effect` | `needs status effects` |  |
| `Projectile_Combat_Mod_Category_Mask` | `needs status effects` |  |

### Detonation and creation effects

| Tag | Status | Note |
| --- | --- | --- |
| `Projectile_Object_Detonation_Particle` | `done` |  |
| `Projectile_Object_Armor_Reduced_Detonation_Particle` | `done` |  |
| `Projectile_Absorbed_By_Shields_Particle` | `done` |  |
| `Projectile_Ground_Detonation_Particle` | `done` |  |
| `Projectile_Lifetime_Detonation_Particle` | `done` |  |
| `Projectile_Ground_Detonation_SurfaceFX` | `gap` |  |
| `Projectile_Object_Creation_Particle` | `done` |  |
| `Explosion_Jitter_Factor` | `gap` |  |
| `Explosion_Scorch_Mark` | `done` | through SurfaceFX's default surface to its decal, on land within 40 units of the ground |

### Custom bolt rendering

| Tag | Status | Note |
| --- | --- | --- |
| `Projectile_Custom_Render` | `done` |  |
| `Projectile_Width` | `done` |  |
| `Projectile_Length` | `done` |  |
| `Projectile_Texture_Slot` | `done` |  |
| `Projectile_Laser_Color` | `done` |  |

## Abilities: Abilities and Unit_Abilities_Data blocks

113 needs.

### Shared ability fields

| Tag | Status | Note |
| --- | --- | --- |
| `Activation_Style` | `needs abilities` |  |
| `Applicable_Unit_Categories` | `needs abilities` |  |
| `Applicable_Unit_Types` | `needs abilities` |  |
| `Excluded_Unit_Types` | `needs abilities` |  |
| `Unit_Strength_Category` | `needs abilities` |  |
| `Stacking_Category` | `needs abilities` |  |
| `Apply_To_All_Allies` | `needs abilities` |  |
| `Affects_All_Allies` | `needs abilities` |  |
| `Affects_All_Allied_Sources` | `needs abilities` |  |
| `Initially_Enabled` | `needs abilities` |  |
| `Causes_Despawn` | `needs abilities` |  |
| `SFXEvent_Activate` | `needs abilities` |  |
| `SFXEvent_Target_Affected` | `needs abilities` |  |
| `Owner_Particle_Effect` | `needs abilities` |  |
| `Owner_Particle_Bone_Name` | `needs abilities` |  |
| `Target_Particle_Effect` | `needs abilities` |  |
| `Target_Particle_Bone_Name` | `needs abilities` |  |
| `Owner_Light_Effect_Type` | `needs abilities` |  |
| `_Color` | `needs abilities` |  |
| `_Color2` | `needs abilities` |  |
| `_Duration` | `needs abilities` |  |
| `_Pulse_Count` | `needs abilities` |  |
| `Target_Light_Effect_*` | `needs abilities` |  |
| `Activation_Min_Range` | `needs abilities` |  |
| `Activation_Max_Range` | `needs abilities` |  |
| `Activation_Chance` | `needs abilities` |  |
| `Damage_Interval_In_Secs` | `needs abilities` |  |
| `Must_Be_Bought_On_Black_Market` | `needs abilities` |  |
| `Duration_In_Secs` | `needs abilities` |  |
| `Duration_In_Seconds` | `needs abilities` |  |
| `Terminate_Effect_On_Move_Command` | `needs abilities` |  |

### Ability classes

| Tag | Status | Note |
| --- | --- | --- |
| `Combat_Bonus_Ability` | `needs abilities` |  |
| `Force_Healing_Ability` | `needs abilities` |  |
| `Redirect_Blaster_Ability` | `needs abilities` |  |
| `Battlefield_Modifier_Ability` | `needs abilities` |  |
| `Income_Stream_Mod_Ability` | `needs abilities` |  |
| `Reduce_Production_Price_Ability` | `needs abilities` |  |
| `System_Spy_Ability` | `needs abilities` |  |
| `Income_Stream_Ability` | `needs abilities` |  |
| `Galactic_Stealth_Ability` | `needs abilities` |  |
| `Galaxy_Wide_Upgrade_Ability` | `needs abilities` |  |
| `Enable_Radar_Ability` | `needs abilities` |  |
| `Remove_Corruption_Ability` | `needs abilities` |  |
| `Stealth_Ability` | `needs abilities` |  |
| `Enable_Ability` | `needs abilities` |  |
| `Starbase_Upgrade_Ability` | `needs abilities` |  |
| `Enhance_Defense_Ability` | `needs abilities` |  |
| `Drain_Life_Ability` | `needs abilities` |  |
| `Tractor_Beam_Attack_Ability` | `needs abilities` |  |
| `Ion_Cannon_Shot_Attack_Ability` | `needs abilities` |  |
| `Neutralize_Hero_Ability` | `needs abilities` |  |
| `Arc_Sweep_Attack_Ability` | `needs abilities` |  |
| `Earthquake_Attack_Ability` | `needs abilities` |  |
| `Sensor_Jamming_Ability` | `needs abilities` |  |
| `Absorb_Blaster_Ability` | `needs abilities` |  |
| `Siphon_Credits_Ability` | `needs abilities` |  |
| `Generic_Attack_Ability` | `needs abilities` |  |
| `Base_Power_Ability` | `needs abilities` |  |
| `Remote_Bomb_Ability` | `needs abilities` |  |
| `Reduce_Production_Time_Ability` | `needs abilities` |  |
| `Planet_Income_Bonus_Ability` | `needs abilities` |  |
| `Black_Market_Ability` | `needs abilities` |  |
| `Proximity_Mines_Ability` | `needs abilities` |  |
| `Garrison_Upgrade_Ability` | `needs abilities` |  |
| `Grenade_Attack_Ability` | `needs abilities` |  |
| `Permanent_Weapon_Swap_Ability` | `needs abilities` |  |
| `Weatherproof_Ability` | `needs abilities` |  |
| `Bounty_On_Faction_Ability` | `needs abilities` |  |
| `Lucky_Shot_Attack_Ability` | `needs abilities` |  |
| `Hero_Protection_Ability` | `needs abilities` |  |
| `Galactic_Sabotage_Ability` | `needs abilities` |  |
| `Corruption_Ability` | `needs abilities` |  |
| `Personal_Flame_Thrower_Ability` | `needs abilities` |  |
| `Force_Confuse_Ability` | `needs abilities` |  |
| `Force_Whirlwind_Ability` | `needs abilities` |  |
| `Force_Telekinesis_Ability` | `needs abilities` |  |
| `Find_Weakness_Ability` | `needs abilities` |  |
| `Planet_Destruction_Ability` | `needs abilities` |  |
| `Super_Laser_Ability` | `needs abilities` |  |
| `Stun_Ability` | `needs abilities` |  |
| `Laser_Defense_Ability` | `needs abilities` |  |
| `Buzz_Droids_Ability` | `needs abilities` |  |
| `Hack_Ability` | `needs abilities` |  |
| `Repair_Ability` | `needs abilities` |  |
| `Eat_Attack_Ability` | `needs abilities` |  |
| `Cable_Ability` | `needs abilities` |  |
| `Force_Lightning_Ability` | `needs abilities` |  |
| `Political_Transition_Bonus_Ability` | `needs abilities` |  |
| `Political_Control_Protection_Ability` | `needs abilities` |  |
| `Retreat_Prevention_Ability` | `needs abilities` |  |
| `Vehicle_Thief_Ability` | `needs abilities` |  |
| `Slicer_Ability` | `needs abilities` |  |
| `Reduce_Technology_Price_Ability` | `needs abilities` |  |
| `Planet_Income_Gambling_Ability` | `needs abilities` |  |
| `Energy_Weapon_Attack_Ability` | `needs abilities` |  |
| `Maximum_Firepower_Attack_Ability` | `needs abilities` |  |
| `Concentrate_Fire_Attack_Ability` | `needs abilities` |  |
| `Shield_Flare_Ability` | `needs abilities` |  |
| `Saber_Throw_Ability` | `needs abilities` |  |
| `Force_Cloak_Ability` | `needs abilities` |  |
| `Berserker_Ability` | `needs abilities` |  |
| `Force_Sight_Ability` | `needs abilities` |  |
| `Hack_Super_Weapon_Ability` | `needs abilities` |  |
| `Infection_Ability` | `needs abilities` |  |
| `Radioactive_Contaminate_Ability` | `needs abilities` |  |
| `Corrupt_Systems_Ability` | `needs abilities` |  |
| `Summon_Ability` | `needs abilities` |  |
| `Tactical_Bribe_Ability` | `needs abilities` |  |
| `Blast_Ability` | `needs abilities` |  |
| `Spawn_Ability` | `needs abilities` |  |
| `Cluster_Bomb_Ability` | `needs abilities` |  |
| `Leech_Shields_Ability` | `needs abilities` |  |
| `Converted_To_Enemy_Die_Time_Seconds` | `needs abilities` |  |

## Companies, squadrons, teams, transports, garrisons, spawning, and special systems

5 done, 1 partial, 8 gap, 123 needs.

### Companies and teams

| Tag | Status | Note |
| --- | --- | --- |
| `Company_Units` | `done` |  |
| `Company_Transport_Unit` | `needs transports` |  |
| `Create_Team_Type` | `partial` | the team entity (squads) of land companies; squadrons ignore it |
| `Max_Squad_Size` | `done` | a team entity per that many members when the company names a `Create_Team_Type`; formation teams otherwise |
| `Create_Team` | `needs hero teams` |  |
| `ContainerArrangement` | `done` | Rows and Dartboard |
| `Is_Force_Sensitive` | `needs abilities` |  |
| `Is_Stealth_Company` | `needs stealth` |  |
| `Required_Orbiting_Units` | `needs galactic` |  |
| `Share_Damage_With_Teammates` | `needs hero teams` |  |
| `Redirect_Damage_To_Teammates` | `needs hero teams` |  |
| `Respawn_Whole_Team_When_Killed` | `needs reinforcement` |  |

### Space squadrons

| Tag | Status | Note |
| --- | --- | --- |
| `Squadron_Units` | `done` |  |
| `Squadron_Offsets` | `done` |  |
| `Squadron_Formation_Error_Tolerance` | `gap` |  |
| `Number_per_Squadron` | `needs galactic` |  |
| `Squadron_Capacity` | `needs unit spawning` | carrier capacity |
| `Is_Bomber` | `gap` |  |

### Transports and landing

| Tag | Status | Note |
| --- | --- | --- |
| `Transport_Capacity` | `needs transports` |  |
| `Landing_Transport_Variant` | `needs transports` |  |
| `Use_Transported_Object_Bounds_For_Landing_Z` | `needs transports` |  |
| `Exit_Door_Angle_Degrees` | `needs transports` |  |
| `Exit_Door_Distance` | `needs transports` |  |
| `Stay_In_Transport_During_Ground_Battle` | `needs transports` |  |
| `Display_Contained_Hero_Grab_Bars` | `needs transports` |  |
| `Can_Contain_Heroes_During_Ground_Battle` | `needs transports` |  |

### Garrisons

| Tag | Status | Note |
| --- | --- | --- |
| `Garrison_Category` | `needs garrisons` |  |
| `Garrison_Value` | `needs garrisons` |  |
| `Num_Garrison_Slots` | `needs garrisons` |  |
| `Num_Garrison_Guns` | `needs garrisons` |  |
| `Garrison_Bone_Names` | `needs garrisons` |  |
| `Garrison_Enter_Dist` | `needs garrisons` |  |
| `Garrison_Exit_Dist` | `needs garrisons` |  |
| `Garrison_Radius` | `needs garrisons` |  |
| `Spawn_Garrison_On_Load` | `needs garrisons` |  |

### Units produced by structures and carriers

| Tag | Status | Note |
| --- | --- | --- |
| `Starting_Spawned_Units_Tech_0` | `needs unit spawning` |  |
| `Reserve_Spawned_Units_Tech_0` | `needs unit spawning` |  |
| `Spawned_Squadron_Delay_Seconds` | `needs unit spawning` |  |
| `Spawned_Squadron_Location_Bones` | `needs unit spawning` |  |
| `Spawned_Squadron_Location_Flyout_Distances` | `needs unit spawning` |  |
| `Fighter_Bay_Flyout_Distance` | `needs unit spawning` |  |
| `Tactical_Respawn_Time_In_Secs` | `needs unit spawning` |  |
| `Spawn_Planet` | `needs unit spawning` |  |

### Indigenous spawners

| Tag | Status | Note |
| --- | --- | --- |
| `Spawned_Indigenous_Units_Type` | `needs unit spawning` |  |
| `Spawned_Indigenous_Pack_Type` | `needs unit spawning` |  |
| `Spawn_Indigenous_Units_In_Packs` | `needs unit spawning` |  |
| `Spawned_Indigenous_Units_Quantity` | `needs unit spawning` |  |
| `Spawned_Indigenous_Units_Delay_Seconds` | `needs unit spawning` |  |
| `Spawn_Indigenous_Units_Chance` | `needs unit spawning` |  |
| `Spawn_Indigenous_Units_Radius` | `needs unit spawning` |  |

### Deploying units

| Tag | Status | Note |
| --- | --- | --- |
| `Deploys` | `needs deploying` |  |
| `Auto_Deploys` | `needs deploying` |  |
| `Deployment_Anim_Rate` | `needs deploying` |  |
| `DeployedBehavior` | `needs deploying` |  |
| `Deployed_Walk_Speed` | `needs deploying` |  |
| `Deployed_Walk_Anim_Speed` | `needs deploying` |  |
| `Max_Dist_Walk_While_Deployed` | `needs deploying` |  |
| `Shielded_Only_When_Deployed` | `needs deploying` |  |
| `Must_Face_Target_To_Attack_When_Deployed` | `needs deploying` |  |
| `Deployed_Max_Attack_Distance_Multiplier` | `needs deploying` |  |
| `Deployed_Turret_Rotate_Extent_Degrees` | `needs deploying` |  |
| `Deployed_Turret_Elevate_Extent_Degrees` | `needs deploying` |  |
| `Turret_Deployed_Rest_Angle` | `needs deploying` |  |
| `Custom_Hard_YExtent_Deployed` | `needs deploying` |  |
| `Fire_Weapon_When_Deployed` | `needs deploying` |  |
| `Fire_Weapon_When_Undeployed` | `needs deploying` |  |
| `Fire_When_Deployed` | `needs deploying` |  |
| `Fire_When_Undeployed` | `needs deploying` |  |

### Holstering

| Tag | Status | Note |
| --- | --- | --- |
| `Holster_Weapon_Bone_Name` | `needs holstering` |  |
| `Holster_Drawn_Bone_Translation` | `needs holstering` |  |
| `Holster_Holstered_Bone_Translation` | `needs holstering` |  |
| `Holster_Transition_Time_In_Secs` | `needs holstering` |  |
| `Holster_Minimum_Drawn_Time_In_Secs` | `needs holstering` |  |
| `Holster_Disable_Engine_Loops` | `needs holstering` |  |

### Hero clash

| Tag | Status | Note |
| --- | --- | --- |
| `Clash_Type` | `needs hero clash` |  |
| `Clash_Range` | `needs hero clash` |  |
| `Involved_Hero_Types` | `needs hero clash` |  |
| `First_Hero_Type` | `needs hero clash` |  |
| `Second_Hero_Type` | `needs hero clash` |  |
| `First_Hero_Win_Exchange_Chance` | `needs hero clash` |  |
| `Second_Hero_Win_Exchange_Chance` | `needs hero clash` |  |
| `First_Hero_Damage_Multiplier` | `needs hero clash` |  |
| `Second_Hero_Damage_Multiplier` | `needs hero clash` |  |
| `First_Hero_Draw_Anim_Type` | `needs hero clash` |  |
| `First_Hero_Win_Anim_Type` | `needs hero clash` |  |
| `First_Hero_Lose_Anim_Type` | `needs hero clash` |  |
| `First_Hero_Conversation_Anim_Type` | `needs hero clash` |  |
| `Second_Hero_Draw_Anim_Type` | `needs hero clash` |  |
| `Second_Hero_Win_Anim_Type` | `needs hero clash` |  |
| `Second_Hero_Lose_Anim_Type` | `needs hero clash` |  |
| `Second_Hero_Conversation_Anim_Type` | `needs hero clash` |  |

### Presence and glory

| Tag | Status | Note |
| --- | --- | --- |
| `Presence_Range` | `needs abilities` |  |
| `Presence_Induced_Animations` | `needs abilities` |  |
| `Glory_Cinematics` | `needs abilities` |  |

### Stealth, conversion, theft, infection

| Tag | Status | Note |
| --- | --- | --- |
| `Stealth_Capable` | `needs stealth` |  |
| `Conversion_Ability_Changes_To_Enemy` | `needs abilities` |  |
| `Vehicle_Thief_Inside_Clone` | `needs abilities` |  |
| `Causes_Infection` | `needs abilities` |  |

### Field effects

| Tag | Status | Note |
| --- | --- | --- |
| `Gravity_Control_Field_Range` | `needs status effects` |  |
| `Gravity_Control_Field_Slow_Fraction` | `needs status effects` |  |
| `Gravity_Control_Field_Effect` | `needs status effects` |  |
| `Is_Affected_By_Gravity_Control_Field` | `needs status effects` |  |
| `Disable_Force_Abilities_Range` | `needs status effects` |  |
| `Passive_Missile_Shield_Radius` | `needs status effects` |  |
| `Bombing_Run_Prevention_Radius` | `needs status effects` |  |

### Dynamic transform

| Tag | Status | Note |
| --- | --- | --- |
| `Dynamic_Transform_Delay_Seconds` | `gap` |  |
| `Dynamic_Transform_Seconds` | `gap` |  |
| `Dynamic_Transform_Start_Scale` | `gap` |  |
| `Dynamic_Transform_End_Scale` | `gap` |  |
| `Dynamic_Transform_Start_Color` | `gap` |  |
| `Dynamic_Transform_End_Color` | `gap` |  |

### Super weapons and bombing runs

| Tag | Status | Note |
| --- | --- | --- |
| `Is_Special_Weapon_In_Space` | `needs super weapons` |  |
| `Special_Weapon_Index` | `needs super weapons` |  |
| `Special_Weapon_Target_Action_Index` | `needs super weapons` |  |
| `Special_Weapon_Valid_Targets` | `needs super weapons` |  |
| `Allows_Special_Weapon_Use` | `needs super weapons` |  |
| `Is_Skirmish_Tactical_Super_Weapon` | `needs super weapons` |  |
| `Is_Super_Weapon_Killer` | `needs super weapons` |  |
| `Lobbing_Superweapon_Chargeup_Frames` | `needs super weapons` |  |
| `Lobbing_Superweapon_Chargeup_Particle` | `needs super weapons` |  |
| `Lobbing_Superweapon_Chargeup_Particle_Bone_Name` | `needs super weapons` |  |
| `TSW_Start_Distance_From_Target` | `needs super weapons` |  |
| `TSW_Attack_Distance_From_Target` | `needs super weapons` |  |
| `TSW_Start_Pos_Match_Targets_Z` | `needs super weapons` |  |
| `TSW_Z_Adjust_Relative_To_Target_Pos` | `needs super weapons` |  |
| `TSW_Power_Up_Countdown_Seconds` | `needs super weapons` |  |
| `TSW_Attack_Anim_Duration_Seconds` | `needs super weapons` |  |
| `TSW_Explosion_Debris_Creation_Frame_Delay` | `needs super weapons` |  |
| `TSW_Post_Destruction_Wait_Frames` | `needs super weapons` |  |
| `Land_Bomber_Type` | `needs super weapons` |  |

### Beacons, abilities, and planet leftovers

| Tag | Status | Note |
| --- | --- | --- |
| `Beacon_Lifetime_In_Secs` | `needs abilities` |  |
| `Beacon_Radar_Map_Event_Name` | `needs abilities` |  |
| `Ability_Names` | `needs abilities` |  |
| `Special_Ability_Name` | `needs abilities` |  |
| `Hero_Ability` | `needs abilities` |  |
| `Force_Strength` | `needs abilities` |  |
| `The_Force` | `needs abilities` |  |

## Production, economy, tech, galactic play, AI, visibility, and identity

4 done, 2 partial, 5 gap, 225 needs, 4 n/a.

### Faction, identity, and scripts

| Tag | Status | Note |
| --- | --- | --- |
| `Affiliation` | `partial` | first faction only (preview colour, spawn filter) |
| `Faction` | `needs galactic` | black-market item faction (the code literal is a definition type) |
| `Faction_Anim_Subindex` | `needs player layer` | animation sub-index by owner (reinforcement pedestals) |
| `Political_Faction` | `needs galactic` |  |
| `Ship_Class` | `needs galactic` |  |
| `Moniker` | `needs galactic` |  |
| `Is_Named_Hero` | `needs UI` | hero flags for the interface |
| `Is_Generic_Hero` | `needs UI` | hero flags for the interface |
| `Can_Be_Only_One` | `needs galactic` |  |
| `Is_Homogeneous` | `needs galactic` |  |
| `Unique_Ground_Unit` | `needs galactic` |  |
| `Unique_Space_Unit` | `needs galactic` |  |
| `Loads_When_Faction_Present` | `needs player layer` | spawn filter |
| `Lua_Script` | `done` |  |
| `Execute_Script_On_Type` | `gap` | object scripts |
| `GalacticBehavior` | `needs galactic` |  |

### Galactic production, limits, and tech

| Tag | Status | Note |
| --- | --- | --- |
| `Build_Cost_Credits` | `needs production` |  |
| `Build_Time_Seconds` | `needs production` |  |
| `Build_Time_Reduced_By_Multiple_Factories` | `needs production` |  |
| `Build_Initially_Locked` | `needs production` |  |
| `Build_Can_Be_Unlocked_By_Slicer` | `needs production` |  |
| `Build_Limit_Current_For_All_Allies` | `needs production` |  |
| `Build_Limit_Current_Per_Player` | `needs production` |  |
| `Build_Limit_Lifetime_For_All_Allies` | `needs production` |  |
| `Build_Limit_Lifetime_Per_Player` | `needs production` |  |
| `Build_Max_Instances_Per_Planet` | `needs production` |  |
| `Build_Requires_Initial_Placement` | `needs production` |  |
| `Build_Advances_Tech_Level` | `needs production` |  |
| `Build_Countdown_Timer` | `needs production` |  |
| `Build_Countdown_Text_ID` | `needs production` |  |
| `Build_Countdown_Text_RGBA` | `needs production` |  |
| `Build_Tab_Heroes` | `needs production` |  |
| `Build_Tab_Land_Units` | `needs production` |  |
| `Build_Tab_Outpost` | `needs production` |  |
| `Build_Tab_Space_Station` | `needs production` |  |
| `Build_Tab_Space_Units` | `needs production` |  |
| `Build_Tab_Special_Structures` | `needs production` |  |
| `IsBuildable` | `needs production` |  |
| `Available_In_Skirmish` | `needs production` |  |
| `Tech_Level` | `needs production` |  |
| `Required_Ground_Base_Level` | `needs production` |  |
| `Required_Star_Base_Level` | `needs production` |  |
| `Required_Planets` | `needs production` |  |
| `Required_Special_Structures` | `needs production` |  |
| `Required_Tech_Structure` | `needs production` |  |
| `Required_Timeline` | `needs production` |  |
| `Maintenance_Cost` | `needs production` |  |
| `Score_Cost_Credits` | `needs production` |  |

### Tactical production, build pads, and skirmish

| Tag | Status | Note |
| --- | --- | --- |
| `Tactical_Build_Cost_Multiplayer` | `needs production` |  |
| `Tactical_Build_Cost_Campaign` | `needs production` |  |
| `Tactical_Build_Time_Seconds` | `needs production` |  |
| `Tactical_Build_Prerequisites` | `needs production` |  |
| `Tactical_Production_Queue` | `needs production` |  |
| `Tactical_Buildable_Objects_Multiplayer` | `needs production` |  |
| `Tactical_Buildable_Objects_Campaign` | `needs production` |  |
| `Tactical_Buildable_Constructed` | `needs production` |  |
| `Tactical_Build_Start_Lower_Z` | `needs production` |  |
| `Tactical_Build_Attachment_Bone_Name` | `needs production` |  |
| `Tactical_Sell_Credits` | `needs production` |  |
| `Tactical_Build_Increments_Tech_Level` | `needs production` |  |
| `Tactical_Destruction_Decrements_Tech_Level` | `needs production` |  |
| `Minimum_Time_Before_Pad_Can_Build_Again` | `needs production` |  |
| `Respects_Skirmish_Pre_Build_Bases_Option` | `needs production` |  |
| `Tactical_Additional_Structure_Type` | `needs production` |  |
| `Place_At_Every_Specific_Marker_Position` | `needs production` |  |
| `Place_Other_Type_At_Every_Specific_Marker_Position` | `needs production` |  |
| `Base_Position` | `needs production` |  |

### Upgrade chains

| Tag | Status | Note |
| --- | --- | --- |
| `Next_Upgrade_Level_Type` | `needs production` |  |
| `Previous_Upgrade_Level_Type` | `needs production` |  |
| `Destroy_Previous_Upgrade_Level` | `needs production` |  |
| `Show_In_Sidebar_While_Building` | `needs production` |  |

### Bases, levels, and slots

| Tag | Status | Note |
| --- | --- | --- |
| `Base_Level` | `needs production` |  |
| `Base_Level_Available` | `needs production` |  |
| `Next_Level_Base` | `needs production` |  |
| `Prev_Level_Base` | `needs production` |  |
| `Max_Space_Base` | `needs production` |  |
| `Max_Ground_Base` | `needs production` |  |
| `Special_Structures` | `needs production` |  |
| `Special_Structures_Land` | `needs production` |  |
| `Special_Structures_Space` | `needs production` |  |
| `Requires_Base_Power` | `needs base power` |  |

### Population

| Tag | Status | Note |
| --- | --- | --- |
| `Population_Value` | `needs production` |  |
| `Additional_Population_Capacity` | `needs production` |  |
| `Land_Override_Population_Value` | `needs production` |  |
| `Space_Override_Population_Value` | `needs production` |  |

### Planets

| Tag | Status | Note |
| --- | --- | --- |
| `Galactic_Position` | `needs galactic` |  |
| `Galactic_Model_Name` | `partial` | resolved, not shown (no galactic map) |
| `Destroyed_Galactic_Model_Name` | `needs galactic` |  |
| `Always_Instantiate_Galactic` | `needs galactic` |  |
| `Terrain` | `needs galactic` | planet climate (the code literal is a shader name) |
| `Land_Tactical_Map` | `needs galactic` |  |
| `Space_Tactical_Map` | `needs galactic` |  |
| `Destroyed_Space_Tactical_Map` | `needs galactic` |  |
| `Planet_Surface_Accessible` | `needs galactic` |  |
| `Planet_Credit_Value` | `needs galactic` |  |
| `Planet_Destroyed_Credit_Value` | `needs galactic` |  |
| `Planet_Capture_Bonus_Reward` | `needs galactic` |  |
| `Planet_Tech_Availability_Rating` | `needs galactic` |  |
| `Planet_Ability_Name` | `needs galactic` |  |
| `Planet_Ability_Description` | `needs galactic` |  |
| `Planet_Ability_Icon` | `needs galactic` |  |
| `Describe_Population` | `needs galactic` |  |
| `Potential_Indigenous_Power` | `needs galactic` |  |
| `Political_Control` | `needs galactic` |  |
| `Planet_Retains_Residual_Influence` | `needs galactic` |  |
| `Planet_Groundbase_Stealth` | `needs galactic` |  |
| `Planet_Starbase_Stealth` | `needs galactic` |  |
| `Planet_Is_Destroyable_By_TSW` | `needs galactic` |  |
| `Planet_Is_Mining_Colony` | `needs galactic` |  |
| `Planet_Restricted_Unit_Types` | `needs galactic` |  |
| `Planet_Restricted_Unit_Categories` | `needs galactic` |  |
| `Planet_Black_Market_Influences` | `needs galactic` |  |
| `Planet_Black_Market_Modifier` | `needs galactic` |  |
| `Planet_Slice_Affinity_Types` | `needs galactic` |  |
| `Planet_Slice_Difficulty_Rating` | `needs galactic` |  |
| `Planet_Corruption_Removal_Base_Cost` | `needs galactic` |  |
| `Galactic_Influence_Range` | `needs galactic` |  |

### Galactic fleets and heroes

| Tag | Status | Note |
| --- | --- | --- |
| `Always_Spawn_In_Orbit` | `needs galactic` |  |
| `Attach_To_Flagship_During_Space_Battle` | `needs galactic` |  |
| `Can_Be_Neutralized_By_Major_Heroes` | `needs galactic` |  |
| `Can_Be_Neutralized_By_Minor_Heroes` | `needs galactic` |  |
| `Neutralization_Cost` | `needs galactic` |  |
| `Can_Participate_In_Space_Battle` | `needs galactic` |  |
| `Galactic_Fleet_Override_Icon_Index` | `needs galactic` |  |
| `Galactic_Fleet_Override_Model_Name` | `needs galactic` |  |
| `Is_Interdictor` | `needs galactic` |  |
| `Prevents_Blockade_Run_Attrition` | `needs galactic` |  |
| `Use_Special_Submit_Rules` | `needs galactic` |  |
| `Prevents_Story_Campaign_Autoresolve` | `needs galactic` |  |

### FoC corruption missions

| Tag | Status | Note |
| --- | --- | --- |
| `Corruption_1_Type` | `needs galactic` |  |
| `Corruption_1_Credit_Cost` | `needs galactic` |  |
| `Corruption_1_Mission_Briefing` | `needs galactic` |  |
| `Corruption_1_Required_Mission_Type` | `needs galactic` |  |
| `Corruption_1_Required_Mission_Count` | `needs galactic` |  |
| `Corruption_1_Required_Unit_Type` | `needs galactic` |  |
| `Corruption_1_Required_Unit_Count` | `needs galactic` |  |
| `Corruption_1_Success_Bink_Hologram_Name` | `needs galactic` |  |
| `Corruption_1_Failure_Bink_Hologram_Name` | `needs galactic` |  |
| `Corruption_1_Success_Credit_Bonus` | `needs galactic` |  |
| `Corruption_1_Success_Unlock_Unit_Type` | `needs galactic` |  |
| `Corruption_1_Tactical_Game_Mode` | `needs galactic` |  |
| `Corruption_1_Tactical_Map_Name` | `needs galactic` |  |
| `Corruption_1_Tactical_Script_Name` | `needs galactic` |  |
| `Corruption_2_Type` | `needs galactic` |  |
| `Corruption_2_Credit_Cost` | `needs galactic` |  |
| `Corruption_2_Mission_Briefing` | `needs galactic` |  |
| `Corruption_2_Required_Mission_Type` | `needs galactic` |  |
| `Corruption_2_Required_Mission_Count` | `needs galactic` |  |
| `Corruption_2_Required_Unit_Type` | `needs galactic` |  |
| `Corruption_2_Required_Unit_Count` | `needs galactic` |  |
| `Corruption_2_Success_Bink_Hologram_Name` | `needs galactic` |  |
| `Corruption_2_Failure_Bink_Hologram_Name` | `needs galactic` |  |
| `Corruption_2_Success_Unlock_Unit_Type` | `needs galactic` |  |
| `Corruption_3_Type` | `needs galactic` |  |
| `Corruption_3_Credit_Cost` | `needs galactic` |  |
| `Corruption_3_Mission_Briefing` | `needs galactic` |  |
| `Corruption_3_Required_Mission_Type` | `needs galactic` |  |
| `Corruption_3_Required_Mission_Count` | `needs galactic` |  |
| `Corruption_3_Required_Unit_Type` | `needs galactic` |  |
| `Corruption_3_Required_Unit_Count` | `needs galactic` |  |
| `Corruption_3_Success_Bink_Hologram_Name` | `needs galactic` |  |
| `Corruption_3_Failure_Bink_Hologram_Name` | `needs galactic` |  |
| `Is_Indigenous_Unit_Corruptible` | `needs galactic` |  |

### Bribery, slicing, sabotage, piracy, black market

| Tag | Status | Note |
| --- | --- | --- |
| `Cheap_Bribe_Cost` | `needs galactic` |  |
| `Expensive_Bribe_Cost` | `needs galactic` |  |
| `Tactical_Bribe_Cost` | `needs galactic` |  |
| `Bribe_Modifier_On_Corrupted_Planets` | `needs galactic` |  |
| `Bribe_Modifier_On_Uncorrupted_Planets` | `needs galactic` |  |
| `Slice_Cost_Credits` | `needs galactic` |  |
| `Sabotage_Cost_Credits` | `needs galactic` |  |
| `Tutorial_Sabotage_Cost_Credits` | `needs galactic` |  |
| `Piracy_Value_Credits` | `needs galactic` |  |
| `Base_Cost_Credits` | `needs galactic` |  |
| `Tutorial_Base_Cost_Credits` | `needs galactic` |  |
| `Tutorial_Tech_Level` | `needs galactic` |  |
| `Uncommon_Price_Multiplier` | `needs galactic` |  |

### Capture points, cash points, reinforcement, and victory

| Tag | Status | Note |
| --- | --- | --- |
| `Capture_Point_Radius` | `needs battle rules` |  |
| `Capture_Point_Transition_Time_Seconds` | `needs battle rules` |  |
| `Influences_Capture_Point` | `needs battle rules` |  |
| `Cash_Point_Credits_Per_Second` | `needs battle rules` |  |
| `Cash_Point_Radius` | `needs battle rules` |  |
| `Cash_Point_Transition_Time_Seconds` | `needs battle rules` |  |
| `Influences_Cash_Point` | `needs battle rules` |  |
| `Is_Community_Property` | `needs battle rules` |  |
| `Ownership_Sticks` | `needs battle rules` |  |
| `Control_Point_Domination_Condition_Relevant` | `needs battle rules` |  |
| `Reinforcement_Enabling_Radius` | `needs battle rules` |  |
| `Reinforcement_Prevention_Radius` | `needs battle rules` |  |
| `Reinforcement_Region_Blob_Name` | `needs battle rules` |  |
| `Should_Reinforcements_Move` | `needs battle rules` |  |
| `Victory_Relevant` | `needs battle rules` |  |
| `Land_Victory_Relevant` | `needs battle rules` |  |
| `Space_Victory_Relevant` | `needs battle rules` |  |
| `HQ_Win_Condition_Relevant` | `needs battle rules` |  |
| `Map_Load_Spawn_Table` | `needs battle rules` |  |

### AI and autoresolve

| Tag | Status | Note |
| --- | --- | --- |
| `AI_Combat_Power` | `done` |  |
| `Combat_Power_Value` | `needs AI` |  |
| `Has_Land_Evaluator` | `needs AI` |  |
| `Has_Space_Evaluator` | `needs AI` |  |
| `SpaceAutoResolveHitRate` | `needs AI` |  |
| `SpaceAutoResolveStunRate` | `needs AI` |  |
| `Size_Value` | `needs AI` |  |
| `Can_Indigenous_Unit_Stop` | `needs AI` |  |
| `CanCellStack` | `gap` | pathfinding cell sharing |

### Fog of war and reveal

| Tag | Status | Note |
| --- | --- | --- |
| `Land_FOW_Reveal_Range` | `needs fog of war` |  |
| `Space_FOW_Reveal_Range` | `needs fog of war` |  |
| `Dense_FOW_Reveal_Range_Multiplier` | `needs fog of war` |  |
| `Initial_State_Visible_Under_FOW` | `needs fog of war` |  |
| `Last_State_Visible_Under_FOW` | `needs fog of war` |  |
| `Multisample_FOW_Check` | `needs fog of war` |  |
| `Visible_To_Enemies_When_Empty` | `needs fog of war` |  |
| `Model_Visible_To_Enemy` | `needs fog of war` |  |
| `Reveal_During_Setup_Phase` | `needs fog of war` |  |
| `Reveal_During_Setup_Phase_Only` | `needs fog of war` |  |
| `Reveal_For_Attacker` | `needs fog of war` |  |
| `Reveal_For_Defender` | `needs fog of war` |  |
| `Modifies_Reveal_Range` | `needs fog of war` |  |
| `Reveal_Range_Modifier` | `needs fog of war` |  |
| `Sensor_Range` | `needs fog of war` |  |

### Markers, dummies, decorations, and editor flags

| Tag | Status | Note |
| --- | --- | --- |
| `Is_Marker` | `done` | not drawn; stays an entity for scripts |
| `Is_Dummy` | `gap` |  |
| `Is_Decoration` | `done` | drawn and animated, never simulated; outside the state hash; still part of the map extent |
| `Is_Discardable` | `n/a` | never true in stock |
| `Is_Branched_Map_Discardable` | `n/a` | never true in stock |
| `Is_Editor_Placed` | `n/a` | editor flag |
| `Marker_For_Specific_Object_Type` | `gap` |  |
| `Is_Asteroid_Field` | `needs space hazards` |  |
| `Is_Impassable_Asteroid` | `needs space hazards` |  |
| `Is_Ion_Storm` | `needs space hazards` |  |
| `Is_Nebula` | `needs space hazards` |  |
| `Permanent` | `gap` |  |
| `Tactical_Model_Name` | `n/a` | probably ignored legacy model field (2 uses) |

### Tactical overview camera

| Tag | Status | Note |
| --- | --- | --- |
| `Tactical_Overview_Clicks` | `needs tactical overview` |  |
| `Tactical_Overview_Click_Time` | `needs tactical overview` |  |
| `Tactical_Overview_Distance` | `needs tactical overview` |  |
| `Tactical_Overview_Distance2` | `needs tactical overview` |  |
| `Tactical_Overview_FOV` | `needs tactical overview` |  |
| `Tactical_Overview_FOV2` | `needs tactical overview` |  |
| `Tactical_Overview_Pitch` | `needs tactical overview` |  |
| `Tactical_Overview_Pitch2` | `needs tactical overview` |  |

## Models, rendering, animation, user interface, and text

26 done, 7 partial, 74 gap, 83 needs, 3 n/a.

### Core structure (summary; see the overview chapters)

| Tag | Status | Note |
| --- | --- | --- |
| `Variant_Of_Existing_Type` | `done` |  |
| `Behavior` | `partial` | read for flight and body aiming; other behaviors unused |
| `LandBehavior` | `partial` | read for flight and body aiming; other behaviors unused |
| `SpaceBehavior` | `partial` | read for flight and body aiming; other behaviors unused |
| `TacticalBehavior` | `gap` |  |
| `CategoryMask` | `done` |  |
| `Property_Flags` | `done` |  |
| `Type` | `done` |  |
| `Weather_Category` | `needs weather` |  |

### Models and model placement

| Tag | Status | Note |
| --- | --- | --- |
| `Land_Model_Name` | `done` |  |
| `Space_Model_Name` | `done` |  |
| `xxxSpace_Model_Name` | `n/a` | disabled by its prefix |
| `Model_Name` | `n/a` | stray spelling, used once |
| `Land_Terrain_Model_Mapping` | `gap` |  |
| `Scale_Factor` | `done` |  |
| `Scale` | `gap` | decal size (the code literal is unrelated) |
| `LOD_Bias` | `done` |  |
| `Facing_Adjust` | `gap` |  |
| `Camera_Aligned` | `gap` |  |
| `Pre_Lit` | `gap` |  |
| `No_Colorization_Color` | `gap` |  |
| `Overall_Length` | `gap` |  |
| `Overall_Width` | `gap` |  |
| `Is_Sprite` | `gap` |  |
| `Movie_Object` | `needs cinematics` |  |
| `Cinematic_Object_Only` | `needs cinematics` |  |

### Detail levels, fading, and culling

| Tag | Status | Note |
| --- | --- | --- |
| `No_Reflection_Below_Detail_Level` | `gap` |  |
| `No_Refraction_Below_Detail_Level` | `gap` |  |
| `No_Particle_Below_Detail_Level` | `gap` |  |
| `Blob_Shadow_Below_Detail_Level` | `gap` |  |
| `Exclude_From_Distance_Fade` | `gap` |  |
| `Allow_Particle_Model_Culling` | `gap` |  |
| `Occlusion_Silhouette_Enabled` | `done` | team-colored silhouette where hidden; the global GameConstants switch is a Graphics setting |

### Shadows, selection blobs, and ground marks

| Tag | Status | Note |
| --- | --- | --- |
| `Blob_Shadow_Material_Name` | `gap` |  |
| `Blob_Shadow_Scale` | `gap` |  |
| `Blob_Shadow_Bone_Name` | `gap` |  |
| `Selection_Blob_Material_Name` | `needs UI` | selection marks |
| `Highlight_Blob_Material_Name` | `needs UI` | selection marks |
| `SurfaceFX_Name` | `partial` | dynamic tracks (trails) and walkers' footprints; sounds, particles, and hover effects not yet |
| `Terrain_Texture_Modifier_Material` | `gap` |  |
| `Terrain_Texture_Modifier_Square` | `gap` |  |
| `Terrain_Texture_Modifier_Join_Distance` | `gap` |  |
| `Hides_When_Built_On` | `needs production` |  |

### Damage visuals and hardpoint art

| Tag | Status | Note |
| --- | --- | --- |
| `Land_Damage_Thresholds` | `done` | on land; the state of the last threshold the health fraction is at or below |
| `Land_Damage_Alternates` | `done` | meshes and `_ALT<n>` proxy effects of the state; wrecks show the state for no health |
| `Asteroid_Damage_Hit_Particles` | `needs space hazards` |  |
| `Model_To_Attach` | `done` |  |
| `Attachment_Bone` | `done` |  |
| `Engine_Particles` | `done` |  |
| `Engine_Hardpoint` | `needs cinematics` |  |
| `Tooltip_Text` | `needs UI` |  |
| `Blast_Ability_Fire_Projectile_Type` | `needs abilities` |  |
| `Internal_Damage_Type` | `gap` |  |

### Animation

| Tag | Status | Note |
| --- | --- | --- |
| `Loop_Idle_Anim_00` | `done` | Yes loops IDLE_00; otherwise a random IDLE variant follows each that ends, and a lone one replays after 2-4 s (owner's recollection and choice, to observe) |
| `Idle_Anim_00_Rate_Mod` | `done` | the last occurrence wins on the two repeating asteroid masses |
| `Allow_Idle_When_Moving` | `done` |  |
| `Land_Model_Anim_Override_Name` | `done` |  |
| `Specific_Death_Anim_Index` | `done` |  |
| `Specific_Death_Anim_Type` | `partial` | Crushed (and DIE) for crushing; the Force and fire types wait for abilities |
| `Should_Death_Clone_Play_Idle` | `gap` |  |
| `Cinematic_Anim_Index` | `needs cinematics` |  |
| `Cinematic_Anim_Blend_Seconds` | `needs cinematics` |  |
| `Pause_During_Cinematic_Anim` | `needs cinematics` |  |
| `Particle_Lifetime_Frames` | `gap` |  |

### Layering of skydomes and backdrops

| Tag | Status | Note |
| --- | --- | --- |
| `Layer_Z_Adjust` | `partial` | height of units spawned in space (recorded); placements and render layers not yet |
| `In_Background` | `partial` | backdrops receive no shadows (owner, 2026-11-13); the background drawing layer is not modeled |
| `In_Midground` | `gap` |  |
| `In_Midforeground` | `gap` |  |
| `Sort_Order_Adjust` | `gap` |  |

### Selection, brackets, and interface models

| Tag | Status | Note |
| --- | --- | --- |
| `Select_Box_Scale` | `needs UI` |  |
| `Select_Box_Z_Adjust` | `needs UI` |  |
| `GUI_Bracket_Size` | `needs UI` |  |
| `GUI_Bracket_Width` | `needs UI` |  |
| `GUI_Bracket_Height` | `needs UI` |  |
| `GUI_Bounds_Scale` | `needs UI` |  |
| `GUI_Bias` | `needs UI` |  |
| `Mouse_Collide_Override_Sphere_Radius` | `needs UI` |  |
| `Not_Really_Selectable` | `needs UI` |  |
| `User_Bound_Min` | `needs UI` |  |
| `User_Bound_Max` | `needs UI` |  |
| `GUI_Model_Name` | `needs UI` |  |
| `GUI_Model` | `needs UI` |  |
| `GUI_Distance` | `needs UI` |  |
| `GUI_Offset` | `needs UI` |  |
| `GUI_Velocity` | `needs UI` |  |
| `GUI_Angles` | `needs UI` |  |
| `GUI_X_Rot` | `needs UI` |  |
| `GUI_Row` | `needs UI` |  |
| `Icon_Name` | `needs UI` |  |
| `Native_Icon_Name` | `needs UI` |  |
| `Show_Hero_Head` | `needs UI` |  |
| `Show_In_Sidebar_When_Complete` | `needs UI` |  |
| `Show_In_Sidebar_Tray` | `needs UI` |  |
| `Show_Name` | `needs UI` |  |
| `Name_Adjust` | `needs UI` |  |
| `Ranking_In_Category` | `needs UI` |  |
| `Include_In_UI_Map_Header` | `needs UI` |  |

### Radar

| Tag | Status | Note |
| --- | --- | --- |
| `Is_Visible_On_Radar` | `needs radar` |  |
| `Is_Visible_On_Enemy_Radar` | `needs radar` |  |
| `Visible_On_Radar_When_Fogged` | `needs radar` |  |
| `Radar_Icon_Name` | `needs radar` |  |
| `Radar_Icon_Size` | `needs radar` |  |
| `Radar_Icon_Scale_Land` | `needs radar` |  |
| `Radar_Icon_Scale_Space` | `needs radar` |  |
| `Radar_Range_Icon_Name` | `needs radar` |  |
| `Radar_Rotate_Icon` | `needs radar` |  |
| `Radar_Show_Facing` | `needs radar` |  |
| `Radar_Clip_To_Visible_Region` | `needs radar` |  |
| `Radar_Register_As_Foreground_Object` | `needs radar` |  |
| `Begin_Control_Transition_Radar_Event` | `needs radar` |  |
| `End_Control_Transition_Radar_Event` | `needs radar` |  |

### Text and encyclopedia

| Tag | Status | Note |
| --- | --- | --- |
| `Text_ID` | `needs UI` |  |
| `Encyclopedia_Text` | `needs UI` |  |
| `MP_Encyclopedia_Text` | `needs UI` |  |
| `Encyclopedia_Unit_Class` | `needs UI` |  |
| `Encyclopedia_Good_Against` | `needs UI` |  |
| `Encyclopedia_Vulnerable_To` | `needs UI` |  |
| `Localized_Name` | `needs UI` |  |
| `Localized_Description` | `needs UI` |  |
| `Used_By_Units` | `needs UI` |  |
| `Friendly_Spawn_Text` | `needs UI` |  |
| `Enemy_Spawn_Text` | `needs UI` |  |

### Planets (galactic map)

| Tag | Status | Note |
| --- | --- | --- |
| `Describe_Advantage` | `needs galactic` |  |
| `Describe_History` | `needs galactic` |  |
| `Describe_Tactical` | `needs galactic` |  |
| `Describe_Terrain` | `needs galactic` |  |
| `Describe_Weather` | `needs galactic` |  |
| `Describe_Wildlife` | `needs galactic` |  |
| `Encyclopedia_Weather_Icon` | `needs galactic` |  |
| `Encyclopedia_Weather_Name` | `needs galactic` |  |
| `Encyclopedia_Weather_Info` | `needs galactic` |  |
| `Zoomed_Terrain_Index` | `needs galactic` |  |

### Tactical cameras

Postponed by the owner (2026-11-13): battles use the engine's own cameras for now; these rows stay
`gap` until that changes.

| Tag | Status | Note |
| --- | --- | --- |
| `Distance_Default` | `gap` |  |
| `Distance_Min` | `gap` |  |
| `Distance_Max` | `gap` |  |
| `Distance_Per_Mouse_Unit` | `gap` |  |
| `Distance_Smooth_Time` | `gap` |  |
| `Distance_Spline` | `gap` |  |
| `Spline_Steps` | `gap` |  |
| `Fov_Default` | `gap` |  |
| `Fov_Min` | `gap` |  |
| `Fov_Max` | `gap` |  |
| `Fov_Per_Mouse_Unit` | `gap` |  |
| `Fov_Smooth_Time` | `gap` |  |
| `Pitch_Default` | `gap` |  |
| `Pitch_Min` | `gap` |  |
| `Pitch_Max` | `gap` |  |
| `Pitch_Per_Mouse_Unit` | `gap` |  |
| `Pitch_Per_Zoom_Unit` | `gap` |  |
| `Pitch_Spline` | `gap` |  |
| `Pitch_When_Zoomed_In` | `gap` |  |
| `Pitch_Zoom_Begin_Fraction` | `gap` |  |
| `Yaw_Default` | `gap` |  |
| `Yaw_Min` | `gap` |  |
| `Yaw_Max` | `gap` |  |
| `Yaw_Per_Mouse_Unit` | `gap` |  |
| `Near_Clip` | `gap` |  |
| `Far_Clip` | `gap` |  |
| `Object_Fade_Begin` | `gap` |  |
| `Object_Fade_End` | `gap` |  |
| `Location_Height_Smooth_Time` | `gap` |  |
| `Location_Height_Up_Smooth_Time` | `gap` |  |
| `Location_Height_Down_Smooth_Time` | `gap` |  |
| `Top_Bounds_Buffer` | `gap` |  |
| `Bottom_Bounds_Buffer` | `gap` |  |
| `Side_Bounds_Buffer` | `gap` |  |

### Lights, lens flares, and terrain decals

| Tag | Status | Note |
| --- | --- | --- |
| `Diffuse` | `gap` | light colour (the code literal is a material parameter) |
| `Intensity` | `gap` |  |
| `Radius` | `gap` |  |
| `Falloff_Start` | `gap` |  |
| `Blob_Radius` | `gap` |  |
| `Blob_Color` | `gap` |  |
| `Blob_Intensity` | `gap` |  |
| `Flare` | `gap` |  |
| `Category` | `gap` |  |
| `Render_Mode` | `done` | diffuse, bump, and additive, in the terrain shader |
| `UV_Slot` | `done` | a random cell of those listed |
| `Slot_Scale` | `done` |  |
| `Z_Angle` | `done` |  |
| `Random_Rotate` | `done` |  |
| `Random_Size_Offset` | `done` |  |
| `Fadeout_Time` | `done` | then a 2 s fade (an engine choice) |

### Wind disturbance

| Tag | Status | Note |
| --- | --- | --- |
| `Wind_Disturbance_Radius` | `gap` |  |
| `Wind_Disturbance_Strength` | `gap` |  |
| `Wind_Disturbance_Sphere_Alpha` | `gap` |  |

### Other tags assigned to this family

| Tag | Status | Note |
| --- | --- | --- |
| `Ambient` | `n/a` | stub test data (developer comment) |
| `Play_Conversation_Events` | `needs hero clash` |  |
| `Destroy_When_Child_Dies` | `needs production` |  |
| `Destroy_When_Used_As_Corruption_Prereq` | `needs production` |  |
| `Tactically_Built_Child_Object_Persists` | `needs production` |  |
| `Override_Bounty_MP` | `needs galactic` |  |
| `Override_Bounty_SP` | `needs galactic` |  |
| `Surface_Bombardment_Capable` | `needs galactic` |  |

## Sound events and speech

26 done, 1 partial, 2 gap, 58 needs, 4 n/a.

### Unit responses to orders and selection

| Tag | Status | Note |
| --- | --- | --- |
| `SFXEvent_Select` | `done` |  |
| `SFXEvent_Move` | `done` |  |
| `SFXEvent_Attack` | `done` |  |
| `SFXEvent_Guard` | `done` |  |
| `SFXEvent_Stop` | `gap` | the sandbox has no stop order yet |
| `SFXEvent_Move_Opposite` | `gap` |  |
| `SFXEvent_Fleet_Move` | `needs galactic` |  |
| `SFXEvent_Group_Move` | `done` | a group member with the line speaks it |
| `SFXEvent_Group_Attack` | `done` |  |
| `SFXEvent_Assist_Move` | `done` | another type's answer after the response (owner's account) |
| `SFXEvent_Assist_Attack` | `done` |  |
| `SFXEvent_Attack_With_Non_Hero_Ability` | `needs abilities` |  |
| `SFXEvent_Barrage` | `needs abilities` |  |
| `SFXEvent_Attacked` | `needs player layer` |  |
| `SFXEvent_Announce` | `needs player layer` |  |
| `SFXEvent_Deploy` | `needs deploying` |  |
| `SFXEvent_Draw_Weapon` | `needs holstering` |  |
| `SFXEvent_Holster_Weapon` | `needs holstering` |  |
| `SFXEvent_Hero_Respawned` | `needs reinforcement` |  |
| `Play_SFXEvent_On_Sighting` | `needs fog of war` |  |
| `SFXEvent_Cough_Override` | `needs status effects` | gas or poison cough |
| `SFXEvent_Move_Into_Nebula` | `needs space hazards` |  |
| `SFXEvent_Move_Into_Asteroid_Field` | `needs space hazards` |  |
| `SFXEvent_Damaged_By_Asteroid` | `needs space hazards` |  |

### Combat and weapon sounds

| Tag | Status | Note |
| --- | --- | --- |
| `SFXEvent_Fire` | `done` |  |
| `Fire_SFXEvent` | `done` |  |
| `Projectile_SFXEvent_Detonate` | `done` |  |
| `Projectile_SFXEvent_Detonate_Reduced_By_Armor` | `done` |  |
| `Lobbing_Superweapon_SFXEvent_Chargeup` | `needs super weapons` |  |
| `Lobbing_Superweapon_SFXEvent_Fire` | `needs super weapons` |  |

### Engine and ambient loops

| Tag | Status | Note |
| --- | --- | --- |
| `SFXEvent_Engine_Moving_Loop` | `partial` | jetpack types (secondary locomotor) stay silent until it exists |
| `SFXEvent_Engine_Idle_Loop` | `done` |  |
| `SFXEvent_Engine_Cinematic_Focus_Loop` | `needs cinematics` |  |
| `SFXEvent_Ambient_Loop` | `done` |  |
| `XSFXEvent_Ambient_Loop` | `n/a` | disabled by its prefix |
| `SFXEvent_Ambient_Moving` | `done` | needs the AMBIENT_SFX behavior |
| `SFXEvent_Ambient_Moving_Min_Delay_Seconds` | `done` |  |
| `SFXEvent_Ambient_Moving_Max_Delay_Seconds` | `done` |  |
| `SFXEvent_Turret_Rotating_Loop` | `done` | while the turret's direction changes between ticks (unit turrets; hardpoint turrets do not turn yet) |
| `SFXEvent_Powered_Active_Loop` | `needs base power` |  |
| `SFXEvent_Debug_Test_Loop` | `n/a` | debug sound |

### Death and damage sounds

| Tag | Status | Note |
| --- | --- | --- |
| `Death_SFXEvent_Start_Die` | `done` |  |
| `Death_SFXEvent_End_Die` | `done` | played with the wreck's Death_Explosions_End (an inference: every stock user has both) |
| `Spin_Away_On_Death_SFXEvent_Start_Die` | `done` |  |
| `Selt_Destruct_SFXEvent_Start_Die` | `needs retreat` |  |
| `Land_Damage_SFX` | `done` | on entering a state on the way down, on land |

### Health and base warnings

| Tag | Status | Note |
| --- | --- | --- |
| `SFXEvent_Health_Low_Warning` | `done` | at a third of health; every unit until the player layer |
| `SFXEvent_Health_Critical_Warning` | `done` | at a tenth |
| `SFXEvent_Enemy_Health_Low_Warning` | `needs player layer` |  |
| `SFXEvent_Enemy_Health_Critical_Warning` | `needs player layer` |  |
| `SFXEvent_Enemy_Damaged_Health_Low_Warning` | `needs player layer` |  |
| `SFXEvent_Enemy_Damaged_Health_Critical_Warning` | `needs player layer` |  |
| `SFXEvent_Unit_Under_Attack` | `needs player layer` | advisor line to the owner |
| `SFXEvent_Unit_Lost` | `needs player layer` | advisor line to the owner |

### Build and production announcements

| Tag | Status | Note |
| --- | --- | --- |
| `SFXEvent_Build_Started` | `needs production` |  |
| `SFXEvent_Build_Complete` | `needs production` |  |
| `SFXEvent_Build_Cancelled` | `needs production` |  |
| `SFXEvent_Tactical_Build_Started` | `needs production` |  |
| `SFXEvent_Tactical_Build_Complete` | `needs production` |  |
| `SFXEvent_Tactical_Build_Cancelled` | `needs production` |  |
| `SFXEvent_Sold_Tactical` | `needs production` |  |
| `SFXEvent_Special_Weapon_Ready` | `needs super weapons` |  |
| `SFXEvent_Beacon_Placed` | `needs UI` |  |

### Hardpoint sounds

| Tag | Status | Note |
| --- | --- | --- |
| `SFXEvent_Attack_Hardpoint` | `needs UI` | ordering an attack on a hardpoint |
| `SFXEvent_Hardpoint_Destroyed` | `done` | by hardpoint Type; every owner until the player layer |
| `SFXEvent_Hardpoint_All_Weapons_Destroyed` | `n/a` | empty in both stock uses |
| `SFXEvent_Hardpoint` | `n/a` | events undefined; probably dead |
| `Death_Explosion_SFXEvent` | `done` |  |

### Special weapons and bombing runs

| Tag | Status | Note |
| --- | --- | --- |
| `SFXEvent_Bomb_Run_Select_Target` | `needs super weapons` |  |
| `SFXEvent_Bomb_Run_Incoming` | `needs super weapons` |  |
| `SFXEvent_Bombard_Select_Target` | `needs super weapons` |  |
| `SFXEvent_Bombard_Incoming` | `needs super weapons` |  |
| `TSW_SFXEvent_Activate_Voice` | `needs super weapons` |  |
| `TSW_SFXEvent_Weapon_Power_Up` | `needs super weapons` |  |
| `TSW_SFXEvent_GUI_Bttton_Press` | `needs super weapons` |  |
| `TSW_MusicEvent_Activation_Start` | `needs super weapons` |  |
| `TSW_Post_Music_Wait_Frames` | `needs super weapons` |  |

### Speech and music

| Tag | Status | Note |
| --- | --- | --- |
| `Build_Speech_Underway` | `needs production` |  |
| `Build_Speech_Completed` | `needs production` |  |
| `Build_Speech_Stopped` | `needs production` |  |
| `Build_Speech_Countdowns` | `needs production` |  |
| `Build_Music_Completed` | `needs production` |  |
| `Corruption_1_Mission_Commencing_Speech_Event` | `needs production` |  |
| `Corruption_1_Success_Speech_Event` | `needs production` |  |
| `Corruption_1_Failure_Speech_Event` | `needs production` |  |
| `Corruption_2_Success_Speech_Event` | `needs production` |  |
| `Corruption_2_Failure_Speech_Event` | `needs production` |  |
| `Corruption_3_Success_Speech_Event` | `needs production` |  |
| `Corruption_3_Failure_Speech_Event` | `needs production` |  |
| `First_Hero_Win_Speech` | `needs hero clash` |  |
| `Second_Hero_Win_Speech` | `needs hero clash` |  |
