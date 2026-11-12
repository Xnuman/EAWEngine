# Game-object XML tags

What the tags of unit, structure, and other game-object definitions in *Empire at War* (EAW) and
*Forces of Corruption* (FoC) mean, how they are used, and which ones EAWEngine reads. This is a
research reference for engine work and modding, not a schema: the meanings are evidence-graded,
and the counts describe the stock data, not limits a mod must respect.

## Sources and method

Three corpora were surveyed with a tolerant XML tokenizer (the game's XML has multi-root
fragments, stray `<` characters, and unbalanced tags in a few AI files) and compared tag by tag:

| Corpus | XML files | Game-object definitions | Role |
| --- | --- | --- | --- |
| EAW source XML | 366 | 2,098 | the `Source` folder the official map editor installs (EAW) |
| FoC source XML | 609 | 3,607 | the same for FoC |
| Installed FoC data | 638 | 3,617 (plus the patch's `CIN_*` kinds) | the owner's patched installation, read through the VFS |

The two source trees come from a public, unlicensed redistribution of the map editor's `Source`
folder (`DeFrosto/Empire-at-War-Source-Files` on GitHub). They are Petroglyph's protected data:
they were read locally as evidence and are not part of this repository, and this document
paraphrases their developer comments instead of quoting them. 578 of the 610 FoC source files are
byte-identical to the installed ones; the source predates the last patches (see
[Patch changes](#what-the-games-and-patches-changed)), so the installed data is authoritative for
shipped behavior, and the source adds developer comments, the `Enum` folder, and the EAW state.

"Game-object definitions" are the named children of the files that `GameObjectFiles.xml` and
`HardPointDataFiles.xml` list, plus the few kinds kept in their own files (tactical cameras, lens
flares, light sources, terrain decals, black-market items, hero clashes, ambient map sounds).
Sound events, speech events, campaigns, AI goals, command-bar components, and story events are
other databases and are not covered.

Evidence markers used throughout:

- **(comment)**: a developer comment in the source XML states it.
- **(data)**: inferred from the values, the object kinds that use the tag, or related tags.
- **(name)**: only the tag's name suggests it; treat as a guess.
- **(engine)**: EAWEngine implements it this way (see the code and
  [compatibility-research.md](compatibility-research.md)); still not proof of the original.

Counts are FoC source counts unless a row says otherwise; "objects" counts definitions that use a
tag, "uses" counts occurrences.

## Definitions, kinds, and groups

A definition is an element with a `Name` attribute directly below a file's root element:
`<GroundVehicle Name="T2B_Tank"> ... </GroundVehicle>`. Its child elements are the tags. Only two
object attributes occur: `Name` (every object) and `Description` (4 objects). Tags carry
attributes only in nested blocks (`Abilities` and its children carry `Name`, and `Abilities`,
`Unit_Abilities_Data`, and `Clash_Actions` carry `SubObjectList="Yes"`).

The element name (the kind) is a label rather than a class: FoC uses 61 kinds, and the behavior
of an object comes from its tags. Evidence: 150 variants inherit across kinds (41 `Props_Story`
objects are variants of `Indigenous_Unit` types, 22 of `GroundCompany` types, 8 `UniqueUnit`
objects of `SpaceUnit` types), and class-defining behavior tokens accompany the kinds (with inheritance resolved, all 119
`GroundCompany`, 52 `HeroCompany`, and 33 `Squadron` objects carry `DUMMY_GROUND_COMPANY` or
`DUMMY_SPACE_FIGHTER_SQUADRON`; upgrades carry `DUMMY_UPGRADE`, projectiles `PROJECTILE`). This is inference from the data: whether the original's
loader ignores the element name entirely is unverified.

This document groups the kinds as follows (FoC source counts):

| Group | Kinds (objects) |
| --- | --- |
| units | `Indigenous_Unit` 138, `SpaceUnit` 92, `GroundInfantry` 92, `UniqueUnit` 91, `GroundVehicle` 77, `HeroUnit` 39, `TransportUnit` 24, `GenericHeroUnit` 18, `Slave_Unit` 8, `Mobile_Defense_Unit` 5, `LandBombingUnit` 3 |
| containers | `GroundCompany` 119, `HeroCompany` 52, `Container` 51 (infantry teams, fleets), `Squadron` 33, `SlaveCompany` 8 |
| structures | `GroundBuildable` 158, `GroundStructure` 127, `SpecialStructure` 99, `StarBase` 50 (+1 `Starbase`), `MultiplayerStructureMarker` 48, `SpaceBuildable` 30, `GroundBase` 21, `SpaceStructure` 16, `SecondaryStructure` 15, `TechBuilding` 4 |
| hardpoints | `HardPoint` 355 |
| projectiles | `Projectile` 174 |
| upgrades | `UpgradeObject` 194 |
| props | `Props_Story` 261, `SpaceProp` 242, `Props_Generic` 110, `Marker` 105, `Prop_Felucia` 97, `Props_Urban` 76, `Props_Temperate` 68, `Prop_Desert` 52, `Prop_Forest` 49, `Props_Snow` 22, `Props_Swamp` 20, `Props_Volcanic` 20, `MiscObject` 10, `ScriptMarker` 6 |
| effects | `Particle` 165, `Decal` 19, `SpecialEffect` 2, `LightSource` 1, `LensFlare` 1 |
| other objects | `Planet` 65, `LandPrimarySkydome` 22, `SpaceSecondarySkydome` 13, `Item` 10, `SpacePrimarySkydome` 9, `LandSecondarySkydome` 7, `Ambient_Map_Sound` 6 (+1 `AmbientMapSound`), `TacticalCamera` 3, `Hero_Clash` 3 |

Two kinds are spelled two ways (`StarBase`/`Starbase`, `Ambient_Map_Sound`/`AmbientMapSound`),
which also suggests that the element name is not what the loader keys on. One name is defined
twice (`ObiWan_BobaFett`).

### Files and load order

`GameObjectFiles.xml` lists the 120 files of game objects in load order (FoC); hardpoints come
from `HardPointDataFiles.xml`, and the other databases from their own metafiles (`SFXEventFiles`,
`CampaignFiles`, `FactionFiles`, `CommandBarComponentFiles`, `TargetingPrioritySetFiles`,
`TradeRouteFiles`, `MousePointerFiles`, `DataMinerXMLFiles`). A developer comment on the listing
says a file was moved up so its objects appear first on the build bar: list order is visible to
the player. EAWEngine indexes the final VFS winners of every XML file instead of following the
metafiles (`xml-audit`); following them is needed once load order matters (build bars, duplicate
names).

## Tags at a glance

FoC game objects use **1,005 distinct top-level tags** (EAW: 872; installed FoC: 1,008) and 862 nested paths inside
`Abilities`, `Unit_Abilities_Data`, `Clash_Actions`, and `Flare`. Usage is long-tailed:

| Objects using the tag | Tags |
| --- | --- |
| 1 | 173 |
| 2-4 | 143 |
| 5-19 | 210 |
| 20-99 | 279 |
| 100 or more | 200 |

The units group alone uses 496 distinct tags, structures 293, containers 143, projectiles 132,
hardpoints 56. FoC adds 136 top-level tags; three EAW tags are gone in FoC (`Fire_Bone_C`,
`Fire_Bone_D`, `Planet_Vigilance`), and the installed data adds `Engine_Hardpoint`,
`Shield_Generator_Hardpoint`, and `Weapon_Hardpoint` (patch cinematic hardpoints). EAWEngine
reads about 95 of the 1,005 tags today ([Engine coverage](#engine-coverage)); the status of every
tag, kept current as work proceeds, is in [xml-tag-status.md](xml-tag-status.md).

## Inheritance: `Variant_Of_Existing_Type`

794 FoC definitions (22%) are variants: they name a parent and inherit every tag they do not
set. Findings:

- Every parent resolves; no chain is longer than two links (717 one-link, 77 two-link variants)
  and none is cyclic.
- The tag comes first in 777 variants and later in 17, so its position does not matter (data).
- 47 variants are defined before their parent in load order, so parents resolve after the files
  load, not while one is parsed (data).
- 150 variants inherit across kinds (above).
- What variants override most: `Affiliation` 305, `Tactical_Health` 167, `Land_Model_Name` 128,
  `Scale_Factor` 125, `Text_ID` 117, `Icon_Name` 116, `Encyclopedia_Text` 112, `LandBehavior` 92,
  `Specific_Death_Anim_Index` 92, `Encyclopedia_Unit_Class` 88, `Specific_Death_Anim_Type` 87, the
  tactical build cost and time 84 each, `SFXEvent_Select` 83. Variants are mostly reskins,
  faction copies, story copies, and tactical-build versions of a base unit.
- Blocks are restated whole: 32 of the 36 variants that declare `Unit_Abilities_Data` have a
  parent that declares one too; EAWEngine treats such a block as replacing the parent's. Whether the original merges a variant's blocks with the parent's is unverified.
- Empty values are frequent in variants and parents alike (2,495 empty top-level values:
  `Text_ID` 290, `Required_Planets` 174, `Tactical_Build_Prerequisites` 172, ...); an empty tag
  most likely sets "none" and overrides the parent (inference).

EAWEngine resolves fields through `GameObjectCatalog::field` (`src/assets`): the nearest
definition in the chain that declares the tag wins, a parent of the same kind is preferred when
a name is defined twice, and lists come only from the declaring definition (replace semantics,
an engine choice matching the data).

## Value syntax

- **Case.** Tag names are matched case-insensitively in practice: `HardPoints` (84) and
  `Hardpoints` (4), `Fov_Smooth_Time` and `Fov_Smooth_time`, and a `SoundFX_Name` closed as
  `SoundFX_name` all occur in shipped data. Values are case-insensitive too (`Yes` 5,403, `No`
  3,280, `true` 1,061, `True` 916, `yes` 727, `False` 683, `false` 391, `no` 364, `YES`/`NO` 2).
- **Booleans.** `Yes`/`No` and `True`/`False` in any case.
- **Numbers.** 932 values start with a bare decimal point (`.1`); ten tags carry a C float suffix
  (`Select_Box_Z_Adjust` 157 values such as `0.0f`, `Spawn_Indigenous_Units_Radius`,
  `Spawn_Indigenous_Units_Chance`, `On_Fire_Speed_Modifier`, `Spin_Away_On_Death_Time`, ...), so
  a reader must accept both. Hex appears only in the enum files.
- **Lists.** Names and numbers are separated by commas, spaces, or both, often with padding.
  Masks use `|`: `CategoryMask` (131 objects), `Space_Layer` (69), category fields inside
  abilities, `Property_Flags` (9), `Projectile_Stun_Victims_Category_Mask`,
  `Special_Weapon_Valid_Targets`, `Attack_Category_Restrictions`; the same masks are written with
  commas or spaces elsewhere, so the separators are interchangeable (data).
- **Keyed repeats.** Some tags repeat with a key as the first list element, one entry per key:
  `Fire_Inaccuracy_Distance` (262 objects; `category, distance`), `Targeting_Fire_Inaccuracy`
  (105), `Death_Clone` (64; `damage type, clone`), `SFXEvent_Attack_Hardpoint` (62),
  `SFXEvent_Hardpoint_Destroyed` (36), `Starting_Spawned_Units_Tech_0` (31) and
  `Reserve_Spawned_Units_Tech_0` (30; `type, count`), `Squadron_Offsets` (30), `Squadron_Units`.
- **Accidental repeats.** Other tags repeat with the same value (copy-paste): `MovementClass` (61
  of 63 repeats identical), `Has_Land_Evaluator` 19, `Is_Discardable` 18,
  `Death_SFXEvent_Start_Die` 24 of 25, `GUI_Bracket_Size` 9, ... A few repeat with different
  values: `Mass` (4 objects), `Icon_Name` (4), `Scale_Factor` (6), `Targeting_Max_Attack_Distance`
  (7), `Tactical_Health` (3), `Build_Initially_Locked` (6), `Affiliation` (2). Which one the
  original uses is unverified; EAWEngine takes the last (`direct_field`), the usual override
  convention.
- **Disabled tags.** Developers disabled tags by renaming them with an `X` prefix
  (`XSFXEvent_Ambient_Loop`, `XXXSpace_Model_Name`). That shipped data carries them, and
  misspellings such as `Formation_Prority`, means the original ignores unknown tags silently
  (inference); EAWEngine does the same.

## Behaviors

Three tags list behavior tokens, the modules an object instantiates: `Behavior` (any mode; 1,492
objects), `LandBehavior` (land battles; 642), and `SpaceBehavior` (space battles; 322), plus
`GalacticBehavior`, `TacticalBehavior`, and `DeployedBehavior` on a few objects. The token decides
much of what the other tags do: a tag such as `Shield_Points` matters only with `SHIELDED`,
`Spawned_Squadron_*` only with `SPAWN_SQUADRON`, `Wind_Disturbance_*` only with
`WIND_DISTURBANCE` (a developer example in the source shows exactly this pairing). Tokens are
comma- and space-separated and case-insensitive. The `DUMMY_*` tokens mark what an object is for
the galactic and production layers (a buildable starship, a ground company, an upgrade) rather
than adding runtime behavior.

EAWEngine reads the lists for flight (`FIGHTER_LOCOMOTOR` in `SpaceBehavior`), body aiming (no
`TURRET`), and kind classification; the rest are not interpreted yet.

Installed FoC counts per list; "EAW" says whether the EAW source uses the token.

| Token | `Behavior` | `LandBehavior` | `SpaceBehavior` | `GalacticBehavior` | `TacticalBehavior` | `DeployedBehavior` | Object groups | EAW  Meaning |
|---|---|---|---|---|---|---|---|---|---|
| `HIDE_WHEN_FOGGED` | 214 | 298 | 135 |  |  |  | structure 257, unit 252, projectile 109, prop 29 | yes | hidden from opponents under fog of war (name) |
| `REVEAL` | 119 | 336 | 147 |  |  |  | structure 277, unit 210, container 61, prop 51, projectile 3 | yes | reveals fog of war around itself; ranges in `Land_FOW_Reveal_Range`/`Space_FOW_Reveal_Range` (name, data) |
| `SELECTABLE` | 391 | 92 | 58 | 25 | 9 |  | unit 251, structure 175, other_object 64, container 50, prop 34, projectile 1 | yes | the player can select it (name) |
| `IDLE` | 101 | 237 | 15 |  |  |  | unit 146, prop 114, structure 72, projectile 16, effect 4, other_object 1 | yes | plays idle animations and idle behavior (name) |
| `LAND_OBSTACLE` | 26 | 315 |  |  |  |  | structure 186, prop 149, unit 6 | yes | blocks ground movement (name, data: structures and props) |
| `UNIT_AI` | 80 | 139 | 116 |  |  |  | structure 141, unit 117, container 49, prop 28 | yes | has tactical unit AI (auto-targeting, responses) (name) |
| `SPACE_OBSTACLE` | 62 | 180 | 67 |  |  |  | structure 246, prop 63 | yes | blocks space movement; extents in `Space_Obstacle_*` (name, data) |
| `DAMAGE_TRACKING` | 7 | 190 | 80 |  |  |  | unit 139, structure 111, prop 26, projectile 1 | yes | keeps a damage state for presentation (damage thresholds, damaged models and effects) (name, data) |
| `TARGETING` | 7 | 117 | 91 |  |  |  | unit 174, prop 19, structure 15, container 7 | yes | can choose targets (name) |
| `DUMMY_UPGRADE` | 187 | 1 |  |  |  |  | upgrade 187, prop 1 | yes | class marker: an upgrade object, not a physical object (data: every UpgradeObject) |
| `WEAPON` |  | 139 | 46 |  |  |  | unit 154, structure 16, prop 15 | yes | has its own weapon (the `Projectile_Types`/fire tags) (name, data) |
| `POWERED` | 39 | 45 | 96 |  |  |  | unit 118, structure 56, prop 6 | yes | depends on power; can lose it (ion stun, power-to-weapons) (name) |
| `DEBRIS` |  | 10 | 139 |  |  |  | prop 149 | yes | flies as debris after a death (`Debris_*` tags) (data: space debris props) |
| `STUNNABLE` |  | 149 |  |  |  |  | unit 92, structure 54, prop 3 | yes | can be stunned (name) |
| `PARTICLE` | 148 |  |  |  |  |  | effect 148 | yes | class marker: a particle effect object (data: Particle kind) |
| `MARKER` | 143 | 1 | 1 |  |  |  | prop 97, structure 48 | yes | class marker: an editor or script marker without gameplay (data) |
| `SHIELDED` | 30 | 20 | 93 |  |  |  | unit 89, structure 51, prop 3 | yes | has shields (`Shield_Points`, `Shield_Refresh_Rate`) (name, data) |
| `DUMMY_GROUND_COMPANY` | 141 |  |  |  |  |  | container 140, prop 1 | yes | class marker: a ground company that expands into its `Company_Units` (data) |
| `ABILITY_COUNTDOWN` | 5 | 63 | 60 |  |  |  | unit 102, prop 11, container 10, structure 4, effect 1 | yes | runs ability recharge timers (data: units with `Unit_Abilities_Data`) |
| `ION_STUN_EFFECT` |  | 51 | 77 |  |  |  | structure 85, unit 40, prop 3 | yes | shows the ion-stun effect and can be ion stunned (name) |
| `SURFACE_FX` |  | 122 |  |  |  |  | unit 108, prop 14 | yes | leaves surface effects (tracks, footprints) from SurfaceFX.xml (name, data) |
| `PROJECTILE` | 119 |  |  |  |  |  | projectile 119 | yes | class marker: a projectile (data) |
| `WALK_LOCOMOTOR` |  | 113 |  |  |  |  | unit 92, prop 21 | yes | the ground locomotor of single ground units (data: infantry, vehicles, heroes) |
| `TERRAIN_TEXTURE_MODIFICATION` |  | 98 |  |  |  |  | structure 97, prop 1 | yes | paints the terrain under it (`Terrain_Texture_Modifier_*`) (name, data) |
| `EARTHQUAKE_TARGET` |  | 92 |  |  |  |  | structure 89, prop 3 | yes | damaged by the earthquake bombardment (name) |
| `GARRISON_UNIT` |  | 86 |  |  |  |  | container 41, unit 40, prop 5 | FoC | can enter garrisons (`Garrison_Category`) (name, data) |
| `DUMMY_GROUND_STRUCTURE` | 69 | 1 |  |  |  |  | structure 68, prop 2 | yes | class marker: a buildable ground structure in galactic play (data) |
| `NEBULA` |  |  | 68 |  |  |  | unit 66, prop 2 | yes | affected by nebulas (name) |
| `PLANET` | 64 |  |  |  |  |  | other_object 64 | yes | class marker: a galactic planet (data) |
| `PRODUCTION` | 64 |  |  |  |  |  | other_object 64 | yes | can produce objects (planets) (name, data) |
| `DUMMY_LAND_BASE_LEVEL_COMPONENT` | 1 | 59 |  |  |  |  | structure 58, prop 2 | yes | counts toward a planet's ground base level (name, data) |
| `SPAWN_SQUADRON` |  | 17 | 41 |  |  |  | structure 49, unit 7, prop 2 | yes | launches fighter squadrons (`Spawned_Squadron_*`, `Starting_Spawned_Units_Tech_0`) (name, data) |
| `SIMPLE_SPACE_LOCOMOTOR` |  |  | 52 |  |  |  | unit 50, prop 2 | yes | the locomotor of capital ships, frigates, corvettes (data) |
| `DUMMY_TOOLTIP` | 6 | 41 | 2 |  |  |  | structure 34, prop 10, unit 5 | yes | shows a tooltip only (data) |
| `TEAM` | 49 |  |  |  |  |  | container 49 | yes | class marker: an infantry team container (data: Container kind) |
| `TACTICAL_UNDER_CONSTRUCTION` | 48 |  |  |  |  |  | structure 48 | yes | can be under construction on a tactical build pad (name, data) |
| `TACTICAL_BUILD_OBJECTS` | 44 | 1 |  |  |  | 1 | structure 45, unit 1 | yes | a tactical build pad that builds `Tactical_Buildable_Objects_*` (name, data) |
| `DUMMY_STARSHIP` | 45 |  |  |  |  |  | unit 43, prop 2 | yes | class marker: a buildable starship in galactic play (data) |
| `LAND_TEAM_CONTAINER_LOCOMOTOR` |  | 41 |  |  |  |  | container 41 | yes | locomotor of a ground team container (data) |
| `TEAM_TARGETING` |  | 41 |  |  |  |  | container 41 | yes | targeting shared by a team (data) |
| `AMBIENT_SFX` |  | 9 | 30 |  |  |  | unit 39 | yes | plays ambient sound loops (`SFXEvent_Ambient_*`) (name, data) |
| `DUMMY_SPECIAL_WEAPON_SOURCE` | 38 |  |  |  |  |  | prop 38 | yes | source point of a special weapon shot (data) |
| `DUMMY_TACTICAL_SUPER_WEAPON_TARGET` | 37 |  |  |  |  |  | prop 37 | yes | target of the tactical super weapon (data) |
| `LAND_TEAM_INFANTRY_LOCOMOTOR` |  | 37 |  |  |  |  | unit 37 | yes | locomotor of a team member (data: squad infantry) |
| `AFFECTED_BY_SHIELD` |  | 35 |  |  |  |  | unit 34, prop 1 | yes | blocked by base shields (name) |
| `FIGHTER_LOCOMOTOR` |  | 6 | 28 |  |  |  | unit 34 | yes | the locomotor of fighters and bombers (never stop, attack runs) (data; engine: flight model) |
| `TURRET` |  | 26 | 8 |  |  |  | unit 18, structure 14, prop 2 | yes | has a rotating turret; without it infantry aim with the body (data; engine: aims_with_body) |
| `TACTICAL_SELL` | 24 |  | 8 |  |  |  | structure 31, prop 1 | yes | can be sold in tactical play (`Tactical_Sell_Credits`) (name) |
| `TRANSPORT` | 32 |  |  |  |  |  | unit 31, prop 1 | yes | carries units to the ground (`Transport_Capacity`) (name, data) |
| `SPAWN_INDIGENOUS_UNITS` |  | 31 |  |  |  |  | structure 28, prop 3 | yes | spawns indigenous units (`Spawned_Indigenous_*`) (name, data) |
| `ASTEROID_FIELD_DAMAGE` |  |  | 29 |  |  |  | unit 28, prop 1 | yes | takes damage in asteroid fields (name) |
| `CAPTURE_POINT` | 23 | 5 |  |  |  |  | structure 26, prop 2 | yes | a capturable point (`Capture_Point_*`) (name, data) |
| `DUMMY_ORBITAL_STRUCTURE` | 17 |  | 9 |  |  |  | structure 25, prop 1 | yes | class marker: a buildable orbital structure (data) |
| `DUMMY_SPACE_FIGHTER_SQUADRON` | 26 |  |  |  |  |  | container 26 | yes | class marker: a squadron that expands into `Squadron_Units` (data) |
| `HINT` | 25 |  |  |  |  |  | prop 21, unit 3, structure 1 | yes | an AI hint point (data: props) |
| `SKY_DOME` | 25 |  |  |  |  |  | other_object 25 | yes | class marker: a sky dome (data) |
| `TELEKINESIS_TARGET` |  | 24 |  |  |  |  | unit 23, prop 1 | yes | can be lifted by Force telekinesis (name) |
| `SQUASH` |  | 22 |  |  |  |  | unit 21, prop 1 | yes | crushes infantry it drives over (name, data: walkers and tanks) |
| `DUMMY_STAR_BASE` | 21 |  |  |  |  |  | structure 20, prop 1 | yes | class marker: a space station level (data) |
| `INDIGENOUS_UNIT` |  | 21 |  |  |  |  | unit 20, prop 1 | yes | an indigenous (neutral native) unit (name) |
| `WIND_DISTURBANCE` |  | 17 |  |  |  |  | unit 16, prop 1 | yes | stirs grass and particles (`Wind_Disturbance_*`) (comment, data) |
| `DUMMY_GROUND_BASE` | 16 |  |  |  |  |  | structure 16 | yes | class marker: a ground base level (data) |
| `BASE_SHIELD` | 10 | 4 |  |  |  |  | structure 14 | yes | projects a base shield (`Base_Shield_*`) (name, data) |
| `HUNT` |  | 8 | 5 |  |  |  | unit 13 | yes | hunts targets automatically (name) |
| `WALL` |  | 9 |  |  |  |  | structure 9 | yes | a wall segment (name, data) |
| `EXPLOSION` | 8 |  |  |  |  |  | effect 8 | yes | an explosion particle object (data) |
| `IMPOSING_PRESENCE` |  | 8 |  |  |  |  | unit 7, prop 1 | yes | affects units in its presence range (`Presence_Range`) (name, data) |
| `TEAM_LOCOMOTOR` | 8 |  |  |  |  |  | container 8 | yes | locomotor of a team container (data) |
| `HOLSTER_WEAPON` |  | 7 |  |  |  |  | unit 6, prop 1 | yes | holsters and draws its weapon (`Holster_*`) (name, data) |
| `BOMB` | 5 | 1 |  |  |  |  | projectile 5, prop 1 | yes | falls as a bomb (name, data) |
| `SELF_DESTRUCT` |  | 4 | 2 |  |  |  | unit 4, prop 2 | yes | can self-destruct (name) |
| `FLYING_LOCOMOTOR` |  | 5 |  |  |  |  | unit 5 | yes | the locomotor of land air units (data) |
| `TREAD_SCROLL` |  | 5 |  |  |  |  | unit 4, prop 1 | yes | scrolls tread textures by speed (`Tread_Scroll_Rate`) (name, data) |
| `HARASS` |  | 4 |  |  |  |  | unit 4 | yes | harasses enemies (`Harass_Enemy_Exclusion_Range`) (name) |
| `REINFORCEMENT_POINT` | 4 |  |  |  |  |  | prop 4 | yes | a reinforcement point (name) |
| `BARRAGE` |  | 1 | 2 |  |  |  | unit 3 | yes | can fire an area barrage (name) |
| `CASH_POINT` | 3 |  |  |  |  |  | prop 3 | yes | a point that yields credits when held (`Cash_Point_*`) (name, data) |
| `DUMMY_DESTROY_AFTER_BOMBING_RUN` | 3 |  |  |  |  |  | upgrade 3 | yes | an upgrade object removed after its bombing run (name) |
| `DUMMY_DESTROY_AFTER_PLANETARY_BOMBARD` | 3 |  |  |  |  |  | upgrade 3 | FoC | an upgrade object removed after its bombardment (name) |
| `DYNAMIC_TRANSFORM` | 1 | 2 |  |  |  |  | effect 1, prop 1, unit 1 | FoC | changes scale and colour over time (`Dynamic_Transform_*`) (name, data) |
| `GARRISON_VEHICLE` |  | 3 |  |  |  |  | unit 3 | FoC | a vehicle that can be garrisoned (name) |
| `GRAVITY_CONTROL_FIELD` |  | 3 |  |  |  |  | structure 3 | yes | slows units in a field (`Gravity_Control_Field_*`) (name, data) |
| `LANDBOMBER_LOCOMOTOR` |  | 3 |  |  |  |  | unit 3 | yes | the locomotor of bombing-run bombers (data) |
| `LANDING` |  | 3 |  |  |  |  | unit 3 | yes | lands and takes off (transports) (name) |
| `DEMOLITION` |  | 2 |  |  |  |  | unit 2 | yes | can plant demolition charges (name) |
| `DEPLOY_TROOPERS` |  | 2 |  |  |  |  | unit 2 | yes | deploys troopers (AT-AT) (name, data) |
| `DISABLE_FORCE_ABILITIES` |  | 2 |  |  |  |  | structure 2 | FoC | blocks Force abilities in range (`Disable_Force_Abilities_Range`) (name) |
| `DUMMY_DESTROY_AFTER_SPECIAL_WEAPON_FIRED` | 2 |  |  |  |  |  | upgrade 2 | yes | an upgrade object removed after its special weapon fired (name) |
| `FLEET` | 2 |  |  |  |  |  | container 2 | yes | class marker: a fleet container (data) |
| `GARRISON_STRUCTURE` |  | 2 |  |  |  |  | structure 2 | FoC | a structure that holds a garrison (name) |
| `LOBBING_SUPERWEAPON` |  | 2 |  |  |  |  | structure 2 | yes | a super weapon that lobs its shot (`Lobbing_Superweapon_*`) (name, data) |
| `LURE` |  | 1 | 1 |  |  |  | unit 2 | yes | lures enemies (name) |
| `PROXIMITY_MINE` | 2 |  |  |  |  |  | prop 1, projectile 1 | FoC | a proximity mine (name) |
| `SPECIAL_WEAPON` |  |  | 2 |  |  |  | structure 2 | yes | a special weapon (`Special_Weapon_*`) (name, data) |
| `TACTICAL_SUPER_WEAPON` |  |  | 2 |  |  |  | unit 2 | yes | the tactical super weapon (`TSW_*`) (name, data) |
| `VEHICLE_THIEF` |  | 2 |  |  |  |  | unit 1, prop 1 | yes | can steal vehicles (name) |
| `BIKE_LOCOMOTOR` |  | 1 |  |  |  |  | unit 1 | yes | a speeder-bike locomotor (name) |
| `BUZZ_DROIDS` |  |  | 1 |  |  |  | unit 1 | FoC | releases buzz droids (name) |
| `DUMMY_CANT_PATHFIND` | 1 |  |  |  |  |  | unit 1 | FoC | excluded from pathfinding (name) |
| `FLEET_LOCOMOTOR` | 1 |  |  |  |  |  | container 1 | yes | locomotor of a fleet container (data) |
| `GARRISON_HOVER` |  | 1 |  |  |  |  | unit 1 | FoC | a hover vehicle that can be garrisoned (name) |
| `MULTIPLAYER_BEACON` | 1 |  |  |  |  |  | prop 1 | yes | a multiplayer beacon (name) |

## Categories, properties, and enums

The source's `Enum` folder holds the enumerations whose names tags use:

- **`GameObjectCategoryType`** (the `CategoryMask` names; a 32-bit bit field per a developer
  comment, with an older commented-out numbering): Fighter, Bomber, Transport, Corvette, Frigate,
  Capital, Super, Infantry, Vehicle, Air, Structure, the eleven `Anti*` counterparts, LandHero,
  Wall, Upgrade, SpaceHero, NonCombatHero (27 bits). The stock `CategoryMask` values use 26 of
  them: Structure 284, Upgrade 187, Infantry 94, LandHero 61, SpaceHero 49, Capital 48, Fighter
  31, Vehicle 31, AntiBomber 28, AntiVehicle 27, NonCombatHero 19, Corvette 17, AntiFrigate 16,
  AntiFighter 16, AntiCapital 14, Wall 13, AntiCorvette 13, Frigate 12, Transport 11,
  AntiInfantry 10, AntiStructure 9, Air 8, Bomber 7, AntiAir 6, AntiSuper 3, Super 1. The `Anti*`
  bits describe what a unit is good against (AI and encyclopedia), the others what it is. Scripts
  test them with `Is_Category` (48 calls in the stock Lua: Infantry 18, Vehicle 11, Structure 7,
  LandHero 7, ...).
- **`GameObjectPropertiesType`** (the `Property_Flags` names, a bit field of 14): HealsInfantry,
  HealsVehicles, Turret, TacticalStructure, InBase, OutBase, Fodder, SmallShip,
  NotOpportunityTarget, GoodInfantryCrusher, IsSupercrusher, IsRushTarget, CanContainGarrison,
  GarrisonCanFire. Stock use: SmallShip 43, InBase 33, NotOpportunityTarget 12,
  TacticalStructure 9, GoodInfantryCrusher 9, Turret 5, CanContainGarrison 4, Fodder 2,
  IsSupercrusher 2, and one each of the rest except OutBase (unused). Scripts test them with
  `Has_Property` (6 calls). Some duplicate a dedicated tag (`Is_Supercrusher` next to the
  `IsSupercrusher` flag on the AT-AT).
- **`MovementClassType`** (18): Infantry, Wheeled, Walker, Flying, Tracked, Hover, LargeWalker,
  Space, Lancet, Spotter, LargeHover, BombingRun, and `*NoShield` versions of Infantry, Wheeled,
  Walker, Tracked, Hover, and LargeWalker. Stock `MovementClass` values use 12: Infantry 133,
  Space 76, Hover 12, Walker 8, Flying 8, Tracked 5, BombingRun 3, LargeWalker 2, Lancet 2,
  Spotter 1, LargeHover 1, Wheeled 1. `MovementClassTypeDefs.xml` maps each class to the
  passability it may enter (EAWEngine reads it).
- **`ObjectWeatherCategoryType`** (9): Default, Infantry, LargeWalker, Speeder, Walker, Tracked,
  Hover, Wheeled, Flying, the `Weather_Category` values (stock: Infantry 55, Walker 11, Hover 10,
  Flying 7, Tracked 5, LargeWalker 3, Speeder 2).
- **`SurfaceFXTriggerType`** (33): the surface-effect triggers (tracks, footprints per walker,
  hover, projectile impacts, scorch marks by size, ground types) that `SurfaceFX.xml` and
  animation events name.
- **`PerceptionTokenType`** (229) and **`AIGoalCategoryType`** (18): AI perception and goal
  vocabularies, outside this document.

## What the games and patches changed

- **EAW to FoC.** 2,091 of the 2,097 EAW objects survive into FoC (dropped: two reinforcement
  markers, an E3 planet, a swamp tree, two Veers AT-AT death clones). FoC adds about 1,500
  objects (the Underworld faction, new heroes, corruption, props) and 136 tags.
- **FoC source to the installed patch.** 47 objects changed and 38 were added (cinematic
  `CIN_*` units, props, and star destroyer hardpoints for the patch's cinematics); none was
  removed. The patch adds `Damage_Type` to 13 objects, an `Eat_Attack_Ability` to two, `Ship_Class`
  to two; changes `Special_Structures_Space` on 11 planets, combat-bonus ability recharge,
  activation, and target lists, and three death clones; and removes one `Variant_Of_Existing_Type`.

## Script-visible tags

The stock Lua reads type data through a few calls, which makes the tags behind them script
contracts: `Is_Category` (`CategoryMask`), `Has_Property` (`Property_Flags`), `Activate_Ability`,
`Is_Ability_Ready`, and `Is_Ability_Active` (100, 26, and 6 calls; the names are
`Unit_Abilities_Data` types such as SPREAD_OUT, Turbo, SPOILER_LOCK, DEPLOY, ROCKET_ATTACK,
STEALTH), `Get_Type`/`Get_Name` (the definition name), `Get_Hull` and `Get_Shield` (health and
shields), `Get_Faction_Name` (`Affiliation`), `Get_Combat_Rating` (`AI_Combat_Power`),
`Get_Build_Cost`/`Get_Tactical_Build_Cost`, `Is_Hero`, `Is_Transport`, `Is_In_Garrison`,
`Get_Contained_Object_Count`, and `Lua_Script` names the script an object runs. EAWEngine's
script host implements the subset listed in [lua-runtime-research.md](lua-runtime-research.md).

## The tag catalogue

Every top-level tag of the FoC and installed data has one row in one of the sections below (nested ability fields are in the abilities section). Rows give who uses the tag, its values, and its meaning with an evidence marker; each section ends with its findings.

### Movement, locomotion, formations, and collision

These tags set how an object moves (speed, turning, acceleration, flight attitude), which locomotor and which passability class drive it, how it joins and keeps formations, how much ground or space it occupies, and how other objects collide with or crush it. Most are scalar per-type values with no repeats; the few repeats are identical copies (team containers that list `MovementClass` twice). Ground units mix two spelling styles (`Max_Speed` beside `OverrideAcceleration`, `FormationOrder`, `MaxJiggleDistance`), and several settings exist in two spellings. Values are in the original's internal units (per 30 Hz frame where measurable); GameConstants.xml holds the global defaults and multipliers many of them combine with. A handful of rows (camera, turret, debris, objective, particle) landed in this family by name match; they are listed for completeness and marked.

#### Speed and acceleration

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Max_Speed` | units 262, projectiles 123, containers 48, props 9 (442) | float 0..400; ground infantry about 0.8-1.2, vehicles 1-3, fighters 4-5, capitals 1.4, projectiles 5-400 (e.g. 11, 25) | Top speed (name, data). Projectiles use it as flight speed; developer comments tie grenade arc shape and reach to it plus the grenade gravity tags (comment). Variants of space units often set 0.25 (galactic or cinematic copies) (data). Engine: reads as speed x 30 (provisional per-frame-to-per-second scale) for units and projectile speed. |
| `Min_Speed` | units 107, containers 7, props 1 (115; 1 repeat) | float 0..3.5; fighters 1.5-2.5, many ground .1 | Lowest cruising speed (name); fighters never drop below it (data). Also set on corvettes and ground units, where its effect is unknown. Engine: reads only for types whose SpaceBehavior has FIGHTER_LOCOMOTOR (x30; half of top speed when absent). |
| `Alternate_Max_Speed` | heroes 8, vehicles 2 (10) | float 1.8..2.7 | Top speed while using the secondary locomotor (jetpack heroes Boba Fett, Bossk, IG-88, Silri, Tyber Zann; Dark Trooper phases) (data). |
| `OverrideAcceleration` | units 139, props 5, containers 1 (145) | float .02..2.0; .1 typical ground, .02 capitals | Acceleration replacing a derived default (name, data). Engine: speed per frame per frame (x900, a guess); absent means instant speed change. |
| `OverrideDeceleration` | units 139, props 5, containers 1 (145) | float .02..2.0; .1/.25 typical | Braking counterpart of OverrideAcceleration (name, data). Engine: as OverrideAcceleration. |
| `Override_Acceleration` | containers 40 (infantry team containers such as Stormtrooper_Team) | float .08 or .1 | Alternate spelling of the acceleration override, used only on land team containers (data). Whether the original reads both spellings is unknown. Engine: not read. |
| `Override_Deceleration` | containers 40 (same team containers) | float .08 or .1 | Alternate spelling of OverrideDeceleration on team containers (data). Engine: not read. |
| `Max_Thrust` | space units 67, unique 40, transports 8, flying indigenous 6, bombers 3, vehicles 2, props 2 (128) | float .02..4.5; .05 frigates, 1.0 fighters | Thrust limit of the flight model (name). Comments on the Death Star use it for moving into range and accelerating or decelerating there (comment). Relation to OverrideAcceleration unknown. |
| `LateralAcceleration` | infantry 1 (Scout_Trooper) | .15 | Sideways acceleration of the speeder bike (name, data). Single use. |
| `Mass` | units 219, structures 168, planets 65, props 16, containers 2, projectile 1 (471; 15 repeats) | float .98..1.3; 1.0 for 400 uses; .98-.9995 on fighters, flyers, ships | Despite the name, values near 1 on moving flyers and ships look like a per-frame velocity retention (inertia/damping) factor (data, guess); structures and planets carry the default 1.0. One 1.3 (UC_U_Ground_Arms_Depot) is an outlier. |
| `Defend_Mode_Speed_Multiplier` | space unit 1, unique 1 (Venator types) | 0.5 | Speed factor while in a defend mode (name). |
| `On_Fire_Speed_Modifier` | indigenous 22, slaves 7, infantry 4 (33) | `1.2f` (C-style suffix) | Speed factor while burning (name); slightly faster, as fleeing burning infantry (data). |
| `Hyperspace` | space units 45, unique 15, transports 8, containers 5, props 2 (75) | bool | Whether the object can use hyperspace (name). |
| `Hyperspace_Speed` | same set (75; 1 repeat) | float 0..2; 1.5 typical, 1 capitals | Speed of hyperspace entry or exit (name). |
| `Can_Hyperspace_Without_Activating_Ability` | generic heroes 2 (probe droid, refugee) | No | Galactic: a probe droid moved alone must use its spy ability at the destination (comment). |
| `Disallows_Hyperspace_Retreat` | space unit 1 (Interdictor) | No | Explicitly off for the Interdictor because its gravity-well ability handles retreat blocking (comment). |
| `Hyperspace_Fleet_Reveal_Range` | heroes 2 (Vader), structures 2, planet 1 | float 0..150 | Galactic range at which arriving fleets are revealed (name). |
| `Space_Full_Stop_Command` | space units 14, unique 8, vehicles 2, prop 1 (25) | bool, mostly Yes | Whether the unit offers a full-stop command (name). |

#### Turning and flight attitude

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Max_Rate_Of_Turn` | units 257, projectiles 120, containers 41, props 7 (425) | float 0..360; ground 10-20, fighters 3, capitals .3-.4, 0 on 87 (mostly projectiles), 360 on Proj_Drunk_Missile | Turn rate (name); on projectiles, homing turn rate (data). Engine: degrees per 30 Hz frame (x30 deg/s); 0 turns instantly. |
| `Alternate_Max_Rate_Of_Turn` | heroes 8, vehicle 1 (9) | 6.0 | Turn rate with the secondary (jetpack) locomotor (data). |
| `Stopped_Rate_Of_Turn` | vehicles 8, unique 2 (walkers, Gargantuan) (10) | float 1..15.5 | Turn rate while not moving (name, data). Engine: land turn-in-place rate, falling back to Max_Rate_Of_Turn; the 45 degree turn-in-place threshold is an engine choice. |
| `Min_Speed_Fraction_For_Turn` | space 9, vehicles 8, unique 2, others 2 (21) | float 0..0.7 | Speed fraction kept or required while turning (name). Engine: clamped 0..1; default 0 on land, 0.25 in space (engine choice). |
| `Close_Enough_Angle_For_Move_Start` | indigenous 2, vehicles 2, unique 1, infantry 1 (6) | 30..360 | Facing error within which a unit may start moving instead of turning first; added to make the AT-AT turn look right (comment). GameConstants default CloseEnoughAngleForMoveStart 180. |
| `Begin_Turn_Towards_Distance` | space 17, unique 10, flying indigenous 5 (32) | float 100..900 | Distance at which a flyer starts turning toward its goal (name). |
| `Bank_Turn_Angle` | space 67, unique 41, transports 8, indigenous 6, vehicles 6, bombers 3, props 2, infantry 1 (134) | int 0..80; 5 capitals, 40 fighters | Maximum bank (roll) angle in a turn, degrees (name, data). |
| `Max_Rate_Of_Roll` | same 134 objects | float .02..10; .02-.2 ships, 5 fighters | Rate of banking into a turn (name). |
| `Max_Lift` | space 23, unique 11, heroes 8, indigenous 6, vehicles 6, bombers 3 (57; 1 repeat) | float 0..5, some `0.0f` | Vertical climb or dive limit of flyers (name). |
| `MaxFacingLookAheadFrames` | flying indigenous 6, vehicles 3, bombers 3 (12) | 5 or 30 | Frames a flyer looks ahead when choosing its facing (name). GameConstants has SpaceLocomotorFacingLookaheadAcc 1.0. |
| `UseSecondaryFacing` | vehicles 8, mobile defense 1 (9) | bool, 7 true | Hover/repulsor vehicles (T2-B, 2-M, skiff, F9TZ, Canderous tank) whose body facing may differ from travel direction (data, name). |
| `SecondaryTurnAngle` | same 8 | 90 (Skiff 65) | Angle limit of the secondary facing, degrees (name). |
| `SecondaryTurnInPlaceROTCoefficient` | same 8 | .4/.5 | Turn rate factor of the secondary facing when turning in place (name). |
| `MinSecondaryTurnROTCoefficient` | same 8 | .01..0.4 | Lower bound of the secondary facing's turn rate factor (name). |
| `MaxSecondaryTurnROTCoefficient` | same 8 | .7/.9 | Upper bound of that factor (name). |
| `SecondaryTurnLookaheadDistance` | same 8 | 0..60 | Path look-ahead used by the secondary facing (name). |
| `Has_Pre_Turn_Anim` | vehicles 10, indigenous 3, unique 1, hero 1 (15) | bool | Plays a turn-start animation before turning (walkers, rancor, tauntaun) (name, data). |
| `Walk_Transition` | vehicles 9, indigenous 2, infantry 1, unique 1, hero 1 (14; 2 repeats) | bool, 15 Yes | Uses transition clips between standing and walking (name). |
| `Locomotor_Has_Animation_Priority` | vehicles 6, unique 1 (walkers, droids) (7) | yes | Movement follows the walk animation rather than the reverse (name, guess). |
| `Should_Cause_Limited_Turrets_To_Turn` | vehicles 3 (Snowspeeder, Lancet, Gallofree HTT) | false | Whether a limited-arc turret makes the body turn (name). |
| `Movement_Directions_Count` | unique 1 (Gargantuan_Battle_Platform) | 2 | FoC only. Number of movement directions (forward/back) (name, guess). |

#### Locomotors and movement class

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `MovementClass` | units 201, containers 46, props 5 (252; 63 repeats, identical copies) | Infantry 174, Space 97, Hover 12, Flying 9, Walker 8, Tracked 5, BombingRun 3, LargeWalker 2, Lancet 2, Spotter 1, LargeHover 1, Wheeled 1 | Passability class: names an entry of MovementClassTypeDefs.xml listing the passability types the class may enter (data); numbered 0..17 in Enum/MovementClassType.xml. GroundVehicle entries spread over Hover 7, Walker 7, Infantry 6, Tracked 4, Flying 3, Space 2, Lancet 2 and others. Engine: reads it into a passability mask; Clear or Shield when missing or unknown. |
| `Primary_Locomotor_Name` | story props 8, heroes 3, vehicles 2 (13) | WALK_LOCOMOTOR | Default locomotor of a multi-locomotor type (data). |
| `Secondary_Locomotor_Name` | heroes 1, vehicles 2 (Boba Fett, Dark Trooper II/III) | JETPACK_LOCOMOTOR | The alternate locomotor (data). |
| `Uses_Multiple_Locomotors` | heroes 8, vehicles 2 (10) | Yes | Enables switching locomotors, grouped with jetpack settings (comment). |
| `Hover_Offset` | transports 9, heroes 8, unique 8, indigenous 6, vehicles 5, bombers 3, space 1 (40; 1 repeat) | float 0..11000; 16-100 typical; 400 and 11000 on cinematic landers | Height above terrain at which a hovering or flying unit travels (name, data). Engine: spawn height for types that do not follow the ground. |
| `Hover_Height` | vehicles 2 (Lancet, Gallofree HTT) | 80 | FoC only. Second hover-height setting next to Hover_Offset (name); relation unknown. |
| `Prepare_Strafe_Height` | flying indigenous 6, vehicles 3, bombers 3 (12) | 75..250 | Height a land flyer climbs to before a strafing run (name). |
| `Strafe_Distance` | space fighters 16 | 400/500 | Length of a fighter's strafing pass (name). |
| `Minimum_Follow_Distance` | space 18, unique 2 (20) | 25..60 | Closest distance when following another unit (name). |
| `Avoid_Enemy_Exclusion_Range` | space 1 (TIE_Scout) | 500 | Distance kept from enemies (name). Single use. |
| `Avoidance_Disabled` | indigenous 2, props/misc 2, projectile 1 (5) | bool (true/Yes) | Others do not avoid this object, or it does not avoid (dust devil, mines, remote bomb) (name). |
| `PathFrameDelay` | vehicles 3 (Snowspeeder, Lancet, HTT) | 0/4 | Frames between path updates (name). GameConstants Max_Move_Frame_Delay 6. |
| `MovementPredictionInterval` | containers 41, vehicles 6, unique 1 (48) | 3..12, 4 typical | Prediction horizon for movement collision checks (name). GameConstants LandPredictionTimeInterval 2.0. |
| `MovementBoxExpansionFactor` | AT-AT and Veers' AT-AT (2) | 8.0 | Enlarges the box swept while moving (name). |
| `Snap_Movement_Orders_To_Center` | prop 1 (Prop_Good_Ground_Area) | true | Move orders onto this object go to its center (name). |
| `Autonomous_Move_Extension_Vs_Attacker` | containers 70, units 55, props 2 (127) | float 0..500; 200 typical | How far a unit may move on its own to respond to an attacker (name). |
| `Max_Distance_From_Spawner` | ground structures 49, props 4 (53) | int 0..220; 150 typical | Leash of units spawned by a structure (civilian houses) (name, data). |
| `Is_Stationary_When_Attacking` | heroes 8, others 4 (12) | 11 Yes | Unit stops to attack (Jedi, Vader, Palpatine, Elite Guard) (name, data). |
| `Destroy_When_Stunned_Over_Water` | vehicle 1 (MAL_Rocket_Vehicle) | Yes | FoC only. Dies when stunned above water (name). |
| `Stationary_Space_Layer` | vehicle 1 (Snowspeeder) | Land | Layer used while not moving (the landed speeder) (name, data). |

#### Space layers and space obstacles

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Space_Layer` | structures 233, units 182, props 119, containers 41, projectile 1 (576; 2 repeats) | Land 241, Corvette 143, StaticObject 84, Frigate \| Corvette 60, Frigate 16, None 11, Capital 10, Capital \| Frigate \| Corvette 9, SuperCapital 3, empty 1; lists with `\|` | Height/collision layer (data). In space: ship class layer (Corvette, Frigate, Capital, SuperCapital); static props and stations StaticObject; fighters set none. On land: Land for ground units, None for some walkers, Frigate for air units (snowspeeder, air whale); ground structures and tall props list the air layers they block (data). Engine: Land or None means the type walks on terrain. (The dump's Priority_Set references are name collisions.) |
| `Space_Obstacle_Offset` | structures 140, props 35 (175) | vector3, nearly all 0 0 0 | Center offset of the obstacle used by space-style pathfinding; ground structures carry it because snowspeeders path the space way (comment). |
| `Space_Obstacle_Radius` | props 34, structure 1 (35) | int 0..2800 | Radius of that obstacle (name, comment). Mostly space props and wrecks. |
| `Debris_Movement_Vector` | space props 149 (hardpoint breakoffs) | vector3, e.g. 0,0,-0.5 | Off-topic for movement proper: drift direction of debris pieces broken off hardpoints (name, data). |

#### Footprint, extents, and passability

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Custom_Soft_Footprint_Radius` | units 86, structures 63, props 4 (153) | 3..550; 3.5 infantry, 100-550 starbases | Soft (yielding) personal-space radius (name, data). Engine: reads it, accepted in (0, 10000). |
| `Custom_Footprint_Radius` | space 18, unique 3 (fighters, freighters) (21) | 7 or 15 | Footprint radius of small space craft (data). Different tag from the soft radius. |
| `Custom_Hard_XExtent` | units 139, props 45, structures 43 (227; 2 repeats) | 2..800 | Hard collision box size along X (name). One unit's extents are large to compensate for its scale factor; some flyers set them for folded-wing ground collision (comment). |
| `Custom_Hard_YExtent` | units 137, props 45, structures 43 (225; 2 repeats) | 2..800 | Same along Y (name). |
| `Custom_Hard_XExtent_Offset` | props 41, units 14 (55) | -42..30 | Offset of the hard box along X (name); death clones and props (data). |
| `Custom_Hard_YExtent_Offset` | props 41, units 3 (44) | -24..21 | Same along Y (name). |
| `Collision_Box_Modifier` | space 19, unique 1 (20) | 2.0; 250 once (a SpaceUnitsSupers entry) | Scale on the collision box (name). |
| `Collision_Mesh` | hardpoints 267 | bone names (HP_*_COLL, SPAWN_00) | Off-topic: bone of a hardpoint's collision or attachment; some are only HUD reticle anchors (comment). |
| `Obstacle_Width` | structures 21, units 13, props 7 (41) | 0..220 | Width of a rectangular ground obstacle (build pads, landing transports) (name). One cave comment says passability for spawned units must be painted by hand (comment). |
| `Obstacle_Height` | same 41 | 0..220 | Depth of that rectangle (name). |
| `Obstacle_X_Offset` | 40 | -110..0, usually -width/2 | Corner offset of the rectangle (data). |
| `Obstacle_Y_Offset` | 40 | -110..-10 | Same along Y (data). |
| `Obstacle_Proxy_Type` | structures 77 | names of build-pad types (73), empty 4 | Type whose obstacle footprint this structure uses, deferring to its build pad (comment). |
| `OverridePassability` | ground structures 6 (walls) | EmpireWall, RebelWall | Passability type the object paints (data). Engine: obstacle passability mask. |
| `SecondaryOccupationPassability` | special structures 3 (base shields) | Shield | Passability type painted over the covered area (data). |
| `Fine_Tune_Occupied_Passability` | structures 14 | false/No only | Finer occupied-cell passability (name); never enabled in stock. |
| `OccupationStyle` | units 134, containers 41, props 3 (178) | 1x1 only | Occupation grid style (name). Single value. |
| `UnitCollisionClass` | units 131, props 45, containers 41, structures 32, projectile 1 (250; 2 repeats) | Infantry 123, Vehicle 54, Medium Obstacle 20, High Obstacle 19, Landing Transport 10, Giant Vehicle 9, Large Vehicle 8, Build Pad 6, Low Obstacle 3, Bike 1 | Collision category deciding who blocks or pushes whom (name, data). Engine: an "obstacle" class makes the object a static obstacle. |
| `MaxJiggleDistance` | vehicles 16, others 13 (29) | 0..6 | How far a unit drifts when nudged at rest (name). |
| `MinimumPushReturnDistance` | units 98, props 2 (100; 2 repeats) | 10, 60, 100 | Distance beyond which a pushed unit returns to its spot (name). GameConstants RepushDistance 6.0. |

#### Crushing

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Is_Squashable` | units 75, containers 40 (115; 5 repeats) | bool (Yes/true/No) | Can be crushed by vehicles (name). |
| `Is_Squashable_By_Supercrusher` | vehicles 15, ground structures 14 (walls), props 3, others 2 (34) | bool | Can be crushed by a supercrusher (name). |
| `Is_Supercrusher` | AT-AT, Veers' AT-AT, Gargantuan, Gallofree HTT (4) | Yes | Crushes vehicles and walls (name, data). |
| `Squash_Damage_Type` | indigenous 1 (Dust_Devil) | Damage_Force_Whirlwind | Damage type dealt when squashing (name). |
| `Cache_Crusher_Boxes` | vehicle 1 (AT_AT_Walker) | No | Caching of crush boxes (name); single use. |

#### Formations

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `FormationOrder` | units 103, containers 45, prop 1 (149) | int 1..8; 2 typical | Rank of the unit in a formation (name). Engine: accepted 1..1000, default 2. |
| `FormationSpacing` | units 105, containers 41, props 2 (148; 2 repeats) | .3..2.75; .5 typical | Spacing between formation members (name). Engine: accepted 0..100, default 0.5. |
| `FormationGrouping` | units 77, prop 1 (78; 2 repeats) | Standard 67, Solo 13 | Whether the unit forms up with others (name). |
| `FormationRaggedness` | units 40 (41; 1 repeat) | 0..2.5 | Random offset of positions in a formation (name). |
| `Formation_Priority` | ground companies 91, space 38, squadrons 26, others 34 (189; 3 repeats) | int 0..6; 1 typical | Precedence when formations are combined (name). |
| `FormationPriority` | space corvettes 5, unique 1 (6) | 3 | Alternate spelling of Formation_Priority (data). |
| `Formation_Prority` | ground company 1 (Rebel_Speeder_Wing) | 1 | Misspelling of Formation_Priority; probably ignored (name). |
| `Formation_Formup_Wait_Style` | infantry 2 (Plex soldiers) | WaitNone | How the unit waits for formation formup (name). |
| `WaitsForFormationFormup` | infantry 5, heroes 2 (7) | false | Does not wait for formup (name). GameConstants ShouldSkipLandFormup true, MaxLandFormationFormupFrames 240. |
| `Ignore_For_Reoptimization` | ground company 1 (Rebel_Artillery_Brigade) | MPTL_Spotter | Member type left out of formation reoptimization (name). |

#### Animation rates tied to movement

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Movement_Animation_Speed` | units 114, props 2 (116; 1 repeat) | .07..3.5; 0.8 typical | Playback rate of the move animation (name). GameConstants WalkAnimationCutoff .6. |
| `Walk_Animation_Speed` | units 25 | .269..1.0 | Rate of the walk (as opposed to run) clip (name). |
| `Rotation_Animation_Speed` | units 70 | .5..32; 20 typical | Rate of turn animations (name). |
| `Crouch_Animation_Speed` | infantry 9, generic heroes 5, others 3 (17) | .393..1.0; .537 typical | Rate of crouch clips (name). |
| `Tread_Scroll_Rate` | vehicles 3 (MPTL, T4-B, TIE crawler), prop 1 | .12/.167 | Texture scroll rate of tank treads (name). |
| `Cinematic_Anim_Speed` | infantry 2, props 2 | 1.0 | Off-topic: animation rate in cinematics (name). |

#### Off-topic name matches

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Turret_Rotate_Extent_Degrees` | units 61, structures 20, hardpoints 7, props 2 (90; 1 repeat) | 10..360 | Turret yaw arc, degrees (name). |
| `Turret_Elevate_Extent_Degrees` | same 90 (1 repeat) | 20..360 | Turret pitch arc, degrees (name). |
| `Turret_Rotate_Speed` | structures 26, units 15, hardpoints 7, props 2 (50) | .5..14 | Turret turn rate (name). |
| `Location_Follows_Terrain` | tactical cameras 3 | 0/1 | Camera follows terrain height (comment). |
| `Min_Height_Above_Terrain` | tactical camera 1 (Land_Mode) | 20 | Camera minimum height (name). |
| `MinHeightAboveTerrain` | tactical camera 1 (Unlocked) | 0 | Alternate spelling of the camera minimum height (data). |
| `Use_Splines` | tactical camera 1 (Land_Mode) | yes | Land camera uses splines for distance and pitch (comment). |
| `Particle_Attach_To_Collision` | particles 3 (shield absorb) | 1 | Particle attaches to the collision surface (name). |
| `Secondary_Objective` | secondary structures 9 | TEXT_* ids | Objective text of a secondary structure (data). |

#### Findings

- Alternate spellings of one setting: `OverrideAcceleration`/`OverrideDeceleration` (145 units) versus `Override_Acceleration`/`Override_Deceleration` (40 land team containers, never both on one object); `Formation_Priority` (189) versus `FormationPriority` (6 corvettes) and the misspelt `Formation_Prority` (1); `Min_Height_Above_Terrain` versus `MinHeightAboveTerrain` (cameras). The engine reads only `OverrideAcceleration`, so team-container values are dropped; whether the original honors the underscore form is unverified.
- GameConstants.xml multiplies every type's top speed per mode (`Object_Max_Speed_Multiplier_Land` 1.56, `_Space` 1.2, `_Galactic` 5.0). EAWEngine applies no such multiplier; its x30 scale is uncalibrated, so land speeds may be about 36% low relative to space if the multiplier holds.
- `MovementClass` values match MovementClassTypeDefs.xml. FoC adds `InfantryOnly` to Infantry/Flying/Space and `Impassable` (BombingRun), and adds Lancet, Spotter, LargeHover, BombingRun; EAW has 14 classes. Some vehicles use `Infantry` (6 of 33 GroundVehicle entries) and two use `Space`, so the class is about passability, not unit kind. Odd entries: Lancet lacks Obstacle, LargeHover lacks Steep. The 63 duplicate entries are identical copies on team containers.
- `Space_Layer` has two roles: the ship class layer in space and, on land, the air layers a ground object blocks (structures list "Frigate | Corvette" so speeders path around them). The engine uses it only as "Land/None = walks on terrain"; layer-based air collision is not modeled.
- Locomotors come from behavior tokens, not tags: WALK_LOCOMOTOR (131), SIMPLE_SPACE_LOCOMOTOR (61), LAND_TEAM_CONTAINER_LOCOMOTOR (41), LAND_TEAM_INFANTRY_LOCOMOTOR (39), FIGHTER_LOCOMOTOR (35), TEAM_LOCOMOTOR (8), FLYING_LOCOMOTOR (5), LANDBOMBER_LOCOMOTOR (3), JETPACK_LOCOMOTOR (3, via Secondary_Locomotor_Name), FLEET_LOCOMOTOR and BIKE_LOCOMOTOR (1 each). The engine keys only on FIGHTER_LOCOMOTOR; `Alternate_Max_Speed`/`Alternate_Max_Rate_Of_Turn` (jetpack) are unread.
- `Min_Speed` is set on 115 objects, but the engine uses it only for fighters; corvettes (Min_Speed 2.0) and ground units (.1) are ignored, meaning unknown.
- Space flight-model tags (`Max_Thrust`, `Max_Lift`, `Max_Rate_Of_Roll`, `Bank_Turn_Angle`, `Mass` near 1, `Begin_Turn_Towards_Distance`) appear together on all 134 flyers and are unread; the engine's banking and climbing are engine choices. `Mass` reads like a per-frame velocity retention factor (guess).
- The secondary-facing group (`UseSecondaryFacing` and five coefficient tags) is used by 8-9 hover vehicles and is unread; GameConstants `TurnInPlaceSlowdownCorvette/Frigate/Capital` (2/3/4) and `CloseEnoughAngleForMoveStart` 180 are global turn defaults the engine does not read (its 45 degree turn-in-place angle is its own).
- Footprints: the engine reads `Custom_Soft_Footprint_Radius` and `OverridePassability` plus `UnitCollisionClass` containing "obstacle"; it ignores the hard extents and offsets (227 objects), `Obstacle_Width/Height/X/Y_Offset` rectangles (41), `Obstacle_Proxy_Type` (77), `SecondaryOccupationPassability` (base shields), and space obstacle radius/offset. These are the next tags to read for structure and prop blocking.
- FoC-only tags: `Hover_Height`, `Movement_Directions_Count`, `Destroy_When_Stunned_Over_Water` (1-2 uses each). Single-use or constant tags likely of no practical effect: `Fine_Tune_Occupied_Passability` (always off), `OccupationStyle` (always 1x1), `Cache_Crusher_Boxes`, `Formation_Prority`.
- Value syntax: `1.2f` and `0.0f` suffixes (On_Fire_Speed_Modifier, Max_Lift), booleans as Yes/yes/true/True, lists separated by `|`. The engine's float parse accepts the leading number and ignores the suffix.

### Health, shields, armor, weapons, targeting, turrets, and death

This family covers what makes an object damageable and dangerous: hull health, shields, energy, and armor classes (resolved against the `Damage_To_Armor_Mod` table in `GameConstants.xml`); the `HardPoint` objects that space ships and stations (and a few land vehicles) list in `HardPoints`, each a destroyable, targetable part with its own weapon, fire bones, cone, and damage effects; the per-category accuracy and targeting priorities; turret bones and turret target priorities; and the death chain (explosions, death clones, wreck persistence, spin-away, break-off debris, survivors). Three patterns recur. Per-category tags repeat, with the category as the first item of the value (`Fire_Inaccuracy_Distance`, `Targeting_Fire_Inaccuracy`, `Death_Clone` per damage type). Booleans are written loosely (`Yes/yes/True/true`, and a few typos). Several global defaults and multipliers live in `GameConstants.xml`, not on the object. Counts are from the dump (base, FoC source, and installed data combined) unless stated otherwise.

#### Health, shields, armor, and energy

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Tactical_Health` | structures 299, props 233, units 220, projectile 1 (753 objects) | int 0..999999, mostly 100/500/200/1000; repeated in 3 objects (`Home_One` lists 8000 then 10000) | Hull hit points in tactical battles (name, data). `GameConstants` scales object and hardpoint max health by mode: `Object_Max_Health_Multiplier_Space` 1.5, `_Land` 1.0 (comment). Engine: read as hull health, and the multiplier is not applied; when a tag repeats, the last one wins. |
| `Shield_Points` | structures 159, units 145, props 23 (327) | int 0..20000; 0 in 197 | Shield capacity (name, data). Engine: read as the shield pool. |
| `Shield_Refresh_Rate` | units 126, structures 116, props 14 (256) | int 0..10000; 0 in 130 | Shield regeneration (name). `GameConstants` has `ShieldRechargeIntervalInSecs` 3.0, so the value may be points per 3-second interval rather than per second (data, unverified). Engine: read as points per second. |
| `Armor_Type` | structures 264, units 192, props 17 (473) | 43 distinct `Armor_*` words; Armor_Structure 174, Armor_Infantry 53 | Hull armor class, the second key of `Damage_To_Armor_Mod` (data). All 43 values are defined among the table's 53 armor/shield types. Engine: interned and used for hull damage. |
| `Shield_Armor_Type` | units 47, structures 20, props 5 (72) | 6 `Shield_*` words (Capital 26, Corvette 13, Fighter 13, Frigate 11, Tank 7, SSD 2); 1 empty | Armor class used while the shield absorbs damage (data: these names sit in the same damage table). Engine: shield damage uses this type, the rest goes to the hull through `Armor_Type` (an engine choice). |
| `Energy_Capacity` | units 132, structures 126, props 13 (271) | int 0..999999; 0 76, 1000 40 | Energy pool size (name). Related constants: `EnergyRechargeIntervalInSecs` 5.0, `EnergyToShieldExchangeRate` 5.0 (data). Not read by the engine. |
| `Energy_Refresh_Rate` | the same 271 objects | int 0..99999 | Energy regeneration per recharge interval (name). Not read. |
| `Autoresolve_Health` | containers 129, units 113, structures 80, props 7 (329) | int/float 5..25000 | Health used in galactic auto-resolve, not tactical (name; a comment says the values were changed to track real unit health). Not read. |
| `Immune_To_Damage` | GroundBuildable 40, props 5 (45) | always True | Takes no damage (name); used on build pads and markers. Not read. |
| `GUI_Hide_Health_Bar` | GroundStructure 13, prop 1 (14) | bool (true 12, false 3); repeated once | Hides the health bar (name); walls. Not read. |
| `Out_Of_Combat_Defense_Adjustment` | SpaceUnit 18, UniqueUnit 1 (19) | always -1.0 | Unknown defense modifier outside combat (name only); fighters and freighters. Not read. |
| `Defend_Mode_Shield_Regen_Multiplier` | 2 (Hutt_Refurbished_Venator, Jedi_Cruiser) | 5.0 | Shield-regen multiplier while the defend ability is active (name). |
| `Defend_Mode_Energy_Regen_Multiplier` | the same 2 | 5.0 | Energy-regen multiplier in defend mode (name). |
| `Defend_Mode_Weapon_Delay_Multiplier` | the same 2 | 3.0 | Weapon delay multiplier in defend mode (fires more slowly) (name). |
| `Surface_Type_Cover_Damage_Shield` | GroundInfantry 14, GroundVehicle 2 (16) | .5 (12), .25 (4) | Damage fraction blocked by cover terrain (name only). Not read. |
| `Is_Combustible` | Indigenous 22, Slave 7, Infantry 4 (33) | always yes | Can be set on fire (name). Always appears together with the next two tags. |
| `Burning_Damage_Per_Second` | the same 33 | 0.1 (32), 0.2 | Damage while burning (name); the small values suggest a fraction of health per second (data, unverified). |
| `Targeting_Allowed_When_Burning` | the same 33 | always no | A burning unit does not attack (name). |

#### Ground base shields and shield presentation

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Base_Shield_Radius` | SpecialStructure 7, GroundStructure 1 (8) | 200, 370, 740 | Radius of a ground base shield dome (name, data). Related constants: `Base_Shield_Speed_Modifier` 0.5, `Base_Shield_Vulnerability_Modifier` 2.0, and `Base_Shield_Delay_Time` 10 (written `0.5f`/`2.0f` with a C float suffix). |
| `Base_Shield_Always_Off` | structures 13 | bool (True 9, False 4) | The structure's base shield starts or stays off (name); build pads and the small shields. |
| `Base_Shield_Penetration_Particle` | units 34 (GroundVehicle 27...) | always `Shield_Penetration_Effect_Particle` | Effect played when this unit passes through an enemy base shield (name, data); set on vehicles, walkers, and large creatures. |
| `Shield_Normal_Color` | structures 4 | RGBA 0.2,0.2,0.8,1.0 | Base-shield tint (name). |
| `Shield_Upgraded_Color` | SpecialStructure 3 | RGBA 0.2,0.7,0.8,1.0 | Tint after a shield upgrade (name). |
| `Shield_On_Anim` | structures 4 | `SHIELD_ON` | Animation when the shield comes up (name). |
| `Shield_Off_Anim` | structures 4 | `SHIELD_OFF` | Animation when the shield goes down (name). |
| `Shield_Requires_Base_Power` | 1 (Rebel_Mobile_Shield_Generator) | no | The shield works without base power (name). FoC only. |
| `Shield_Sub_Mesh` | 1 (Destroyer_Droid) | `Sphere01` | Mesh drawn as the unit's personal shield (name, data). FoC only. |
| `Shield_Hit_Particles` | LandBombingUnit 3 | `Projectile_Shield_Absorb_Small` | Effect when a shot hits the shield of a bombing-run craft (name). |
| `Damage_Hit_Particles` | LandBombingUnit 3 | bombing-run explosion particles | Effect when the craft's attack hits (name; unclear whether this is the bomb impact or damage taken). |

#### Hardpoint lists and hardpoint parts

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `HardPoints` | StarBase 34, SpaceUnit 29, UniqueUnit 13, GroundVehicle 7, others (88) | list of `HardPoint` names (834 references); spelled `Hardpoints` in 4 objects; 1 empty | The parts mounted on the object (data). Comments mark some lists as a replacement for an inherited list. Engine: each entry becomes a part (kind from the hardpoint's `Type`); weapons become hardpoint weapons, and models attach from `Model_To_Attach`. Field lookup ignores case, so `Hardpoints` is read too. |
| `Health` | HardPoint 259 | float 50..10000; 450 53, 1000 48 | The part's hit points (data); `GameConstants` scales them like hull health (comment) and `Hull_Vs_Hard_Points_Health_Constraint` 0.2 ties hull and part health together (comment). Engine: part health, 0 (indestructible) when `Is_Destroyable` is No; the multiplier is not applied. |
| `Is_Destroyable` | HardPoint 355 | Yes 258, No 97 | The part can be destroyed (name). Engine: read, default Yes. |
| `Is_Targetable` | HardPoint 355 | Yes 258, No 97 | The player and AI can target the part (name). Engine: read, default Yes; shots aim at the nearest live targetable part. |
| `Damage_Particles` | HardPoint 260 | bone names, mostly `HP_*_EMITDAMAGE`, `HP_E_EMITDAMAGE` 18 | Bone where damage fire and smoke play once the part is damaged (data). Engine: resolved to a bone; particle proxies at or below it are held back as damage effects. |
| `Damage_Decal` | HardPoint 255 | bone names `HP_*_BLAST`; 18 empty | Bone of the scorch mesh shown when the part is destroyed (data). Engine: resolved to a bone and hidden until hardpoint damage is presented. |
| `Death_Explosion_Particles` | HardPoint 259 | 6 explosion particles (Large_Explosion_Space_Empire 134, Large_Explosion_Space 98) | Explosion when the part is destroyed (name, data). Not read. |
| `Death_Breakoff_Prop` | HardPoint 167 | `Hardpoint_Breakoff_*` SpaceProps (147 distinct) | Debris prop spawned when the part is destroyed (data). A comment says break-off props are left out on land because the debris behavior has no gravity. Not read. |
| `Repair_Amount_Per_Frame` | HardPoint 87 | .50 46, 2.0 23, .53 18 | Health restored per frame while repairing (name). Not read. |
| `Repair_Cost_Per_Frame` | HardPoint 87 | 1.0..1.8 | Credit cost per frame of repair (name). Not read. |
| `Should_Be_Destroyed_When_All_Hardpoints_Destroyed` | 1 (U_Ground_Palace) | No | Whether the host dies once all its parts are gone (name); the default is presumably Yes (inference). FoC only. |
| `Shield_Generator_Hardpoint` | 1 (CIN_HP_Star_Destroyer_Shield_Generator) | Yes | Marks a cinematic hardpoint as a shield generator (name). Installed data only (`CIN_HARDPOINTS.XML`); not in either source tree. |
| `Weapon_Hardpoint` | HardPoint 6 (CIN_HP_Star_Destroyer_Weapon_*) | Yes | Marks cinematic weapon hardpoints (name). Installed data only. |

#### Hardpoint weapons

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Fire_Projectile_Type` | HardPoint 279 | 40 `Proj_*` projectiles | Projectile the hardpoint fires (data). Engine: a weapon hardpoint without a defined projectile is skipped with a warning. |
| `Fire_Range_Distance` | HardPoint 279 | 150..20000; 2200 72, 2000 43 | Weapon range (name). Engine: the hardpoint weapon's range, falling back to the host's `Targeting_Max_Attack_Distance`; measured in the ground plane (engine choice). |
| `Fire_Min_Recharge_Seconds` | HardPoint 279 | 0..18; 3.0 146 | Lower bound of the delay between bursts (name). Engine: uses the midpoint of min and max (deterministic stand-in for a random draw). |
| `Fire_Max_Recharge_Seconds` | HardPoint 279 | 0.2..18; 4.0 157 | Upper bound of the delay between bursts (name). Engine: as above. |
| `Fire_Pulse_Count` | HardPoint 279 | 1..10 | Shots per burst (name). Engine: read, clamped to at least 1. |
| `Fire_Pulse_Delay_Seconds` | HardPoint 279 | 0.1..1.0; 0.2 208 | Delay between shots of a burst (name). Engine: read. |
| `Fire_Bone_A` | HardPoint 280 | muzzle/fire-point bones (`MuzzleA_00`, `FP_F-R_00`...) | First fire bone (data). A comment states that only bones A and B exist, with no C or D. Engine: the muzzle position, and the cone's center is this bone's X axis. |
| `Fire_Bone_B` | HardPoint 279 | bones; 7 empty; repeated 3 times in 2 station hardpoints | Second fire bone, presumably alternated with A (inference). The two repeating hardpoints list B three times, apparently where C and D were intended; probably only one of them counts (inference). Not read. |
| `Fire_Cone_Width` | HardPoint 279, TransportUnit 4 | 1..364 degrees; 180, 160, 170 most common | Horizontal firing arc in degrees (data; a comment reads a value as a half-angle on each side). Engine: half the value about the fire bone's X axis; 360 or more means no cone. |
| `Fire_Cone_Height` | HardPoint 279, TransportUnit 4 | 3..450 degrees | Vertical firing arc (name). 450 appears once, outside any sensible angle. Not read. |
| `Fire_Inaccuracy_Distance` | HardPoint 227, units 30, structures 7, prop 1 (265) | "category, distance" pairs, repeated per category (1761 uses, up to 14 per object): space categories Fighter/Bomber/Transport/Corvette/Frigate/Capital/Super about 244 each, ground Infantry/Vehicle/Air/Structure 21..28 | How far shots miss, by target category (name, data: small craft get 70, capital ships and supers 1..30). `Millennium_Falcon` lists one category twice. The dump marks the category words as Priority_Set references because the names coincide; they are `GameObjectCategoryType` values. Not read. |
| `Fire_Category_Restrictions` | HardPoint 62, units 26 (88) | category lists (Air 51, Transport 42...); 8 empty; repeated in 3 hardpoints | Categories the weapon will not fire at (data: torpedo hardpoints exclude Fighter, Bomber, Transport; melee creatures exclude Air and Structure). Not read. |
| `Randomize_Between_Fire_Bones` | HardPoint 9 | always No | Fire bones are used in order rather than at random (name). FoC only. |
| `Full_Salvo_Weapon_Delay_Multiplier` | 1 (HP_Interceptor_Frigate_00) | .18 | Recharge multiplier while the full-salvo ability is active (name). FoC only. |
| `Requires_Manual_Target_Assignment` | 1 (HP_Underworld_L3_Station_Main_Cannon) | Yes | Fires only at a target the player assigns, used like a special ability; it fires one burst per assignment (comment). FoC only. |
| `Manual_Hardpoint_Firing_Cooldown_Secs` | the same hardpoint | 120 | Cooldown before the player may fire it again (comment). FoC only. |
| `Allow_Opportunity_Fire_When_Targeting` | HardPoint 57 | False 45, True 12 | May shoot other targets while it has an assigned target (name; comments use False for megaweapons that fire only at directed targets). Related constant: `Hardpoint_Recharge_Cutoff_For_Opportunity_Fire` 3.0. Not read. |
| `Allow_Opportunity_Fire_When_Idle` | HardPoint 11 | False 7, True 4 | May shoot targets of opportunity when idle (name); bombers and torpedo mounts. Not read. |

#### Mode-dependent firing

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Fire_When_In_Normal_Attack_Mode` | HardPoint 8 | No 6, Yes 2 | Hardpoint fires in the host's normal mode (name). Paired with the next tag on T4B and bomber hardpoints. |
| `Fire_When_In_Rocket_Attack_Mode` | HardPoint 8 | Yes 6, No 2 | Hardpoint fires when the host is in rocket-attack mode (name, data: the T4B's rocket hardpoints). |
| `Fire_Weapon_When_In_Normal_Attack_Mode` | GroundVehicle 2 (T4B_Tank, Destroyer_Droid) | Yes | The unit's own weapon fires in normal mode (name). |
| `Fire_Weapon_When_In_Rocket_Attack_Mode` | the same 2 | No | The unit's own weapon is silent in rocket mode (name). |
| `Fire_When_In_Power_To_Weapons_Mode` | HardPoint 6 (HP_Thrawn_SD_*) | Yes | Hardpoint fires while power-to-weapons is active (name). FoC only. |
| `Power_To_Weapons_Mode_Weapon_Delay_Multiplier` | the same 6 | .5 | Recharge multiplier in power-to-weapons mode (name). FoC only. |
| `Should_Attacker_Hold_Fire_For_Special_Ability` | 1 (Tantive_IV) | True | Presumably attackers hold fire so a special ability (capture or disable) can work (name only). |

#### Targeting and accuracy

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Targeting_Max_Attack_Distance` | units 184, containers 45, structures 40, props 7 (276) | 5..20000; repeated in 7 objects with different values (Corellian_Corvette 2000 then 800) | The object's attack range (name). A comment says it must be shorter than the ranges of its hardpoints. Engine: the range of the type's own weapon and the fallback range of hardpoint weapons; the last repeat wins. |
| `Targeting_Min_Attack_Distance` | units 10, structures 4 (14) | 2..220 | Minimum range, so a unit can give up a target that gets too close to turn onto (comment). Not read. |
| `Ranged_Target_Z_Adjust` | units 155, structures 143, props 12 (310) | -5..70; repeated in 2 | Height above the origin that shooters aim at (name, data). Engine: aim height (a negative value is allowed). |
| `Target_Bones` | 4 (AT_AT_Walker, Nebulon_B_Frigate, Veers AT-AT, Eclipse) | bone lists (`MISSILE_TARGET00`...) | Bones that incoming shots, apparently missiles, aim at (name, data). Not read. |
| `Targeting_Priority_Set` | units 165, structures 15, props 4 (184) | 31 set names (AntiInfantry 69...); repeated in 2 | Names a `Priority_Set` from Turret/Space/GroundUnitTargetingPriorities.xml (47 sets, all values resolve). A set lists category priorities, exclusions, and (in space) the order of hardpoint types to attack (data). Not read. |
| `Targeting_Fire_Inaccuracy` | units 87, structures 17, prop 1 (105) | "category, value" pairs repeated per category (424 uses, up to 5): Air/Infantry/Vehicle/Structure about 107 each, LandHero 3, Corvette 1 | Ground counterpart of `Fire_Inaccuracy_Distance`: miss distance by target category (name, data: Air 70 is the most common). The dump attaches "9999 means will not target" comments here, but they belong to nearby turret-priority tags. Not read. |
| `Targeting_Fire_Inaccuracy_Fixed_Radius` | 5 (MPTL, MAL, Broadside, Marauder, a defense satellite) | 40..220 | A fixed scatter radius for artillery-like weapons (comment, data). Not read. |
| `Targeting_Stickiness_Time_Threshold` | units 113, structures 9, props 3 (125) | 3..30; 5.0 in 113 | Seconds a unit stays on its current target before switching (name only). Not read. |
| `Targeting_Land_Model_Stay_Horiz_Flat` | 1 (TIE_Crawler) | No | Whether a land model keeps itself level instead of tilting with the terrain while it targets (name; a nearby comment says this does not apply to turreted units). Not read. |
| `Target_Stealth_Units` | units 14, structures 3 (17) | always Yes | Can see and target stealthed units (name). FoC only. |
| `Is_Valid_Target` | units 39, props 30, structure 1 (70) | No 56, False 11, Yes 3, false 1; repeated once (Sarlacc) | Whether the object can be targeted at all (name); used on ambient creatures, markers, transports. Not read. |
| `Collidable_By_Projectile_Living` | structures 253, units 197, props 32, projectile 1 (483) | Yes 429, No 57; repeated in 3 | Projectiles collide with the object while it is alive (name). Not read. |
| `Collidable_By_Projectile_Dead` | units 30, props 15, projectile 1 (46) | No 25, Yes 23; repeated in 2 | Projectiles collide with the wreck or corpse (name). Not read. |
| `Attack_Category_Restrictions` | GroundStructure 5, GroundVehicle 1 (6) | categories joined with `\|`; repeated twice on the abstract turrets | Target categories the object may not attack (data: the AA turret excludes Infantry, Vehicle, and LandHero, the anti-infantry turret excludes Air). Uses `\|` where `Fire_Category_Restrictions` uses commas. Not read. |
| `Attack_Animation_Is_Overlay` | GroundVehicle 6, UniqueUnit 1 (7) | yes | The attack animation is layered over movement (name); walkers and droids. Not read. |

#### Turrets

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Turret_Bone_Name` | units 17, structures 15, HardPoint 7, props 2 (41) | bones (`B_Turret` 13, `Turret_00` 10, `b_turret` 5...) | Bone that yaws toward the target (name, data). Not read. |
| `Barrel_Bone_Name` | units 14, structures 11, HardPoint 7, prop 1 (33) | bones (`B_Barrel`, `Barrel_00`, `MuzzleA_00`...) | Bone that pitches toward the target (name, data). Not read. |
| `Apply_Y_Turret_Rotate_To_Axis` | structures 17, units 14, props 2 (33) | 1 (31), 0 (2) | Which local axis the turret's yaw drives (name, data: an axis index). Not read. |
| `Apply_Z_Turret_Rotate_To_Axis` | the same 33 | always 2 | Which local axis the barrel's pitch drives (name, data). Not read. |
| `Turret_Rest_Angle` | HardPoint 7, structures 5, unit 1, prop 1 (14) | vector3, all 0,0,0 except one 0,-45,0 | Orientation the turret returns to when idle (name). Not read. |
| `Turret_Rotation_Offset` | HardPoint 7 | always 0,0,0 | Rotation offset of a hardpoint turret (name). FoC only. |
| `Is_Turret` | HardPoint 7 (Gargantuan turrets, L3 station main cannon) | Yes | The hardpoint aims as a turret (name). FoC only. |
| `Turret_XY_Only` | 1 (Sarlacc) | Yes | Turret yaws only, without pitch (name). |
| `Turret_Targets_Ground_Vehicles` | 18 (GroundVehicle 13...) | int priority 1..9999 | The turret's priority for ground vehicles; 1 is highest and 9999 means never (comment). Not read. |
| `Turret_Targets_Ground_Infantry` | the same 18 | 1..9999 | Priority for infantry (comment). |
| `Turret_Targets_Air_Vehicles` | the same 18 | 1..9999 (9999 in 7) | Priority for air units (comment). |
| `Turret_Targets_Anything_Else` | the same 18 | 1..9999 | Priority for everything else (comment). |
| `Ground_Vehicle_Turret_Target` | units 24, prop 1 (25) | Yes 24, No 1 | Puts this object in the "ground vehicle" class of the turret priorities above (data). |
| `Ground_Infantry_Turret_Target` | units 84, prop 1 (85) | always Yes | Puts the object in the "infantry" turret class (data). |
| `Air_Vehicle_Turret_Target` | units 22 | Yes 13, No 9 | Puts the object in the "air" turret class (data); No on birds. |
| `Fires_Forward` | SpaceUnit 16, UniqueUnit 6, LandBombingUnit 3, GroundVehicle 1 (26) | no 25, Yes 1 | Yes lets missiles fire without regard to the firing cone when the projectile's facing is computed (comment). Not read. |

#### Engagement ranges and escorts

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Guard_Chase_Range` | units 145, containers 73, props 7 (225) | 0..1500; 200 112, 1000 64 | How far a guarding unit chases (name). Not read. |
| `Idle_Chase_Range` | the same 225 | -1..1500; 50 66, 0 44 | How far an idle unit chases an attacker (name; comments keep it small for units with no tether). One value is -1. Not read. |
| `Attack_Move_Response_Range` | units 141, containers 73, props 7 (221) | 0..1500; repeated in 3 | Range at which a unit on attack-move responds to enemies (name). Not read. |
| `Harass_Enemy_Exclusion_Range` | 2 (Sand_Person_Sniper_A, Imperial_Elite_Guard) | 100 | Distance a harassing unit keeps from enemies (name only). |
| `Is_Escort` | containers 129, units 60, props 2 (191) | yes/no, plus typos `Nos` 4 and `np` 1 | The unit escorts and guards others, used for AI and formations (name). Not read. |
| `Space_Escort_Unit_Types` | 2 (Death_Star, Death_Star_II) | lists of SpaceUnit and Squadron names | Escorts that come with the super weapon (name, data). Not read. |

#### Death, death clones, and wrecks

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Death_Explosions` | structures 229, props 207, units 123, projectiles 11 (570) | 26 names: 598 references to Particles, 12 to Projectiles; 3 empty; repeated in 43 objects | Effect when the object dies (name, data). In repeating structures the second value differs (E_Ground_Barracks: Huge, then Large). Comments mark entries for death by tactical super weapon and for the barrage ability. Engine: the first list item of the last occurrence; a Projectile name plays that projectile's detonation particle (`scene_particles.cpp`). |
| `Death_Explosions_End` | GroundVehicle 16, UniqueUnit 4 (20) | Large/Huge/Small_Explosion_Land | Explosion when a death clone's wreck ends (name, data: on the walker death clones). Not read. |
| `Death_Clone` | units 114, structures 26, props 3 (143) | "damage type, clone type" pairs repeated per damage type (338 uses, up to 7): Damage_Normal 98, Disruptor_Hand 84, Infection 42, Force_Whirlwind 40, Force_Lightning 40, Crush 21, Remote_Bomb 8, Fire 5, others; 2 empty | Object that replaces the unit when it dies from that damage type: a variant with its own death animation, persistence, and effects (data). Most of these damage types (Normal, Fire, Crush, Force_*, Infection) are not keys of `Damage_To_Armor_Mod`. Engine: the Damage_Normal clone (else the first) gives the DIE variant and persistence; clones are hidden from the spawn list. |
| `Death_Clone_Is_Obstacle` | structures 63, units 19, prop 1 (83) | true/yes/True 77, false/no 7; repeated once | The death clone or rubble blocks movement (name). Mostly set on structures, not on clone objects. Not read. |
| `IsDeathClone` | GroundVehicle 4, UniqueUnit 4 (8) | true | Marks an object as a death clone (name). Engine: does not read it; it detects clones by being named in a `Death_Clone`, or by having `Specific_Death_Anim_Index` or `Death_Persistence_Duration`. |
| `IsDeathCloneObstacle` | units 15, prop 1, projectile 1 (17) | true 15, Yes 2 | Apparently a second spelling of `Death_Clone_Is_Obstacle`; both appear on the same AT-AT clones (data). Which one the game reads is unknown. |
| `Death_Persistence_Duration` | units 113, structures 17, props 2 (132) | -1..40 seconds; 0.0 84, 34 15 | How long the corpse or wreck stays (data). `GameConstants` has `Override_Death_Persistence_Duration` 30. Engine: read for death clones; it ignores values of 0 or below, including the one -1 (on an indigenous creature, possibly meaning forever). |
| `Death_Fade_Time` | units 94, props 2 (96) | 0.05..5; 0.25 in 88 | Fade-out time of the corpse (name). Not read. |
| `Remove_Upon_Death` | props 164, units 97, structures 24, projectile 1 (286) | true 281, false 4, Yes/yes 4; repeated in 3 | The object is deleted at death without leaving its model or wreck (name, data: debris, fighters, satellites). Not read. |
| `Death_Leave_Hulk_Behind` | SpaceUnit 7 | always No | No hulk remains after death (name); set only on frigates. Not read. |
| `Death_Projectiles` | SpaceStructure 3, prop 1 (4) | Proj_*_Detonation | Projectile spawned at death, an area-damage blast (name, data). Not read. |
| `Retreat_Self_Destruct_Explosion` | StarBase 15, prop 1 (16) | `Self_Destruct_Star_Base_Level_*` projectiles | Blast when a star base self-destructs on retreat (name, data). Not read. |
| `Spin_Away_On_Death` | units 22 (SpaceUnit 14...) | always Yes | The craft may spin away out of control instead of exploding at once (name). Not read. |
| `Spin_Away_On_Death_Chance` | the same 22 | 0.2, 0.4 | Probability of spinning away (name). |
| `Spin_Away_On_Death_Time` | the same 22 | `2.0f`, `1.0f` (C float suffix) | Duration of the spin before the final explosion (name). |
| `Spin_Away_On_Death_Explosion` | the same 22 | Small_Explosion_Space(_Empire), Medium_Explosion_Land | Explosion at the end of the spin (name). |
| `Damaged_Smoke_Asset_Name` | GroundVehicle 4, LandBombingUnit 3 (7) | `P_Smoke_Small02.alo` | Smoke model attached when the craft is damaged (name). Not read. |
| `Destruction_Survivors` | structures 32, units 6 (38) | "type, weight/chance" lines; 5 empty; repeated once | Units spawned when the object is destroyed, such as crews and prisoners (data). Not read. |
| `Minimum_Destruction_Survivor_Count` | 1 (Secondary_Prison_E) | 4 | Lower bound of survivors spawned (name). |
| `Maximum_Destruction_Survivor_Count` | 1 (Secondary_Prison_E) | 8 | Upper bound of survivors spawned (name). |
| `Death_By_TSW_Replacements` | SpaceProp 37, Props_Generic 1 (38) | always `Destroyed_Planet_Remains_Part_1, _Part_2` | Props that replace a planet backdrop destroyed by the tactical super weapon (Death Star) (comment, data). Not read. |

#### Break-off debris (on the props `Death_Breakoff_Prop` names)

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Debris_Attached_Particle` | SpaceProp 149 | 5 `Space_Debris_Fire_*` particles | Fire effect attached to the flying debris (name). Not read. |
| `Debris_Facing_Rotate_Vector` | SpaceProp 149 | vector3, components 0.1..1.0 | Tumble rotation of the debris (name). Not read. |
| `Debris_Min_Lifetime_Seconds` | SpaceProp 149 | 15 (139), 1 (10) | Lower bound of the debris lifetime (name). Not read. |
| `Debris_Max_Lifetime_Seconds` | SpaceProp 149 | 25 (139), 1 (10) | Upper bound of the debris lifetime (name). Not read. |

#### Other single-purpose tags

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Combat_Distance` | Hero_Clash 1 (Vader_ObiWan) | 17.65 | Distance between the two duelists in a hero clash (name). |
| `Damage_Percentage` | Hero_Clash 1 | 0.15 | Damage fraction per clash exchange (name only). |
| `Auto_Destruct_Time` | LightSource 1 | 0.4 | The light removes itself after this time (comment). |
| `Auto_Destruct_Fade_Time` | LightSource 1 | 0.4 | Fade time of that self-removal (comment). |
| `Weapon_Type` | 1 (Ground_Ion_Cannon) | `Ion_Cannon_Planetary` | The planetary ion cannon's weapon (name). |
| `Weapon_Quantity` | 1 (Ground_Ion_Cannon) | 1 | Number of those weapons (name). |

#### Findings

- **The space health multiplier is missing.** `GameConstants` multiplies object and hardpoint max health by `Object_Max_Health_Multiplier_Space` 1.5 (land 1.0), per its comment. The engine reads `Tactical_Health` and hardpoint `Health` raw, so space ships and stations have two thirds of their intended health. This is the largest content-contract gap here.
- **Shield regeneration may be read 3x too fast.** The engine reads `Shield_Refresh_Rate` as points per second, but `ShieldRechargeIntervalInSecs` 3.0 suggests points per 3-second tick (and energy uses a 5-second interval). Unverified either way; a black-box check would settle it. `Depleted_Shield_Disable_Time` 5, `Depleted_Shield_Regen_Cap` 0.25, and `EnergyToShieldExchangeRate` 5 are further shield rules the engine does not model.
- **The damage table covers all stock armor.** It holds 2426 `Damage_To_Armor_Mod` entries: 81 damage types against 53 armor and shield types. Every stock `Armor_Type` (43) and `Shield_Armor_Type` (6) value is defined in it. `Death_Clone` damage types are mostly a separate vocabulary (Damage_Normal, Damage_Fire, Damage_Crush, Damage_Force_*, Damage_Infection, Damage_Cable_Attack, Damage_Normal_Deployed), so the engine must not expect them in the table.
- **Repeated scalar tags conflict, and the engine keeps the last.** `Targeting_Max_Attack_Distance` (7 objects, for example Corellian_Corvette 2000 then 800), `Tactical_Health` (Home_One 8000 then 10000), and `Death_Explosions` (43 structures, two different explosions). Which occurrence the original uses is not established; for Death_Explosions, both may play or one may serve a different death cause (comments mention death by tactical super weapon).
- **Category lists are written in two styles.** `Attack_Category_Restrictions` joins with `|`, `Fire_Category_Restrictions` with commas. Both appear to be exclusion lists (torpedo mounts exclude Fighter, Bomber, Transport; the AA turret excludes ground), so a reader must accept both separators and must not treat the lists as allow-lists.
- **Per-category tags repeat as category-value pairs.** `Fire_Inaccuracy_Distance` (space categories on hardpoints, up to 14 per object) and `Targeting_Fire_Inaccuracy` (ground categories) hold one pair per category. The category words come from `GameObjectCategoryType`, not priority-set names. Millennium_Falcon lists one category twice. Neither tag is read yet, so the engine's hits are perfectly accurate.
- **Spellings are inconsistent.** `Hardpoints` appears in 4 objects (read, since lookup ignores case). `IsDeathCloneObstacle` and `Death_Clone_Is_Obstacle` coexist, even on the same AT-AT clones, and `IsDeathClone` is a third marker the engine ignores. `Is_Escort` has the typos `Nos` and `np`, whose parsed meaning in the original is unknown. Float values carry C suffixes (`2.0f` in `Spin_Away_On_Death_Time`, `0.5f` in base-shield constants), so a strict float parser rejects them.
- **Fire bones and cones have quirks.** `Fire_Bone_B` repeats three times in two station hardpoints where C and D were meant, and a developer comment says only A and B exist. The engine reads only `Fire_Bone_A`. Supporting A/B alternation (and `Randomize_Between_Fire_Bones`, FoC) is a next step. `Fire_Cone_Width` reaches 364 and `Fire_Cone_Height` 450: the engine already treats 360 or more as no cone, and should treat Height the same way when it reads it.
- **`Death_Explosions` can name a projectile.** 12 entries name projectiles rather than particles (for example for the barrage ability). EAWEngine already resolves these by object kind and plays the projectile's detonation particle (`scene_particles.cpp`); the area blast of such death projectiles is not simulated yet.
- **Death persistence has extra values.** `Death_Persistence_Duration` -1 appears once, possibly meaning "forever"; the engine ignores values of 0 or below. The global `Override_Death_Persistence_Duration` 30 is unread and its role (cap or default) is unknown.
- **Some tags exist only in one version.** FoC only: `Fire_When_In_Power_To_Weapons_Mode`, `Power_To_Weapons_Mode_Weapon_Delay_Multiplier`, `Full_Salvo_Weapon_Delay_Multiplier`, `Requires_Manual_Target_Assignment`, `Manual_Hardpoint_Firing_Cooldown_Secs`, `Is_Turret`, `Turret_Rotation_Offset`, `Randomize_Between_Fire_Bones`, `Target_Stealth_Units`, `Shield_Requires_Base_Power`, `Shield_Sub_Mesh`, `Should_Be_Destroyed_When_All_Hardpoints_Destroyed`. Installed (patch) data only: `Shield_Generator_Hardpoint` and `Weapon_Hardpoint`, both in `CIN_HARDPOINTS.XML`, which looks cinematic-only.
- **Tags that are probably dead or one-offs.** `Out_Of_Combat_Defense_Adjustment` (always -1), `Turret_Rotation_Offset` and most `Turret_Rest_Angle` values (all zero), `Targeting_Land_Model_Stay_Horiz_Flat` (one use, value No), and `Weapon_Type` and `Weapon_Quantity` (one object each).
- **Hardpoint presentation is the next reading step.** The engine reads part health, destroyability, targetability, and the damage and decal bones. Next are `Death_Explosion_Particles`, `Death_Breakoff_Prop` with its `Debris_*` props (space only, per a developer comment), `Repair_*`, and `Hull_Vs_Hard_Points_Health_Constraint` 0.2, which is relevant to the engine's current choice to damage hull and part together.
- **Targeting is a self-contained next slice.** `Targeting_Priority_Set` resolves against 47 sets in three files with no dangling names. Those sets carry category priorities, exclusions, and space `Hard_Point_Priorities`. Together with the `Turret_Targets_*` priorities (9999 means never) and the `*_Turret_Target` class flags on targets, they would replace the engine's nearest-hostile choice.
- **Out of this fragment.** The hardpoint `Type`, `Model_To_Attach`, `Attachment_Bone`, `Damage_Type`, `Fire_SFXEvent`, and `Engine_Particles` tags are not in this family's dump. The engine reads `Type`, `Attachment_Bone`, `Damage_Type`, `Fire_SFXEvent`, and `Engine_Particles` in `combat_content.cpp`, and `Model_To_Attach` in `scene_models.cpp`.

### Projectiles and damage delivery

These tags describe what a weapon shot is and what it does: they live mostly on `Projectile` objects in Projectiles.xml (about 173 in the installed FoC data), with the firing cadence (`Projectile_Types`, `Projectile_Fire_*`) on the shooting unit, structure, or prop. A projectile carries its damage and `Damage_Type` (resolved against the `Damage_To_Armor_Mod` table in GameConstants.xml), its flight model (speed comes from the generic `Max_Speed`, not from this family), optional area blast, an optional detonation side effect (stun, ion stun, heal, invulnerability, convert, weaken, combat modifier, engine disable), a custom bolt look, and up to five detonation particles chosen by what it hit. Many side-effect families are used by a single hero-ability projectile, and many projectiles inherit through `Variant_Of_Existing_Type`, so the counts below are explicit uses only, not effective values.

#### Firing cadence (on the shooter)
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Projectile_Types` | units 143, structures 31, props 3 (177) | projectile name; 3 lists of two (Bossk, Disruptor merc, MAL rocket launcher); 2 empty | Projectile type(s) the object fires from its own weapon (data). engine: reads the first entry only, needs `Targeting_Max_Attack_Distance` > 0 |
| `Projectile_Fire_Recharge_Seconds` | units 143, structures 25, props 17, projectiles 6 (191) | seconds 0..45, typically 1.5..2; one object repeats it | Time between volleys (data); a comment warns it must match the unit ability's recharge or the original asserts. engine: reads from the shooter only |
| `Projectile_Fire_Pulse_Count` | units 115, structures 24, projectiles 6, props 2 (147) | int 1..14, mostly 1, 2, 4, 8 | Shots per volley (data). engine: reads from the shooter, clamped to 1..1000 |
| `Projectile_Fire_Pulse_Delay_Seconds` | same 147 objects | seconds 0.02..1.0, mostly 0.2 | Delay between shots of a volley (data). engine: reads from the shooter |
| `Projectile_Appearance_Delay_Frames` | units 27, hardpoints 2, props 1 (30) | frames 6..56; 4 uses are two numbers (`2,1`, `15,20`) | Delay from fire start until the projectile appears, to sync with the fire animation (name, data); the two-number form is unexplained (unknown: min/max or per-barrel). Not read |

#### Damage and damage type
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Projectile_Damage` | projectiles 152, units 15, hardpoints 10, structures 3, props 1 (181) | float 0..30000; 38 are 0 (effect-only shots) | Damage per hit (data). On units/hardpoints it differs from the fired projectile's value (Rebel_Trooper 3.0 vs its blaster's 2.0), so it is probably a per-shooter override (data, unverified). engine: reads from the projectile only |
| `Damage_Type` | projectiles 127, hardpoints 119, units 18, structures 2 (266) | name from the `<Damage_Types>` list, 82 distinct | Damage class looked up against the target's armor in `Damage_To_Armor_Mod` (data). engine: reads from the projectile; a hardpoint's value overrides it; the unit-level value is not read; names absent from the modifier table multiply by 1 |
| `Projectile_Does_Hitpoint_Damage` | projectiles 123 | bool, 105 Yes / 18 No | Whether the hit reduces hull health (comment, data). engine: reads, default true |
| `Projectile_Does_Shield_Damage` | projectiles 124 | bool, 81 Yes / 43 No | Whether the hit reduces shields (comment, data). engine: reads, default true |
| `Projectile_Does_Energy_Damage` | projectiles 123 | bool, 114 No / 9 Yes | Whether the hit drains the target's energy/shields (comment says it knocks shields down). Not read |
| `Projectile_Energy_Per_Shot` | projectiles 126 | 0..500, mostly 0; 5/10/20 on ship lasers | Energy the shooter's pool spends per shot (name, data; GameConstants has `EnergyRechargeIntervalInSecs` 5 and `EnergyToShieldExchangeRate` 5). Not read |
| `Projectile_Damages_Random_Hard_Points` | projectiles 1 (Proj_Hypervelocity_Gun) | yes | Hit lands on random hardpoints rather than the aimed one (name). Not read |
| `Friendly_Damage_Amount` | projectiles 1 (Proj_Remote_Bomb) | 200 | Damage dealt to friendly units (name). FoC only. Not read |
| `Projectile_Damage_Delay_Secs` | projectiles 3 | 0.5, 0.7 | Delay between detonation and damage application (name, data: barrel, construction pod, concussion blast). Not read |
| `Max_Secs_For_AE_Delayed_Damage` | projectiles 53; 2 objects repeat it | always 0 | Upper bound of a delay for area-effect damage (name); every stock value is 0. FoC only. Not read |
| `Damage` | units 210, containers 157, structures 55, props 8 (430) | int 1..50000 | Obsolete strength rating kept so the AI treats the object as a combatant (comment). The dump's engine_reads flag is a false positive (only an order word "damage" matches); not read |
| `Damage_Amount` | 1 Hero_Clash (Vader_ObiWan) | 0.0 | Flat part of the damage per hero-clash exchange, added to the loser's max health times a percentage (comment). Not a game object tag. Not read |
| `Projectile_Block_Chance_Modifier` | projectiles 1 (Boba Fett vs Obi-Wan hero clash) | 0.70 | Multiplier on the target's chance to block the shot (name). Not read |
| `Projectile_Redirect_Chance_Modifier` | same projectile | 0.40 | Multiplier on the target's chance to deflect the shot back (name). Not read |

#### Area blast
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Projectile_Blast_Area_Damage` | projectiles 63 | 1..3000 | Damage dealt in the blast area (comment: scatter radius and damage). Not read |
| `Projectile_Blast_Area_Range` | projectiles 62 | 10..2000 | Blast radius; comments ask artists to size the detonation particle to it (comment). Not read |
| `Projectile_Blast_Area_Dropoff` | projectiles 10 | always True | Blast damage falls off with distance (name). Not read |
| `Projectile_Blast_Area_Dropoff_Tiers` | projectiles 10 | 3, 4, 5 | Number of discrete falloff steps (name). Not read |
| `Projectile_Blast_Area_Immune_Faction` | projectiles 3 | faction (Rebel, Underworld) | Faction unharmed by the blast (name, data: construction pods, exploding barrel). A commented-out max-victims tag sits beside them. Not read |

#### Flight and guidance
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Projectile_Category` | projectiles 105 | Laser 47 (+LASER 4, laser 1), DEFAULT 20, MISSILE 9, GRENADE 8, ROCKET 7, BOMB 4, STATIC_BOMB 2, LIGHT_SABER, MPTL_Rocket, CLUSTER_BOMB | Flight model: straight laser, homing missile, lobbed grenade, curved rocket, falling bomb (data; `docs/compatibility-research.md` records a third-party fork's account). Case varies; MPTL_Rocket is likely not a known category (data). Not read |
| `Projectile_Max_Flight_Distance` | projectiles 119 | 0..150000, mostly 400, 2200, 1200; 19 are 0 | Distance after which the shot expires and plays its lifetime particle (data). 0 marks instant-detonation ability/death projectiles (comment). engine: reads; 0 means unlimited, which is harmless only because no stock weapon fires a 0 projectile |
| `Projectile_Max_Lifetime` | projectiles 14 | seconds 0..900 | Time limit for slow or parked shots (grenades, magnepulse blobs), used instead of a flight distance (comment, data). Not read |
| `Projectile_Acceleration_Per_Frame` | projectiles 2 | 0.0 | Per-frame acceleration; comments state it must stay 0 because target interception assumes constant speed (comment). Not read |
| `Projectile_Max_Scan_Range` | projectiles 19 | 0..2000 | Range in which a guided shot may reacquire a target; lowered to stop retargeting (comment). Not read |
| `Projectile_Target_Point_On_Terrain` | projectiles 6 | bool, 3 True / 3 False | Aim at the ground point instead of the target object (name). Not read |
| `Explode_When_Reached_Target_Radius` | projectiles 6 | True/Yes | Detonate on arriving at the target point rather than on contact; tied to bombard-style rockets (comment, name). Not read |
| `Projectile_Rocket_Straight_Distance` | projectiles 10 | 0..500 | Straight run before the rocket starts curving (name). Not read |
| `Projectile_Rocket_Curve_Distance` | projectiles 9 | 50..500, mostly 50 | Spacing of the rocket path's curve points (name; fork claims spacing x 1.1). Not read |
| `Projectile_Rocket_Curve_Offset` | projectiles 9 | 0..180, mostly 5..15 | Maximum sideways displacement of curve points (name); one value of 180 (unknown unit). Not read |
| `Projectile_Grenade_Gravity` | projectiles 11 | 0.5..1.64 | Gravity of the lob arc (name). Not read |
| `Projectile_Grenade_Gravity_Lob_Mod` | projectiles 11 | 0.5..1.0 | Gravity multiplier for the lob (name). Not read |
| `Projectile_Grenade_Can_Lob_Slower` | projectiles 11 | always yes | Grenade may be thrown slower than `Max_Speed` to reach near targets (name). Not read |
| `Projectile_Grenade_Sticks_On_Collision` | projectiles 4 | yes 2 (sticky, remote bomb), no 2 | Grenade attaches to what it hits (name). Not read |
| `Projectile_Bomb_Fall_Accel_Rate` | projectiles 4 (bombing-run bombs) | 0.20 | Per-frame fall acceleration of bombs (name). Not read |

#### Detonation side effects
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Projectile_Stun_On_Detonation` | projectiles 8 | yes | Enables the stun group (comment). Not read |
| `Projectile_Stun_Duration_Frames` | projectiles 8 | 210..1800 frames | Stun length in 30 Hz frames (comment). Not read |
| `Projectile_Stun_Radius` | projectiles 9 | 20..300 | Stun radius (name); Lukes_Light_Saber sets it without the enable flag (probably inert). Not read |
| `Projectile_Stun_Spawn_Effect` | projectiles 8 | Particle name | Effect attached to stunned victims (data). Not read |
| `Projectile_Stun_Victims_Category_Mask` | projectiles 8 | category mask (Vehicle, Structure, Infantry, All) | Categories that can be stunned (name). Not read |
| `Projectile_Stun_Victims_Unit_Types` | projectiles 5 | list of unit types | Specific types stunned regardless of category; the same five droid/hero names each time (data). Not read |
| `Projectile_Ion_Stun_On_Detonation` | projectiles 5 | true/Yes | Enables the ion stun group (comment). Not read |
| `Projectile_Ion_Stun_Duration` | projectiles 5 | seconds 1..30 | Ion stun length; comments tie it to the engine-disable duration (comment). Not read |
| `Projectile_Ion_Stun_Radius` | projectiles 2 | 70, 300 | Ion stun radius (name). FoC only. Not read |
| `Projectile_Ion_Stun_Speed_Reduction_Percent` | projectiles 5 | 0.5..0.999, written with a C `f` suffix in 4 of 5 (`0.999f`) | Fraction of speed removed (comment). Not read |
| `Projectile_Ion_Stun_Shot_Rate_Reduction_Percent` | projectiles 5 | 0.3..0.8 | Fraction of fire rate removed (comment). Not read |
| `Projectile_Ion_Stun_Stack_Duration` | projectiles 5 | bool, 3 No / 2 Yes | Repeated hits extend the stun rather than reset it (name). Not read |
| `Projectile_Disable_Engines_Duration` | projectiles 4 | seconds 3..20 | How long engines stay disabled (name). Not read |
| `Projectile_Disables_Engines_When_Power_Drained` | projectiles 6 | Yes 4 / No 2 | Engines are disabled once the target's power is drained (name). Not read |
| `Projectile_Instant_Heal_On_Detonation` | projectiles 1 (Obi-Wan heal blast) | Yes | Enables the heal group (comment). Not read |
| `Projectile_Instant_Heal_Health_Increase` | same | 200 | Health restored instantly (comment). Not read |
| `Projectile_Instant_Heal_Radius` | same | 300 | Heal radius (name). Not read |
| `Projectile_Instant_Heal_Duration_Frames` | same | 90 | Duration in frames, likely of the effect (name). Not read |
| `Projectile_Instant_Heal_Spawn_Effect` | same | Particle | Effect on healed units (data). Not read |
| `Projectile_Instant_Heal_Targets_Category_Mask` | same | Infantry, LandHero | Categories that can be healed (name). Not read |
| `Projectile_Cause_Invulnerability_On_Detonation` | projectiles 1 (Obi-Wan) | Yes | Enables invulnerability for friendlies; comment says it now acts as a targeted ability (comment). Not read |
| `Projectile_Cause_Invulnerability_Duration_Frames` | same | 300 | Duration in frames (name). Not read |
| `Projectile_Cause_Invulnerability_Max_Targets` | same | 1 | Maximum units affected (name). Not read |
| `Projectile_Cause_Invulnerability_Radius` | same | 0.0 | Radius; 0 fits the single-target use (data). Not read |
| `Projectile_Cause_Invulnerability_Spawn_Effect` | same | Particle | Effect on protected units (data). Not read |
| `Projectile_Cause_Invulnerability_Targets_Category_Mask` | same | Infantry, LandHero, Vehicle, Structure | Categories that can be protected (name). Not read |
| `Projectile_Convert_Enemy_On_Detonation` | projectiles 1 (Emperor; Mara inherits it as a variant) | Yes | Enables converting enemies to the shooter's side (comment). Not read |
| `Projectile_Convert_Enemy_Radius` | projectiles 2 | 100 | Conversion radius (name). Not read |
| `Projectile_Convert_Enemy_Spawn_Effect` | projectiles 1 | Particle | Effect on converted units (data). Not read |
| `Projectile_Convert_Enemy_Targets_Category_Mask` | projectiles 2 | Infantry, Vehicle | Categories that can be converted (name). Not read |
| `Projectile_Weaken_Enemy_On_Detonation` | projectiles 1 (Antilles) | Yes | Enables the weaken group (data). Not read |
| `Projectile_Weaken_Enemy_Radius` | same | 300 | Radius; a comment says an ability's decal distance must match (comment). Not read |
| `Projectile_Weaken_Enemy_Duration_Seconds` | same | 15 | Duration (name). Not read |
| `Projectile_Weaken_Enemy_Cause_Damage_Reduction_Percent` | same | 0.2 | Damage the weakened unit deals is multiplied by 1 minus this (comment). Not read |
| `Projectile_Weaken_Enemy_Take_Damage_Increase_Percent` | same | 0 | Damage the weakened unit takes is multiplied by 1 plus this (comment). Not read |
| `Projectile_Weaken_Enemy_Spawn_Effect` | same | Particle | Effect on weakened units; its lifetime should match the duration (comment). Not read |
| `Projectile_Weaken_Enemy_Targets_Category_Mask` | same | All | Categories that can be weakened, enemies only (comment). Not read |
| `Projectile_Combat_Mod` | projectiles 1 (MAL carbonite missile) | COMBAT_MOD_SPEED | Which combat statistic the hit modifies (name). FoC only. Not read |
| `Projectile_Combat_Mod_Value` | same | -0.8 | Relative change of that statistic (data: a slow effect). FoC only. Not read |
| `Projectile_Combat_Mod_Duration` | same | 10 | Duration, which should match the effect particle (comment). FoC only. Not read |
| `Projectile_Combat_Mod_Effect` | same | Particle (MAL_Slow_Effect) | Effect on affected units (data). FoC only. Not read |
| `Projectile_Combat_Mod_Category_Mask` | same | Vehicle, Infantry, LandHero | Categories affected (name). FoC only. Not read |

#### Detonation and creation effects
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Projectile_Object_Detonation_Particle` | projectiles 110 | Particle name; 19 empty | Effect when the shot hits an object's hull (comment, data). engine: reads, played on a health hit; also the second fallback for projectiles named in `Death_Explosions` |
| `Projectile_Object_Armor_Reduced_Detonation_Particle` | projectiles 58 | mostly the *_Dud particles | Effect when armor reduces the hit (name, data). engine: reads, played on an armor-reduced hit |
| `Projectile_Absorbed_By_Shields_Particle` | projectiles 99 | Projectile_Shield_Absorb_Small/Medium/Large mostly; 16 empty | Effect when shields absorb the hit (name, data). engine: reads, played on a shield hit |
| `Projectile_Ground_Detonation_Particle` | projectiles 107 | Particle name; 37 empty | Effect when the shot hits terrain (name, data). engine: reads only as the third fallback for `Death_Explosions` projectiles; ground hits of shots are not played |
| `Projectile_Lifetime_Detonation_Particle` | projectiles 118 | Particle name; 64 empty | Effect when the shot expires at its flight limit; also the visible blast of instant-detonation projectiles (data). engine: reads, played on expiry; first choice for `Death_Explosions` projectiles |
| `Projectile_Ground_Detonation_SurfaceFX` | projectiles 28 | PROJECTILE_INFANTRY 15, PROJECTILE 10, 3 empty | Surface-effect family whose terrain-dependent effect plays on ground hits (name, data). Not read |
| `Projectile_Object_Creation_Particle` | projectiles 1 (Tyber Zann shotgun) | Particle | Effect spawned when the projectile is created, a muzzle effect (name). FoC only. Not read |
| `Explosion_Jitter_Factor` | Particle objects 27 (explosions) | int 1..4, growing with explosion size | Unknown; probably random placement or shake of the explosion (name). Not read |
| `Explosion_Scorch_Mark` | Particle objects 37 | EXPLOSION_SCORCH_MARK_TINY..MASSIVE(_DARK); 2 empty | Ground scorch mark an explosion leaves (name). Not read |

#### Custom bolt rendering
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Projectile_Custom_Render` | projectiles 39 | 1 (26), 2 (12), 0 (1) | 1 draws an atlas bolt, 2 a tinted teardrop, 0 the model (data, engine). engine: reads; 1 and 2 become bolts |
| `Projectile_Width` | projectiles 41 | 0.3..6 | Bolt width in world units (data). engine: reads, default 1 |
| `Projectile_Length` | projectiles 41 | 2..20 | Bolt length; engine treats it as absolute for render 1 and in widths for render 2 (engine, inferred from a screenshot) |
| `Projectile_Texture_Slot` | projectiles 40 | `column,row` of the 4x4 W_Laser_Pill atlas: 0,0 (15), 3,0 (12), 2,0 (12), 0,3 (1) | Atlas cell of the bolt (data, engine). engine: reads two numbers, or one cell index from mods; warns outside the grid |
| `Projectile_Laser_Color` | projectiles 12 | RGBA 0..255 (5 distinct; ion blue 104,181,230) | Tint of render-2 bolts (data). engine: reads, alpha defaults to 255 |

#### Findings
- engine reads (`src/engine/src/combat_content.cpp`): `Projectile_Damage`, `Damage_Type`, `Projectile_Max_Flight_Distance`, the shield/hit-point flags from the projectile; `Projectile_Types` (first entry), `Projectile_Fire_Recharge_Seconds`, `_Pulse_Count`, `_Pulse_Delay_Seconds` from the shooter; hardpoint `Damage_Type` overrides. Visuals in `src/engine/src/projectile_visuals.cpp`; `Death_Explosions` projectile fallback in `src/engine/src/scene_particles.cpp`. Detonation sounds (`Projectile_SFXEvent_Detonate*`, another family) are read in `battle_audio.cpp`.
- The 6 projectiles carrying `Projectile_Fire_*` are exactly the six entries (both weapons each) of the three units with two `Projectile_Types` (Bossk, Disruptor merc, MAL): per-weapon cadence for multi-weapon units (data). The engine fires only the first entry and ignores projectile-level cadence.
- `Projectile_Damage` on 15 units, 10 hardpoints, 3 structures differs from the fired projectile's value (Rebel_Trooper 3.0 vs Proj_Hand_Blaster_Red 2.0): likely a per-shooter override, as the engine already does for hardpoint `Damage_Type`. Unverified; the engine ignores it, and also ignores unit-level `Damage_Type` (18 units).
- `Damage_Sation_Turbolaser` (misspelt) is used by 8 hardpoints in HardPoints.xml and is not in GameConstants' `<Damage_Types>` list or any `Damage_To_Armor_Mod` row. The engine treats it as modifier 1 everywhere; what the original does with an undeclared type (default type, or none) is unknown. The engine does not read `<Damage_Types>` and so cannot diagnose such names.
- GameConstants notes there are two damage-type systems: the XML table does not apply to special abilities and some other damage tags, which use hard-coded types (comment). Ability damage cannot be fully table-driven.
- `Projectile_Max_Flight_Distance` 0 means "detonate at once" for 19 ability/death-spawn projectiles (comment) but "unlimited" in the engine; no stock weapon fires one, but a mod weapon could. `Projectile_Max_Lifetime` (14) is the time limit for grenades and blobs and is not read.
- Area blast (63 projectiles), all flight models (category, rockets, grenades, bombs), and every detonation side effect are unread: missiles, grenades, and bombs currently fly straight and deal only single-target damage.
- `Projectile_Ground_Detonation_Particle` (70 named) is not played for shots that hit terrain, only as a `Death_Explosions` fallback; `Projectile_Ground_Detonation_SurfaceFX` (28) would add terrain-dependent effects.
- `Projectile_Category` spelling varies (Laser/LASER/laser); a reader must compare case-insensitively. `MPTL_Rocket` (1) is probably not a valid category. The dump's "names referenced Decal 20" for DEFAULT is a name collision, not a reference.
- `Projectile_Ion_Stun_Speed_Reduction_Percent` values carry a C float suffix (`0.999f`, `0.5f`): a strict float parser would reject 4 of 5 values; the engine's parser must accept a trailing `f` when it reads this tag.
- `Proj_Ground_Turbolaser_Green` uses `Projectile_Texture_Slot` 0,3, the only row-3 cell; the recorded atlas layout has pills only in rows 0-1, so this green turbolaser may draw an empty or unexpected cell (likely a data slip for 3,0, unverified; check in the original).
- GameConstants holds `Laser_Kite_Z_Scale_Factor` 1.2 and `Laser_Beam_Z_Scale_Factor` 8.0; their names suggest a length scale for bolts ("kites") and beams that the engine does not apply. Candidate evidence for the open render-1/render-2 length question.
- Comments in the weaken group name the multiplier tags differently from the tags actually used (`..._Damage_Reduction_Percent`, `..._Vulnerability_Percent`), so the comments are not proof of tag names.
- `Max_Secs_For_AE_Delayed_Damage`, `Projectile_Ion_Stun_Radius`, `Projectile_Combat_Mod*`, `Projectile_Object_Creation_Particle`, `Friendly_Damage_Amount` are FoC only. `Damage` (430 objects) is an obsolete AI marker and `Damage_Amount` belongs to hero-clash definitions; neither is projectile data.
- Single-use tags (invulnerability, heal, convert, weaken, combat mod, block/redirect chance) each serve one hero ability; implement them as one generic "detonation effect" record with radius, category mask, duration, and spawn effect rather than per-tag code. `Lukes_Light_Saber` sets `Projectile_Stun_Radius` without `Projectile_Stun_On_Detonation`, so it is probably inert.

### Abilities: Abilities and Unit_Abilities_Data blocks

Game objects declare what they can do beyond moving and shooting in two sibling blocks. Both always carry `SubObjectList="Yes"` (FoC source: all 410 `<Abilities>` and all 268 `<Unit_Abilities_Data>` blocks; EAW: 227 and 135; the installed patched FoC XML has 411 and 269). Counts below are from the FoC source unless marked EAW or installed. Evidence tags: (comment) a developer comment says it, (data) inferred from values and usage, (name) only the name suggests it, (engine) EAWEngine encodes it.

**`<Abilities>`: ability classes.** Each child is an element whose tag names an ability class (`Combat_Bonus_Ability`, `Stealth_Ability`, ...) and which has a required `Name` attribute (every FoC element has one; 496 distinct names, 97 of them shared by several objects, for example the same structure-upgrade effect declared on 7 objects). The class tag picks the behavior; its child fields parameterize it. `Activation_Style` says when it runs: automatically and permanently in some mode (`Ground_Automatic`, `Space_Automatic`, `Combat_Automatic`, `Skirmish_Automatic`, `Galactic_Automatic`), as a reaction (`Take_Damage`), opportunistically during attacks (`Special_Attack`), when the player triggers it in battle (`User_Input`), or as a galactic-map action (`Ground_Activated`). Many economy and galactic classes have no `Activation_Style` at all. These are the mechanics: auras and bonuses, healing, deflection, income, stealth, and the effect logic behind most activated buttons.

**`<Unit_Abilities_Data>`: the activatable abilities.** It holds `<Unit_Ability>` children with no attributes. Each is identified by its `<Type>`, a fixed engine enumeration (the EAW CommandBarComponents.xml comment says its icon lists follow the enum order of UnitAbilityTypes.xml, but that file ships empty, so the enum is in the executable) (comment, data). A Unit_Ability carries the generic button logic: recharge and duration, stat multipliers while it is active, GUI text, icon and sound overrides, autofire support, and an optional link to an `<Abilities>` element. Stock Lua addresses abilities by Type, never by element name: `Activate_Ability("SPREAD_OUT", false)`, `Try_Ability("STEALTH")`, `Is_Ability_Ready`, `Is_Ability_Active`, `Is_Ability_Autofire`, plus the `Unit_Ability_Ready`, `Unit_Ability_Cancelled`, and `Unit_Ability_Finished` events (data, from the decompiled scripts). The scripts mix cases (`"Turbo"`, `"Power_To_Weapons"`, `"TURBO"`), so matching Type names is case-insensitive (data). Objects carry 0 to 3 entries: FoC has 23 empty blocks, 164 with one entry, 79 with two, and 2 with three.

**How the two relate.** Simple Types are self-contained and have no `<Abilities>` counterpart: SPREAD_OUT, POWER_TO_WEAPONS, SPOILER_LOCK, TURBO, DEFEND, SPRINT, and STIM_PACK only modify stats through `Mod_Multiplier`; HUNT, DEPLOY, and SELF_DESTRUCT need no parameters or spawn an object. Complex Types point to the class element that does the work through `GUI_Activated_Ability_Name`, which holds that element's `Name`. In FoC, 129 Unit_Abilities have this field: 80 resolve to an element in the same object, 22 to an element in another object (a hero *team* or company points to the ability on its member unit, for example Darth_Team to Vader_Force_Whirlwind), 17 are empty, and 10 resolve nowhere (see Findings). Going the other way, 80 of the 82 `User_Input` class elements are named by a Unit_Ability of the same object. The 2 that are not are Silri's Force_Confuse (Silri has no FORCE_CONFUSE entry) and Red_Squadron's Lucky_Shot (its entry has no link). No `Special_Attack` element is linked (0 of 10). Those fire on their own during combat, gated by `Activation_Chance` (data). So an activated ability is a pair: the Unit_Ability is the button and the class element is the effect. Passive and automatic classes need no Unit_Ability.

**Variants (`Variant_Of_Existing_Type`).** The data says a variant that declares either block *replaces* the parent's whole list instead of adding to it (data). Every `_Captured` vehicle repeats its parent's entries and adds EJECT_VEHICLE_THIEF (for example T2B_Tank HUNT becomes T2B_Tank_Captured HUNT + EJECT_VEHICLE_THIEF, 13 such cases), which would be pointless if lists merged. Where a captured variant omits a parent entry, the entry is gone: AT_AT_Walker_Captured has only EJECT and no DEPLOY_TROOPERS, HAV_Juggernaut_Captured loses LASER_DEFENSE, and MAL_Captured loses DEPLOY. Squadron containers derived from Darth_Vader_TIE_Fighter_Container swap REPLENISH_WINGMEN for ION_CANNON_SHOT, and mineral extractor variants swap their parent's named Income_Stream and Force_Healing elements for their own. This is consistent with EAWEngine's `GameObjectCatalog::fields`, where the nearest definition that declares a field wins wholesale (engine). Not established: whether an empty `<Unit_Abilities_Data/>` on a variant clears an inherited list (none of the 23 empty blocks is on a variant), and whether the merge is per block or per child `Name`.

#### Shared ability fields

Fields that recur across many `<Abilities>` classes (FoC counts are of elements carrying the field, all classes summed).

| Field | Uses | Values seen | Meaning |
| --- | --- | --- | --- |
| `Activation_Style` | 521 elements | (tag counts) Ground_Automatic 125, Skirmish_Automatic 106, User_Input 82, Space_Automatic 71, Take_Damage 54, Galactic_Automatic 39, Ground_Activated 34, Special_Attack 10, Combat_Automatic 3, COMBAT_AUTOMATIC 2 | When the ability runs (see above). Ground_/Space_Automatic: always on in that tactical mode (data). Skirmish_Automatic: only in skirmish-style tactical battles not spawned from the galactic game; the comment on income streams says they do not exist in campaign play (comment). Combat_Automatic: always on in any battle (data). Galactic_Automatic: passive on the galactic map (data). Ground_Activated: despite the name, a player-triggered *galactic* action (spy, siphon, sabotage, corruption, remove corruption, planet destruction, hack the Death Star). GameConstants lists these by element name in `Activated_*_Ability_Names` (data). Take_Damage: rolled when hit (data). Special_Attack: AI-triggered during attacks (data). User_Input: a battle button (data). Matched case-insensitively (2 upper-case uses). |
| `Applicable_Unit_Categories` | 358 | empty (most), `Infantry`, `Vehicle | Structure`, `Capital | Corvette | Frigate | Fighter`, `All` | Category filter on the units affected or targeted. Separators are `|` or `,` (both appear, sometimes mixed). Several comments say only the listed types count as valid targets (comment). |
| `Applicable_Unit_Types` | 349 | empty, or comma/newline lists of type names | Type-name filter. With Categories, unclear whether the filters combine as OR or AND (unknown). Some lists are separated by newlines only, without commas (Repair_Ability). |
| `Excluded_Unit_Types` | 31 | type lists (Wampa, Buzz_Droids, droids) | Types excluded from the filters above (name, data). |
| `Unit_Strength_Category` | 59 | category lists or empty | Which units a bonus counts or applies to, used by bonus and price classes alongside Applicable_* (name). The difference from Applicable_Unit_Categories is unknown. |
| `Stacking_Category` | 243 | 0, 1, 2, 3, 9 | Groups effects for stacking. A comment says category 3 on hero combat bonuses stacks with other hero combat bonuses (comment). The exact rule (same category replaces, or the best one wins) is unknown. Galactic price and time reductions use 9. |
| `Apply_To_All_Allies` / `Affects_All_Allies` / `Affects_All_Allied_Sources` | 46 / 30 / 29 | Yes/No, True/False | Effect reaches all of the owner's (and allies') units or income sources rather than the owner itself (name, data). There are three spellings, each owned by particular classes. |
| `Initially_Enabled` | 162 | Yes/No | Starts enabled. `No` marks abilities switched on later by upgrades (`Enable_Ability`) or by hardpoints. Comments tie the comm-array radar and the supply-dock income bonus to their hardpoints, and the reflective-armor deflection to its upgrade (comment). |
| `Causes_Despawn` | 68 | Yes/No | Using a galactic action removes the hero from the map for a while (Siphon, Sabotage, Black_Market) (name, data). |
| `SFXEvent_Activate` | 121 | sound event names | Sound when the ability triggers (data). |
| `SFXEvent_Target_Affected` | 64 | sound event names | Sound on each affected target (name). |
| `Owner_Particle_Effect`, `Owner_Particle_Bone_Name` | 14 / 59 | particle type; bone (ROOT, MuzzleA_00) | Particle on the user at a bone (name, data). |
| `Target_Particle_Effect`, `Target_Particle_Bone_Name` | 27 / 25 | particle type; bone lists (`B_TURRET, TURRET_00, B_TURRET_BASE`) | Particle on each target. Comma lists look like fallback bone candidates (data). |
| `Owner_Light_Effect_Type`, `_Color`, `_Color2`, `_Duration`, `_Pulse_Count` | 66 (Type) | Color_Flash; RGB floats above 1 (1.2, 1.2, 1.2); Color2 always empty; 0.5–1.5 s; 1 or empty | A light pulse that tints the user's model (name, data). The colors are overbright multipliers. |
| `Target_Light_Effect_*` (same five) | 15 | Color_Flash, Smooth_Pulse, Linear_Pulse; Pulse_Count up to 25 | The same pulse applied to targets (name). |
| `Activation_Min_Range`, `Activation_Max_Range` | 64 / 66 | 0–20; 30–20000 | Distance window to the clicked or chosen target for activation. Per a developer comment, the unit first closes to this range of its target, then acts (comment). |
| `Activation_Chance` | 26 | a single 0–1 value, or pairs `count,chance, ...` | For automatic attacks, the chance to fire. The pair form is a curve from the number of valid targets in range to the chance (comment). One commented-out alternative uses 0 to disable activation (comment). |
| `Damage_Amount`, `Damage_Percentage`, `Damage_Type`, `Damage_Interval_In_Secs` | 24 / 19 / 11 / 8 | absolute or 0; fraction of target health; `Damage_Force_Whirlwind`, `Damage_Eat`, ... | Damage dealt by attack classes. When Amount is 0, Percentage gives a share of the target's health (data). |
| `Must_Be_Bought_On_Black_Market` | 18 | Yes | FoC: the ability is hidden until the Underworld black market sells it (name, data). The field also appears on Unit_Ability. |
| `Duration_In_Secs` / `Duration_In_Seconds` | 30 / 8 | -1 (permanent), 5–99999 | Duration of the effect. Both spellings occur, depending on the class (data). |
| `Terminate_Effect_On_Move_Command` | 9 | Yes/True | A move order cancels a channelled effect; one comment says movement cancels the ability (comment). |

Unit_Ability-specific fields (recharge and so on) are in the Unit_Ability table, not here. The `<Abilities>` elements themselves carry no recharge, except `Recharge_Time_In_Secs` on Laser_Defense (interval between zaps) and `Recharge_Time_In_Seconds` on Spawn.

#### Ability classes

FoC has 81 classes, 33 of them new in FoC; EAW has 48 real classes plus one stray tag. Uses are FoC source counts (EAW in parentheses, "–" when absent). Shared fields from the table above are omitted.

| Class | Used by (FoC uses (EAW)) | Specific fields | What it does |
| --- | --- | --- | --- |
| `Combat_Bonus_Ability` | 130 (82): heroes, unique units, upgrades, planets' advantages, props | `Health_`, `Damage_`, `Energy_Pool_`, `Shield_`, `Defense_`, `Movement_Speed_Bonus_Percentage` (all 130), `Ability_Recharge_Bonus_Percentage` (4, 0.85), `Fire_Range_Bonus_Percentage` (3), `Specific_Faction` | Aura or upgrade adding fractional stat bonuses (0.25 = +25%) to matching units in the mode given by Activation_Style (data). The heroes' field-commander bonuses are made of these. |
| `Force_Healing_Ability` | 62 (41): heroes, structures, repair and bacta facilities, props, a grenade burn particle | `Heal_Range`, `Heal_Interval_In_Secs`, `Heal_Amount`, `Heal_Percent`, `Single_Target_Heal`, `Heal_Range_Blob_Material`, `Lighting_Effect_Name`, `Blob_Color` | Periodic healing or repair of self (range 0) or units in range, either absolute (`Heal_Amount`) or as a fraction of maximum health (`Heal_Percent`). A negative amount damages instead (comment), which is how area pain effects and Bossk's plasma burn work (data). |
| `Redirect_Blaster_Ability` | 52 (15): Jedi and Sith, vehicles and structures with reflective-armor upgrades, FoC space units | `Redirect_Chance`, `Block_Chance`, `Reaction_Arc_In_Degrees`, `Max_Projectile_Redirection_Angle_In_Degrees`, `Turn_To_Face_Unblockable_Shots` | Projectile deflection on Take_Damage. The redirect roll comes first, then the block roll, otherwise damage is taken (comment). The applicable filters name the shooters whose shots can be deflected (comment). Upgrade variants start disabled (comment). |
| `Battlefield_Modifier_Ability` | 29 (14): generic heroes, special structures, upgrades | `FOW_Reveal_Range_Multiplier`, `Capture_Point_Time_Multiplier`, `Enable_Bombing_Runs`, `Enable_Special_Weapons`, `Enable_Planetary_Bombardment`, `Reinforcement_Deploy_Time_Multiplier`, `Reinforcement_Time_Multiplier`, `Disable_Reinforcement_Vulnerability`, `Reverse_Application_Logic`, `Base_Shield_Upgrade`, `Stealth_Duration_Multiplier` | Battle-wide rule changes for the owner (or its enemies when `Reverse_Application_Logic`): sight range, capture speed, reinforcement timing, permission for bombing runs and bombardment (name, data). |
| `Income_Stream_Mod_Ability` | 29 (20): upgrades, power generators, star bases | `Income_Multiplier`, `Income_Additive_Value`, `Interval_Multiplier`, `Target_Stream_Source`, `Reverse_Application_Logic` | Modifies a named income stream (`Target_Stream_Source` = the type producing it), multiplicatively and additively. The supply-dock bonus is controlled by that hardpoint (comment). |
| `Reduce_Production_Price_Ability` | 28 (23): planets, heroes, unique units | `Price_Reduction_Percentage`, `Galaxy_Wide` | Cheaper production of matching units, locally or galaxy-wide (name, comment gives an example of 25%). |
| `System_Spy_Ability` | 27 (19): heroes, probe droids, planets, structures | 17 `See_*` flags (fleets, fleet contents, stealth heroes, base levels, income, production, political control, super weapons...) | Galactic intelligence: what the owner may see about enemy systems. Probe droids activate it (Ground_Activated, `Duration_In_Secs` 60); heroes and planets have it passively (data). |
| `Income_Stream_Ability` | 27 (18): mining facilities, command centers, star bases | `Base_Income_Value`, `Base_Interval_In_Secs`, `Split_Income_With_Allies`, `Split_Favors_Owner`, `Full_Amount_To_Everyone`, `Owner_Income_Percentage` | Tactical credit income: this many credits every N seconds. Only in tactical modes not started from galactic (comment); doubled on mining colonies (comment). |
| `Galactic_Stealth_Ability` | 25 (14): heroes, smugglers, FoC ships | `Evade_Detection_Chance` (always 0.0) | Hides the unit's presence on the galactic map (name). The chance field is never non-zero (data). |
| `Galaxy_Wide_Upgrade_Ability` | 19 (9): planets | `Space_Upgrade_Type`, `Land_Upgrade_Type` | Owning the planet grants an upgrade object to all space or land units (data). Planetary advantages are built from this. |
| `Enable_Radar_Ability` | 18 (12): comm arrays, skirmish star bases | (Affects_All_Allies, Initially_Enabled) | Shows the radar/minimap. Controlled by the comm-array hardpoint (comment). |
| `Remove_Corruption_Ability` | 14 (–): heroes | `Corruption_Removal_Cost_Multiplier`, `Corruption_Removal_Time_In_Secs` | FoC galactic action: clears Underworld corruption from a planet. The multiplier scales cost (1.2 means 20% more) (comment), and the time is how long removal takes (comment). |
| `Stealth_Ability` | 12 (–): Tyber Zann, Urai Fen, Noghri, F9TZ, TIE Phantom, Peacebringer, Vengeance | `Stealth_Color`, `Stealth_Transition_Time` | Toggled tactical stealth (User_Input), with the cloak tint and fade time (name, data). Paired with Unit_Ability STEALTH. |
| `Enable_Ability` | 12 (6): upgrade objects | `Ability_Name` | Switches on a named ability element (initially disabled) on the matching units: reflective and magnetically sealed armor (data, comment). |
| `Starbase_Upgrade_Ability` | 12 (8): star base level upgrades | none beyond Activation_Style | Marks the upgrade as a star base level-up (name). Behavior is in code (unknown). |
| `Enhance_Defense_Ability` | 7 (7): generic field and fleet commanders | `Enhancement_Percentage` (0.25) | Defense aura for units of the given strength categories (name, data). An EAW comment says it stacks with hero abilities (comment). |
| `Drain_Life_Ability` | 7 (–): Night Sisters, Silri, Lancet, a sensor-node turret | `Duration_In_Seconds`, `Damage_Per_Second`, `Drain_Effect_Name`, `Drain_Source_Bone_Name`, `Drain_Target_Bone_Name`, `Drain_Radius`, `Should_Heal`, `Max_Drain_Victims`, `Charging_Time_In_Seconds` | Beam that damages up to N victims in a radius over time, optionally healing the user (data). |
| `Tractor_Beam_Attack_Ability` | 7 (3): Star Destroyers, tractor satellites | `Target_Speed_Decrease_Percent` | Slows a targeted ship (0.8 = 80% slower). A comment explains the modifier scale: 0 means no change and 1 a 100% change (comment). |
| `Ion_Cannon_Shot_Attack_Ability` | 6 (1): TIE Defender, V-wing and Y-wing containers | targets only | Effect side of ION_CANNON_SHOT. The projectile comes from the Unit_Ability's `Projectile_Types_Override` (data). |
| `Neutralize_Hero_Ability` | 6 (4): bounty hunters | `Cost_Mod_By_Base_Level`, `Cost_Mod_By_Previous_Neutralizations`, `Cost_Mod_By_Planetary_Corruption_Level` (curves), `General_Minor_/Major_Hero_Cost_Mod`, `Owner_/Target_Respawn_Time_In_Secs` (min, max), `Can_Neutralize_Minor_/Major_Heroes` | Galactic assassination of an enemy hero, priced by curves. Generic bounty hunters cannot target major heroes (comment). |
| `Arc_Sweep_Attack_Ability` | 6 (2): Rancors, Night Sister | `Attack_Animation`, `_Speedup`, `_Subindex`, `Arc_Width_In_Degrees`, `Arc_Min_/Max_Range` (EAW: `Damage_Min_/Max_Range`), `Begin_Applying_Damage_Frame`, `Damage_Sweep_Time_In_Secs`, `Apply_Damage_Right_To_Left` | Melee sweep through an arc, timed to an animation frame (comment: left-to-right or right-to-left). The EAW comment says the whirlwind damage type was borrowed for its knockback death animations (comment). No Activation_Style; uses Activation_Chance. |
| `Earthquake_Attack_Ability` | 6 (3): Force users | (damage fields) | Channelled damage to structures. Targets must also carry the EARTHQUAKE_TARGET behavior (comment). Special_Attack. |
| `Sensor_Jamming_Ability` | 5 (–): Skipray, Houndstooth, sensor satellites | `Blob_Color`, `Blob_Material_Name` | Jams enemy sensors in an area for a duration (name, data). |
| `Absorb_Blaster_Ability` | 5 (2): Vader, Palpatine, Luke, Yoda | `Absorb_Chance`, `Damage_Absorb_Percentage`, `Damage_Absorb_Amount` | Take_Damage: absorbs a shot and heals by projectile damage × percentage + amount; at least one of the two should be non-zero; not for missiles or grenades (comment). |
| `Siphon_Credits_Ability` | 4 (4): smugglers, Han Solo | `Min_/Max_Siphon_Percentage`, `Siphon_Duration_In_Cycles`, `Min_/Max_Respawn_Time` | Galactic action stealing a share of a planet's income for some cycles (name, data). |
| `Generic_Attack_Ability` | 4 (2): Rancors, Wampa, Night Sister | `Damage_Frame_Number`, `SFXEvent_Apply_Damage`, `Attack_Animation(_Speedup)` | An animation-timed melee hit (POUND_ATTACK) on vehicles (data). |
| `Base_Power_Ability` | 4 (2): power generators, palace | none | Powers the base (base shields and turrets) (name). |
| `Remote_Bomb_Ability` | 4 (–): saboteur | `Bomb_Type`, `Toss_Anim`, `Spawn_Frame`, `Spawn_Bone` | Plants a remote bomb object (pairs with PLACE_REMOTE_BOMB; DETONATE_REMOTE_BOMB fires it) (data). |
| `Reduce_Production_Time_Ability` | 4 (–): planets | `Time_Reduction_Percentage` (negative = slower, comment), `Galaxy_Wide` | Faster or slower builds for matching units (comment). |
| `Planet_Income_Bonus_Ability` | 4 (2): mining facilities, palace | `Specific_Mod_Source_Text`, `Percentage_Income_Modifier`, `Absolute_Income_Modifier`, `Additional_Multiplier_On_Mining_Colonies` | Raises galactic planetary income by a percentage plus an absolute amount; the text names the source in the income breakdown (comment, data). The inline example comments disagree with the values (they mention 3% and +20) (data). |
| `Black_Market_Ability` | 4 (–): Tyber, Urai, Silri | `Min_/Max_Respawn_Times` (5 values, per tech level?), `Price_Modifier`, `Tech_Level_Adjustment` | FoC galactic black market action (name). The five-value lists probably map to tech levels (data, guess). |
| `Proximity_Mines_Ability` | 4 (–): Underworld mercs | `Mine_Type`, `Trigger_Radius`, `Number_Of_Mines`, `Activation_Time_In_Seconds`, `Activate_SFX` | Lays N mines of a type (name, data). |
| `Garrison_Upgrade_Ability` | 4 (4): "more garrisons" upgrades | `Additional_Garrison_Units` | Adds units to structure garrisons (name, data). |
| `Grenade_Attack_Ability` | 3 (3): Infiltrator, Mara Jade, Kyle Katarn | `Requires_Direct_Player_Command`, `Grenade_Type`, `Grenade_Toss_Anim`, `Grenade_Spawn_Frame`, `Grenade_Spawn_Bone`, `Grenade_Explode_Timer_In_Secs` | Sticky bomb thrown at a vehicle or structure (pairs with STICKY_BOMB). A comment says the style used to be Special_Attack (comment). |
| `Permanent_Weapon_Swap_Ability` | 3 (–): Disruptor mercs | `Weapon_Index` | Permanently switches to weapon index 1 once bought on the black market (name, data). |
| `Weatherproof_Ability` | 3 (2): weatherproof upgrades, Felucia advantage | none | Makes units immune to weather penalties (name). |
| `Bounty_On_Faction_Ability` | 3 (–): Underworld upgrades, Nal Hutta | `Bounty_Percentage_Amount`, `Target_Faction_Names` | Credits for destroying objects of the listed factions (comment). The faction list spells "Hutt" and "Hutts" in different elements (data). |
| `Lucky_Shot_Attack_Ability` | 2 (2) (installed: 3): Red Squadron, Luke's X-wing | targets only | Effect side of LUCKY_SHOT (data). |
| `Hero_Protection_Ability` | 2 (1): Han Solo, Jabba | `Neutralization_Cost_Multiplier` | Raises the cost of neutralizing this hero (name, data). |
| `Galactic_Sabotage_Ability` | 2 (–): saboteur | `Can_Destroy_Special_Structure`, `Sabotage_Cost_Increase_Factor` | Galactic sabotage of a structure (name). |
| `Corruption_Ability` | 2 (–): saboteur | none | Galactic planet corruption; a comment calls it the new style of corruption (comment). |
| `Personal_Flame_Thrower_Ability` | 2 (1): Boba Fett, Bossk | `Damage_Arc_In_Degrees`, `Damage_Amount`, `Fire_Time_In_Secs`, `Damage_Delay_In_Secs`, `Flame_Emitter_Model_Name` | Cone flame attack (pairs with FLAME_THROWER) (data). |
| `Force_Confuse_Ability` | 2 (–): Obi-Wan, Silri | `Confuse_Time_In_Secs`, `Confuse_Range`, `Confuse_Travel_Time_In_Secs`, `Confuse_Effect` | Expanding wave that makes infantry fight their own side; the travel time is how long the wave takes to reach max range (comment, name). |
| `Force_Whirlwind_Ability` | 2 (1): Vader | `Damage_Range`, `Travel_Time_In_Secs` | Shockwave knocking down infantry. Its Activation_Chance curve gates autofire use (comment). |
| `Force_Telekinesis_Ability` | 2 (1): Vader | (damage, interval) | Lifts and crushes a vehicle. Targets need the TELEKINESIS_TARGET behavior (comment). |
| `Find_Weakness_Ability` | 2 (1): Dodonna, Thrawn | `Damage_Bonus_Percentage` | Allies deal double damage to the marked unit (comment). |
| `Planet_Destruction_Ability` | 2 (1): Death Star, Death Star II | (target effects only) | Galactic planet destruction (Ground_Activated) (data). |
| `Super_Laser_Ability` | 2 (–): Death Star II, Eclipse | `Damage_Per_Frame`, `Fire_Frame`, `Beam_Texture_Name`, `Num_Anim_Frames`, `Line_Width`, `Line_Color`, `Attack_Anim_Sub_Index`, `Face_Target`, `Turn_Rate`, `Firing_Cone`, `Fire_Delay`, `Fire_Continuation`, `Alternate_Description_Text` | Tactical superlaser beam on capital ships (data). Several values carry C float suffixes (`-1.0f`). |
| `Stun_Ability` | 2 (–): Urai Fen | `Stun_Time_In_Secs`, `Stun_Range`, `Stun_Travel_Time_In_Secs`, `Stunned_Effect` | Expanding stun wave on infantry (name, comment on travel time). |
| `Laser_Defense_Ability` | 2 (–): Crusader gunship, HAV Juggernaut | `Defense_Duration_In_Secs`, `Laser_Color`, `Bone_Names`, `Projectile_Types_Targeted`, `Protection_Radius`, `Laser_Beam_Frames`, `_Width`, `_Texture`, `Projectile_Dist_Travelled`, `Lightning_Effect_Name`, `Zap_SFXEvent`, `Recharge_Time_In_Secs` | Point defense shooting down missiles and rockets in a radius (name, data). |
| `Buzz_Droids_Ability` | 2 (–): StarViper | `Chase_Radius`, `Damage_Radius`, `Enemy_Damage_Per_Second`, `Own_Damage_Per_Second`, `Activation_Time_In_Seconds`, `Activate_SFX`, `Object_Type` | Releases buzz droids that damage nearby ships. A comment says `Chase_Radius` must match the Unit_Ability's `Area_Effect_Decal_Distance` (comment). |
| `Hack_Ability` | 1 (1): R2-D2 and C-3PO team | `Team_Member_With_Ability`, `Lightning_*` (effect, source/target bone, max targets, radius) | R2 hacks an enemy turret. The team member field is required for team objects (comment). |
| `Repair_Ability` | 1 (1): R2-D2 and C-3PO team | as Hack | R2 repairs a friendly vehicle; Empire vehicles are listed so hacked ones get repaired too (comment). |
| `Eat_Attack_Ability` | 1 (2) (installed: 3): Sarlacc; installed adds Rancor, Night Sister | `Owner_Attachment_Bone`, `Attachment_Frame`, `Target_Destruction_Frame`, `Target_Grabbed_Animation`, `Needs_To_Rotate`, `Rotation_Base_Bone_Name` | Grab and eat an infantry unit, attaching it to a bone at a frame (data). |
| `Cable_Ability` | 1 (1): Snowspeeder | `Cable_Width`, `_Fadeout_Time`, `_Render_Mode`, `_Texture_Name`, `Drop_`/`Pull_`/`Paralyze_Phase_Duration`, `Cable_Length_Fraction`, `Cable_End_Speed_Fraction` | Tow cable that trips an AT-AT; cancelled by movement (comment). The phase durations look like frames (data, guess). |
| `Force_Lightning_Ability` | 1 (1): Palpatine | `Damage_Application_Time_In_Secs`, `Start_Lightning_Frame_Number`, `Lightning_Effect_Name`, `_Source_Bone_Name`, `_Target_Bone_Name`, `_Max_Targets`, `_Targets_Radius` | Chain lightning: the max-targets count includes the first victim, and the radius is how far it seeks more; effect names refer to LightningEffectTypes.xml (comment). A comment says it must come after Force Whirlwind in the list or Whirlwind never fires (comment): **element order matters**. |
| `Political_Transition_Bonus_Ability` | 1 (1): Tarkin | `Time_Reduction_Percentage` | Faster political conversion of planets (name). |
| `Political_Control_Protection_Ability` | 1 (1): Tarkin | none | Prevents planets from leaving (name). |
| `Retreat_Prevention_Ability` | 1 (1): Tarkin | none | Enemies cannot retreat from the battle (name; a comment wonders whether it should be space only). |
| `Vehicle_Thief_Ability` | 1 (1): Chewbacca | targets | Steals a vehicle. Each target type needs a `_Captured` clone (comment). Pairs with CAPTURE_VEHICLE. |
| `Slicer_Ability` | 1 (1): R2-D2 | `Min_/Max_Respawn_Time_Per_Tech_Level`, `Cost_Multiplier_By_Planet_Tech_Availability`, `Cost_Affinity_Modifier`, `Chance_Modifier_By_Base_Level`, `Chance_Modifier_By_Planet_Difficulty`, `Chance_Affinity_Modifier` | Galactic tech theft (name). |
| `Reduce_Technology_Price_Ability` | 1 (–): The Maw | `Price_Reduction_Percentage` | Cheaper research (name). |
| `Planet_Income_Gambling_Ability` | 1 (1): Grand Arena | `Evaluation_Interval_In_Secs`, `Min_/Max_Income_Modifier`, `Chance_To_Win` | Rerolls an income modifier at each interval, between the minimum and maximum, positive with the given chance (comment). |
| `Energy_Weapon_Attack_Ability` | 1 (1): Accuser (Piet) | `Damage_Per_Frame` | Beam attack on a ship (pairs with ENERGY_WEAPON) (data). |
| `Maximum_Firepower_Attack_Ability` | 1 (1): Veers' AT-AT | targets | Effect side of MAXIMUM_FIREPOWER (data). |
| `Concentrate_Fire_Attack_Ability` | 1 (1): Home One | `Target_Damage_Increase_Percent`, `Target_Speed_Decrease_Percent` | Marks a target that takes more damage (data). |
| `Shield_Flare_Ability` | 1 (–): Gargantuan | `Max_Damage_Per_Victim`, `Damage_Radius`, `Charge_Up_Seconds` | Shield pulse damaging ships around it (name, data). |
| `Saber_Throw_Ability` | 1 (–): Luke | `Saber_Name`, `Catch_Dist` | Throws the saber object and catches it within a distance (name). |
| `Force_Cloak_Ability` | 1 (–): Luke | `Force_Cloak_Color`, `Force_Cloak_Transition_In_Secs` | Force invisibility (name). |
| `Berserker_Ability` | 1 (–): Yoda | `Berserker_Speed`, `_Damage`, `_Damage_Range`, `_Duration_In_Secs` | Yoda's leaping rampage (name, data). |
| `Force_Sight_Ability` | 1 (–): Yoda | `Force_Sight_Duration_In_Secs` | Reveals the map or stealthed units (name). |
| `Hack_Super_Weapon_Ability` | 1 (–): IG-88 | (target effects) | Galactic action against the Death Star (data, GameConstants list). |
| `Infection_Ability` | 1 (–): IG-88 | `Shoot_Anim`, `Spawn_Frame`, `Spawn_Bone`, `Damage_Per_Second`, `Duration_Of_Infection`, `Infection_CategoryMask`, `Distance_To_Infect`, `Infection_Particle`, `Projectile_Type`, `Seconds_To_Infect` | Infection that spreads between nearby infantry (name, data). |
| `Radioactive_Contaminate_Ability` | 1 (–): IG-88 | `Spray_Time_In_Secs`, `Contamination_Object_Name`, `SFXEvent_Radioactive` | Sprays a contamination object (name). |
| `Corrupt_Systems_Ability` | 1 (–): IG-2000 | `Duration_In_Seconds`, `Shield_Drain_Per_Second`, `Inaccuracy_Radius` | Drains the target's shields and spoils its aim (name, data). |
| `Summon_Ability` | 1 (–): Silri | `Object_Type`, `SFXEvent_Unit_Summoned`, `Creation_Delay_In_Seconds` | Spawns an object (Cuddles the Rancor) after a delay (data). |
| `Tactical_Bribe_Ability` | 1 (–): Tyber Zann | `Bribed_Effect`, `_Small`, `Effect_Z_Offset`, `SFXEvent_Success`, `SFXEvent_Fail` | Takes control of an enemy unit in battle; it can fail (name, data). |
| `Blast_Ability` | 1 (–): Peacebringer | `Charge_Up_Seconds`, `Damage_Multiplier`, `Charging_Effect`, `Charged_Effect` | Charged shot with a damage multiplier (name). |
| `Spawn_Ability` | 1 (–): Ewok Handler | `Object_Type`, `Creation_Delay_In_Seconds`, `Recharge_Time_In_Seconds`, `Spawn_Bone_Name`, `Self_Destruct_Radius`, `Explosion_Type` | Automatically sends a suicide bomber (Special_Attack, with its own recharge; the handler's Unit_Abilities_Data is empty) (data). |
| `Cluster_Bomb_Ability` | 1 (–): MC30 | `Detonation_Time_In_Secs`, `Bomb_Type`, `Number_Of_Bombs` | Releases N bombs (name). |
| `Leech_Shields_Ability` | 1 (–): Kedalbe | `Duration_In_Secs`, `Shield_Damage_Per_Second`, `Damage_Multiplier`, `Beam_*`, `Leech_Effect` | Beam that drains the target's shields (name). |
| `Converted_To_Enemy_Die_Time_Seconds` (EAW only) | (1): one GroundVehicle | – | Not a class: an object field misplaced inside `<Abilities>` in EAW GroundVehicles.xml. Sibling objects have it as a direct object field (data). It has no Name attribute. |

#### Unit_Ability types

FoC source has 71 Type spellings (70 case-insensitive), EAW 44 (43). Counts are FoC (EAW). Every entry has `Type`; the fields used by that Type are listed. Shared Unit_Ability fields (FoC uses): `Mod_Multiplier` 307 (several per entry, `KIND, value`), `Recharge_Seconds` 217, `GUI_Activated_Ability_Name` 129, `SFXEvent_GUI_Unit_Ability_Activated` 99, `Expiration_Seconds` 98, `Supports_Autofire` 66, `SFXEvent_Target_Ability` 58, `Effective_Radius` 48, `SFXEvent_GUI_Unit_Ability_Deactivated` 36, `Spawned_Object_Type` 35, `Alternate_Description_Text` 33, `Alternate_Name_Text` 31, `Must_Be_Bought_On_Black_Market` 25, `Area_Effect_Decal_Distance` 19, `Alternate_Icon_Name` 15, `Projectile_Types_Override` 14, `SFXEvent_Special_Ability_Loop` 12, `Particle_Effect` 10, and rarer ones listed per Type. Meanings: `Recharge_Seconds` is the cooldown and `Expiration_Seconds` the active duration of a timed ability (name, data; toggles have no expiration). `Supports_Autofire` means the player can set the ability to fire automatically (name, Lua `Is_Ability_Autofire`). `Effective_Radius` is the radius of an area ability (-1 for unlimited is a guess). `Area_Effect_Decal_Distance` is the radius of the targeting decal (comment, Buzz_Droids). `Alternate_*` override the button text and icon. `Projectile_Types_Override` replaces the unit's projectile while active, and `Spawned_Object_Type` is created on activation (data).

`Mod_Multiplier` kinds (FoC, entries × uses): SPEED_MULTIPLIER 100, WEAPON_DELAY_MULTIPLIER 55, ENERGY_REGEN_MULTIPLIER 42, SHIELD_REGEN_MULTIPLIER 41 (negative values such as -25 appear), TAKE_DAMAGE_MULTIPLIER 40, CAUSE_DAMAGE_MULTIPLIER 9, SHIELD_REGEN_INTERVAL_MULTIPLIER 8, ENERGY_REGEN_INTERVAL_MULTIPLIER 8, FIRE_RATE_MULTIPLIER 4. Values often carry a C `f` suffix (`0.5f`) and tab separators. `Mod_Flag` (2): `ALLOW_TARGET_DIMINISHING_FIREPOWER, false`.

| Type | Uses FoC (EAW) | Users (examples) | Direct fields | Meaning |
| --- | --- | --- | --- | --- |
| SPREAD_OUT | 29 (26) | infantry, squads, field commanders | Mod: SPEED 0.5, TAKE_DAMAGE 0.5; activation SFX | Take-cover toggle: crouch, half speed, half damage taken, wider formation (`Spread_Out_Spacing_Coefficient` 1.8 in GameConstants) (data, engine notes in compatibility-research.md). |
| HUNT | 25 (17) | light tanks, fighters, scout squads | only alt name/description (2) | Unit seeks out enemies on its own (name); no parameters (data). |
| POWER_TO_WEAPONS (+ `power_to_weapons` 1) | 19 (9); installed 20 | Star Destroyers, IPV, M1 tank, Wedge's X-wing | Recharge 50–60, Expiration 6–20, Mod SPEED, SHIELD_REGEN, ENERGY_REGEN, WEAPON_DELAY, CAUSE_DAMAGE | Timed trade of mobility and shields for firepower (data). |
| EJECT_VEHICLE_THIEF | 18 (10) | every `_Captured` vehicle | Recharge 10 | Ejects the hero who stole the vehicle (name, data). |
| SPOILER_LOCK | 14 (8) | X-wing, B-wing, Luke's X-wing | Mod WEAPON_DELAY, SHIELD/ENERGY_REGEN, SPEED; on/off SFX | S-foil toggle: attack mode against cruise mode (name; Lua toggles it with `false`). |
| STEALTH | 14 (–) | Tyber, Urai, Noghri, F9TZ, TIE Phantom | Recharge 10–60, Expiration 15–120, GUI link (14, all to Stealth_Ability), alt texts, Black_Market (5) | Timed stealth (data). |
| DEPLOY | 11 (5) | SPMAT, MPTL, MAL, Dark Trooper III, MDU | alt description (2) | Deploy and undeploy: siege mode, MDU build (name). |
| ION_CANNON_SHOT | 10 (3) | TIE Defender, V-wing, Y-wing | Recharge 20–25, GUI link (to Ion_Cannon_Shot_Attack_Ability), Projectile_Types_Override, Autofire | Ion shot disabling a target's systems (name). |
| SELF_DESTRUCT | 10 (3) | TIE Crawler, MZ8, Krayt, Vengeance | Expiration 2–18 (countdown), Recharge 10, Spawned_Object_Type (blast), Owner_Attachment_Bone, Particle_Effect, Stop_When_Activated No, Max_Secs_For_AE_Delayed_Damage 0, Black_Market | Blows itself up after a countdown, spawning a blast object (data). |
| TRACTOR_BEAM | 8 (4) | Star Destroyers, tractor satellite | Recharge 5–25, GUI link, Mod SPEED, loop SFX, Autofire | Button for Tractor_Beam_Attack_Ability. Mod SPEED is the user's own slowdown (data, guess). |
| DEFEND | 8 (6) | Assault Frigate, Calamari Cruiser, Home One | Expiration 15–25, Recharge 20–60, Mod WEAPON_DELAY, SHIELD/ENERGY_REGEN and their INTERVAL, SPEED | Power to shields (EAW icon list: `i_button_sa_power_to_shields`) (data). Akbar's two entries hold two Expiration_Seconds each. |
| LUCKY_SHOT | 7 (6) | Red Squadron, Luke's X-wing | Recharge 60, Mod CAUSE_DAMAGE, Projectile override, target SFX, Autofire | A single high-damage shot (name, data). |
| STIM_PACK | 7 (–) | Underworld mercs | Expiration 8–15, Recharge 35–45, Mod SPEED, WEAPON_DELAY, `Damage_Percent_When_Activated` 0.25–0.35 | Combat boost that costs a share of the unit's health on use (name, data). |
| LURE | 6 (2) | A-wing, Canderous tank | Effective_Radius 350, Target_Types, Recharge, Expiration, `Is_Pulsing`, `Pulse_Frequency_Secs` 0.5, `Max_Number_Of_Pulses` 40, Particle_Effect, Mod TAKE_DAMAGE | Taunt that pulls listed enemy types to attack it while it takes less damage (name, data). |
| JET_PACK | 6 (2) | Boba Fett, Dark Troopers II/III | Recharge 45–60, GUI link, Effective_Radius -1, target SFX | Jump to a target point (data). |
| SPRINT | 6 (4) | Han, Chewbacca, Dark Trooper I | Expiration 7–12, Recharge 30–45, Mod SPEED, TAKE_DAMAGE | Timed speed burst (data). |
| MISSILE_SHIELD | 5 (4) | AT-AA, Interdictor, Arc Hammer | Effective_Radius 100–1000, Recharge, Expiration, `Disable_Movement`, Autofire | Shield against missiles in a radius; the unit cannot move (data). |
| ROCKET_ATTACK | 5 (2) | T4-B, Destroyer droid | `Targeting_Max_Attack_Distance` 200–300, alt texts | Switches to or fires a rocket weapon (name). |
| FOW_REVEAL_PING | 4 (3) | TIE Scout, MPTL spotter, uplink station | Recharge 23–30, Spawned_Object_Type (reveal blast), Effective_Radius 0, decal distance | Temporarily reveals fog of war around a point (name, data). |
| DEPLOY_TROOPERS | 4 (4) | AT-AT, Veers | Recharge 20, Spawned_Object_Type, `Max_Num_Spawned_Objects` 2, Owner_Attachment_Bone (trooper bones) | Unloads stormtrooper squads at bones, at most 2 (data). |
| STICKY_BOMB | 4 (4) | Infiltrator, Mara, Kyle | Recharge (`30.0f`), GUI link to Grenade_Attack_Ability | Button for the grenade class (data). |
| DRAIN_LIFE | 4 (–) | Night Sisters, Silri | Recharge 10–50, GUI link, alt icon and texts | Button for Drain_Life_Ability (data). |
| BARRAGE | 4 (4) | AT-ST, Broadside, Marauder | Expiration 10, Recharge 40–60, Mod FIRE_RATE, Projectile override, decal distance, `Targeting_Fire_Inaccuracy_Fixed_Radius_Override`, `Target_Position_Z_Offset` | Area bombardment of a ground point with a fixed scatter radius (data). |
| FORCE_TELEKINESIS | 4 (2) | Vader, Darth_Team | Recharge 30, GUI link | Button for the class (data). |
| FORCE_WHIRLWIND | 4 (2) | Vader | Recharge 25–30, GUI link, Effective_Radius 500, Spawned_Object_Type (particles) | Button for the class (data). |
| TURBO | 4 (3) | Corellian Corvette and Gunboat, Sundered Heart | Expiration 20–23, Recharge 50–60, Mod WEAPON_DELAY, SHIELD/ENERGY_REGEN, SPEED | Speed boost at the cost of weapons (data). |
| PLACE_REMOTE_BOMB | 4 (–) | saboteur | Recharge 2–30, GUI link to Remote_Bomb_Ability, Autofire | Plants the bomb (data). |
| DETONATE_REMOTE_BOMB | 4 (–) | saboteur | none | Detonates placed bombs (name). |
| SWAP_WEAPONS | 4 (–) | Bossk, MAL | alt texts, GUI link (dangling), Black_Market (3) | Switches the weapon set (name). |
| PROXIMITY_MINES | 4 (–) | Underworld mercs | Recharge 80, GUI link, Effective_Radius 500, decal distance, Black_Market | Button for the mine class (data). |
| REPLENISH_WINGMEN | 3 (3) | Vader's TIE, TIE Prototype | Recharge 60 (1), Particle_Effect, `Active_By_Default`, Autofire | Respawns lost squadron wingmen (name). |
| TARGETED_HACK | 3 (3) | R2-D2 | Recharge 45, GUI link (Hack_Ability), target SFX | Button for hack (data). |
| TARGETED_REPAIR | 3 (3) | R2-D2 | Recharge 35–50, GUI link (Repair_Ability), `Friendly_Ability` | Button for repair; targets friendly units (data). |
| FLAME_THROWER | 3 (2) | Boba Fett, Bossk | Recharge 28–30, GUI link | Button for the flame thrower (data). |
| AREA_EFFECT_CONVERT | 3 (3) | Palpatine, Mara Jade | Recharge 50–70, GUI link, Spawned_Object_Type (corrupt blast projectile) | Turns enemy infantry in an area to the owner's side, through a spawned blast (name, data). |
| SENSOR_JAMMING | 3 (–) | Skipray, Houndstooth | Recharge 35–75, Expiration 30, Effective_Radius 300–900, GUI link | Button for jamming (data). |
| BUZZ_DROIDS | 3 (–) | StarViper | Recharge 60, GUI link, decal distance, Black_Market | Button for buzz droids (data). |
| FIRE_LOBBING_SUPERWEAPON | 2 (1) | Magnepulse cannon | Recharge 30, decal distance | Fires the ground superweapon at a point (name). |
| UNTARGETED_STICKY_BOMB | 2 (2) | Scout Trooper | Recharge 60–64, Spawned_Object_Type (speeder bomb), `Bomb_Countdown_Seconds` | Drops a timed bomb in place (name, data). |
| CABLE_ATTACK | 2 (2) | Snowspeeder | Recharge 120, GUI link | Button for Cable_Ability (data). |
| ENERGY_WEAPON | 2 (2) | Accuser, Piet | Recharge 20–120, Expiration 5–10, Mod SPEED, GUI link, loop SFX | Button for the energy weapon (data). |
| MAXIMUM_FIREPOWER | 2 (2) | Veers | Expiration 1–4, Recharge 30–70, Mod WEAPON_DELAY, Projectile override, Mod_Flag | Overpowered shots (data). |
| FORCE_LIGHTNING | 2 (2) | Palpatine | Recharge 20, GUI link | Button for lightning (data). |
| CONCENTRATE_FIRE | 2 (2) | Home One, Akbar | Effective_Radius 6000, Recharge 20, Expiration 15, Particle_Effect | Allies focus on the marked target (name, data). |
| AREA_EFFECT_STUN | 2 (2) | Han Solo | Recharge 20–40, Spawned_Object_Type (stun blast) | Area stun through a spawned blast (data). |
| CAPTURE_VEHICLE | 2 (2) | Chewbacca | Recharge 20–55, GUI link (Vehicle_Thief_Ability) | Button for vehicle theft (data). |
| FORCE_CONFUSE | 2 (–) | Obi-Wan | Recharge 40, Effective_Radius 500, GUI link | Button for confuse (data). |
| TARGETED_INVULNERABILITY | 2 (2) | Obi-Wan | Recharge 60, Spawned_Object_Type, Effective_Radius 400, Friendly_Ability | Makes a friendly target invulnerable (name, data). |
| DEPLOY_SQUAD | 2 (–) | Executor, a mission SSD | Recharge 5 | Launches a squadron (name). |
| INTERDICT | 2 (2) | Interdictor, gravity-well station | Disable_Movement, Active_By_Default (1), on/off SFX | Gravity well preventing hyperspace retreat; toggled; the unit cannot move (name, Lua on/off). |
| SUPER_LASER | 2 (–) | Death Star II, Eclipse | Recharge 10–60, Expiration 10, Mod SPEED, GUI link, loop SFX | Button for the superlaser (data). |
| TACTICAL_BRIBE | 2 (–) | Tyber Zann | Recharge 50, GUI link | Button for bribe (data). |
| STUN | 2 (–) | Urai Fen | Recharge 30, Effective_Radius 500, Spawned_Object_Type (particles), GUI link | Button for stun (data). |
| LASER_DEFENSE | 2 (–) | Crusader, Juggernaut | Recharge 35–60, Effective_Radius 500, GUI link, Autofire True/False | Button for point defense (data). |
| INVULNERABILITY | 1 (1) | Millennium Falcon | Recharge 60, Expiration 6, Mod TAKE_DAMAGE | Timed damage reduction (data). |
| HARMONIC_BOMB | 1 (1) | Slave I | Recharge 20, Spawned_Object_Type (bomb), Bomb_Countdown_Seconds | Drops a seismic or harmonic bomb (name). |
| WEAKEN_ENEMY | 1 (1) | Sundered Heart | Recharge 60, Effective_Radius 1000, Spawned_Object_Type, decal distance | Area debuff on enemies through a spawned effect (name). |
| SHIELD_FLARE | 1 (–) | Gargantuan | Recharge 45, GUI link | Button for the flare (data). |
| SABER_THROW | 1 (–) | Luke | Recharge 30, GUI link | Button (data). |
| FORCE_CLOAK | 1 (–) | Luke | Recharge 10, Expiration 60, GUI link | Button (data). |
| BERSERKER | 1 (–) | Yoda | Recharge 70, GUI link | Button (data). |
| FORCE_SIGHT | 1 (–) | Yoda | Recharge 50, GUI link, loop SFX | Button; Lua toggles it on and off (data). |
| INFECTION | 1 (–) | IG-88 | Recharge 30, GUI link | Button (data). |
| RADIOACTIVE_CONTAMINATE | 1 (–) | IG-88 | Recharge 45, GUI link, Spawned_Object_Type | Button (data). |
| CORRUPT_SYSTEMS | 1 (–) | IG-2000 | Recharge 60, GUI link | Button (data). |
| SUMMON | 1 (–) | Silri | Recharge 80, GUI link | Button (data). |
| BLAST | 1 (–) | Peacebringer | Recharge 90, GUI link | Button (data). |
| CLUSTER_BOMB | 1 (–) | MC30 | Recharge 50, GUI link | Button (data). |
| FULL_SALVO | 1 (–) | Interceptor IV | Recharge 45, Expiration 7, GUI link (dangling), Black_Market | Fires all weapons for a time (name). |
| LEECH_SHIELDS | 1 (–) | Kedalbe | Recharge 55, GUI link, loop SFX, Black_Market | Button (data). |
| AREA_EFFECT_HEAL | – (2) | EAW Obi-Wan | – | EAW only: area heal. FoC gives Obi-Wan TARGETED_INVULNERABILITY instead (data). |

The EAW CommandBarComponents comment also names enum values with no stock users (GRAVITY_WELL_GENERATOR, HARASS, AFTERBURNER, AVOID_DANGER, DISTRACT), so the enumeration is larger than what the data uses (comment).

#### Clash_Actions (short)

`<Clash_Actions SubObjectList="Yes">` appears only on `Hero_Clash` objects (identical in EAW and FoC): scripted duels between Boba Fett and Han, Chewbacca, or Obi-Wan. The children are `Attack_Action` (10), `Move_Action` (7), and `Special_Ability_Action` (3). Each has `Action_Object` and `Action_Target` (hero types), and a weight from `Action_Relevance_Type` (`Distance_From_Target_Curve` or `..._Times_Health_Percentage`) evaluated from piecewise-linear `distance,weight` and `health,weight` curves. Attack actions fire a dedicated clash projectile (`Attack_Projectile_Type`, `_Number_Of_Shots`, `_Animation_*`, `_Firing_Bone_Name`, `_SFXEvent`). Move actions move to or past the target within a minimum and maximum distance (comment: stop no closer and no further than these), optionally with a jetpack or an animation override. The special-ability action triggers a named ability element (`Ability_Name` = Boba_Fett_Flame_Thrower) and waits `Ability_Cooldown_Time_In_Secs` for it to finish (comment). It is a small utility AI, not a general ability mechanism (data).

#### Findings

- **Activated abilities are pairs, and team objects relay them.** The Unit_Ability (Type, cooldown, multipliers, GUI) is the button and the `<Abilities>` element named in `GUI_Activated_Ability_Name` is the effect. 22 links cross objects (company or team to member), so resolving them needs the company's member list, not only the object itself. 10 links in FoC point to no element at all: MAL_Swap_Weapon, Disruptor_Swap_Weapon, Pulse_Tank_Self_Destruct, Krayt_ and Vengeance_Self_Destruct, Interceptor_Full_Salvo. Each belongs to a self-contained Type (SWAP_WEAPONS, SELF_DESTRUCT, FULL_SALVO), so a dangling link must be harmless, not an error (data). Silri's GUI names have leading and trailing spaces, so trim names before matching.
- **Variant lists replace and do not merge** (data: 13 captured variants restate parent entries; 3 drop one). EAWEngine's nearest-declaring-definition lookup already gives this. Whether `Name`-keyed merging exists (as for other SubObjectList blocks) is unverified. The 4 FoC objects with *two* `<Abilities>` blocks (Crusader_Gunship, Kedalbe_Battleship, UL_Extort_Cash_L1/L2_Upgrade) suggest that both blocks count, because the second block holds the magnetically sealed armor and the space extort-cash effect that the upgrades need. This is unverified. Keep every block in decoded order, and do not keep only the first.
- **Element order is significant.** A Palpatine comment makes Force Lightning's position after Force Whirlwind a precondition for Whirlwind ever firing (comment), so automatic attacks are probably evaluated in list order with the first success winning. Decoders must preserve child order.
- **Sloppy values the reader must accept:** C float suffixes (`0.5f`, `30.0f`, `-1.0f`), tabs inside `Mod_Multiplier`, `|` and `,` list separators mixed in one value, newline-only lists, `Yes`/`True`/`true` booleans, case variants (`COMBAT_AUTOMATIC`, `power_to_weapons`), duplicate fields (8 Remove_Corruption elements repeat `Activation_Style` with the same value; Akbar's DEFEND entries have `Expiration_Seconds` 20 and 15, and which one wins is unknown, probably the last), and a missing comma in GameConstants `Activated_Sabotage_Ability_Names` (two names separated by a space only, so whitespace probably also separates). The EAW stray `Converted_To_Enemy_Die_Time_Seconds` inside `<Abilities>` must be tolerated as an unknown child with a diagnostic, not rejected.
- **FoC versus EAW.** FoC adds 33 classes (stealth, black market, corruption, Underworld units and heroes, the new Jedi powers) and 27 Types; it removes AREA_EFFECT_HEAL. The installed patched data differs from the FoC source in a handful of entries (one more POWER_TO_WEAPONS, Lucky_Shot on Luke_Jedi_XWing, Eat_Attack on Rancor and Night_Sister, recharge tweaks), so behavior audits must use the installed XML.
- **For M4 skirmish.** The minimum useful set: (1) a Unit_Ability runtime keyed by Type with toggle or timed state, `Recharge_Seconds`, `Expiration_Seconds`, and `Mod_Multiplier` stat modifiers (that covers SPREAD_OUT, POWER_TO_WEAPONS, SPOILER_LOCK, TURBO, DEFEND, SPRINT, STIM_PACK, INVULNERABILITY), exposed to Lua through `Activate_Ability`, `Try_Ability`, and `Is_Ability_Ready` with case-insensitive Type names; (2) the `Skirmish_Automatic` economy classes (`Income_Stream_Ability`, `Income_Stream_Mod_Ability`, `Enable_Radar_Ability`, structure self-repair through `Force_Healing_Ability`), with hardpoint- and upgrade-driven enabling (`Initially_Enabled`, `Enable_Ability`); (3) passive auras (`Combat_Bonus_Ability`, `Enhance_Defense_Ability`, `Battlefield_Modifier_Ability`) and `Redirect_Blaster_Ability`. How multipliers combine (product or sum) and the `Stacking_Category` rules are unknown and should be implemented as named, marked engine choices until observed. Mod_Multiplier and Unit_Ability timers are simulation state and belong in the state hash; the light, particle, and SFX fields are presentation only.

### Companies, squadrons, teams, transports, garrisons, spawning, and special systems

This family covers the tags that group objects into other objects, carry them, or make them appear. A selectable land unit (a `GroundCompany`, `HeroCompany`, `SlaveCompany`, or a `Props_Story` acting as one) lists its members in `Company_Units`; at runtime the members are grouped into team objects (`Container` kind, named by `Create_Team_Type`, at most `Max_Squad_Size` members each), and in galactic play the company travels in its `Company_Transport_Unit`. A space `Squadron` lists fighters in repeated `Squadron_Units` and places them with repeated `Squadron_Offsets`. Structures and carriers produce units through `Starting_/Reserve_Spawned_Units_Tech_0` pairs and indigenous spawn houses through `Spawn(ed)_Indigenous_*`; FoC adds garrisons, deployable units, and several special systems (stealth, conversion, vehicle theft, infection, gravity fields, dynamic transforms), while hero clashes, holstering, super weapons, and bombing runs exist in both games. EAWEngine reads only `Company_Units`, `Squadron_Units`, and `Squadron_Offsets`. Presence below is "both" when the tag is in the EAW source, the FoC source, and the installed patched data; "FoC only" when it is absent from the EAW source.

#### Companies and teams

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Company_Units` | containers 168 (GroundCompany 117, HeroCompany 43, SlaveCompany 8), Props_Story 10; 178 objects | type-name list (132) or single name (46); never repeats; members are mostly GroundInfantry (314 refs), Indigenous_Unit (257), GroundVehicle (100), Slave_Unit (72), heroes; up to 18 entries (e.g. 18 `Squad_Stormtrooper`) | The members the company becomes in a tactical battle, one entry per member, duplicates meaning several copies (data). Infantry members are usually `Squad_*` variants that carry a team locomotor (`LAND_TEAM_INFANTRY_LOCOMOTOR`) (data). Both. **engine:** `map_object_layout` and `visual_map_builder` expand a placed or spawned company into its members (split on commas and whitespace, capped at 64 with a diagnostic), all at the company's transform, flagged for formation; nested companies are not expanded. |
| `Company_Transport_Unit` | containers 124 (GroundCompany 75, HeroCompany 41, SlaveCompany 8), Props_Story 1; 125 objects | one type name; 17 distinct; targets TransportUnit 117, UniqueUnit 8 (e.g. `Gallofree_Transport` 46, `Imperial_Landing_Craft` 17, `Millennium_Falcon`, `Slave_I`) | The transport that carries the company on the galactic map and lands it in a ground battle (data, name). Hero companies name their personal ship. Both. |
| `Create_Team_Type` | containers 66 (GroundCompany 45, Squadron 9, SlaveCompany 8, HeroCompany 4), units 4, Props_Story 1; 71 objects | one name; 50 distinct; targets Container 68, Squadron 1 (`TIE_Prototype` names `Darth_Vader_TIE_Fighter_Squadron`); 2 empty | The team (container) object created to group the members, e.g. `Imperial_Stormtrooper_Squad` uses `Stormtrooper_Team` (data). A developer comment says targeted abilities need a team object to attach to (comment). Both. |
| `Max_Squad_Size` | containers 91 (GroundCompany 67, SlaveCompany 8, HeroCompany 8, Squadron 8), Props_Story 10; 101 objects | int 1-9 (1: 31, 5: 30, 3: 28) | Members per team: 18 `Squad_Stormtrooper` with 9 forms two teams of nine, which a developer comment on the AI side confirms in other words (comment, data). Both. |
| `Create_Team` | units 75 (Indigenous_Unit 18, GroundInfantry 18, SpaceUnit 17, GenericHeroUnit 8, Slave_Unit 6, UniqueUnit 6, HeroUnit 2); 76 uses | bool, spelled Yes 42 / True 21 / No 11 / False 2; repeats once (`Noghri_Indigenous`, Yes twice) | Whether a unit spawned on its own is wrapped in a team object (name, data); fighters and indigenous units set it. Comments only mark it as very important. Both. |
| `ContainerArrangement` | Container 41 | `Dartboard` 38, `Rows` 3 (Stormtrooper and field-commander teams) | Formation pattern of team members inside the container (name, data). Both. |
| `Is_Force_Sensitive` | HeroCompany 9, HeroUnit 9, GroundInfantry 1, Props_Story 1; 20 objects | always Yes | Marks Force users (Vader, Mara Jade, Palpatine, Katarn ...) on both the hero and its company, presumably for Force-detection and Force-related targeting rules (name). Both. |
| `Is_Stealth_Company` | HeroCompany 27; 29 uses | bool Yes 18 / No 11; repeats in `IG88_Team` and `Bossk_Team` with Yes then No | The company (smugglers, probe droids, bounty hunters) is hidden from the enemy on the galactic map (name). Both. |
| `Required_Orbiting_Units` | GroundCompany 3 (Dark Trooper P1-P3 companies) | `Arc_Hammer` | The company can only be fielded while the named unit is in orbit (name, data). FoC only. |
| `Share_Damage_With_Teammates` | HeroUnit 2 (`Droid_R2D2`, `Droid_C3P0`) | Yes | Damage to one member is shared across the team (name). Both. |
| `Redirect_Damage_To_Teammates` | UniqueUnit 2 (`Luke_XWing`, `TIE_Prototype`) | Yes | Damage to this unit goes to its team (squadron) mates instead, keeping the hero alive (name). Both. |
| `Respawn_Whole_Team_When_Killed` | HeroUnit 2 (R2-D2, C-3PO), UniqueUnit 1 (`Luke_XWing`) | Yes | The hero respawns with its whole team rather than alone (name). Both. |

#### Space squadrons

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Squadron_Units` | Squadron 29; 51 uses | type-name list (48) or one name (3); repeats in 19 objects (max 3); targets SpaceUnit 103, UniqueUnit 34 | The fighters of the squadron; repeated fields continue the list (`Rebel_X-Wing_Squadron`: "X-Wing, X-Wing" twice, then "X-Wing") (data). Both. **engine:** concatenated across repeats, each member placed at its offset. |
| `Squadron_Offsets` | Squadron 26, Container 4; 142 uses | vector3, repeats once per member (max 7); 22 distinct, e.g. `60,0,0`, `0,±30,0`, `-60,±60,0`; z always 0 | Member position in the squadron's local frame, +X ahead (data, engine). Both. **engine:** the n-th offset belongs to the n-th member, rotated a quarter turn into the model frame; a missing offset puts the member at the origin with a diagnostic. |
| `Squadron_Formation_Error_Tolerance` | Squadron 26, Container 4; 30 objects | float 25.0 (25) or 35.0 (5) | How far a member may stray from its formation slot before correcting (name). Both. |
| `Number_per_Squadron` | SpaceUnit 42, UniqueUnit 8, Props_Story 1; 51 objects | int 1-10 (1: 32, 5: 8) | Fighters per squadron as counted by the unit itself, 1 for corvettes and frigates; probably used where the squadron is not expanded (galactic, auto-resolve) (name, unknown use). Both. |
| `Squadron_Capacity` | SpaceUnit 45, UniqueUnit 11, Props_Story 2; 58 objects | int 0-100 (0: 34, 10: 8, 40: 8) | How many fighter squadrons a ship or station can carry in a fleet; 0 on fighters (name, data). Both. |
| `Is_Bomber` | SpaceUnit 40, Squadron 26, UniqueUnit 9, Container 3, Props_Story 1; 79 objects | bool, mostly lowercase `no` 66 / `yes` 12 / `Yes` 1 | Marks bomber craft and bomber squadrons (name); likely feeds bombing runs and AI roles. Both. |

#### Transports and landing

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Transport_Capacity` | SpaceUnit 21, UniqueUnit 3; 24 objects | int 0-100 (4: 9, 8: 8, 12: 3) | Ground-unit capacity of a space ship (corvettes 4, frigates 8 ...) used when carrying companies on the galactic map (name, data). Both. |
| `Landing_Transport_Variant` | TransportUnit 8, UniqueUnit 8; 16 objects | one type name, the `*_Landing` variant (15 distinct) | The object used to show this transport landing in a ground battle (data). Both. |
| `Use_Transported_Object_Bounds_For_Landing_Z` | TransportUnit 1 (`Imperial_ATAT_Barge_Landing`) | false | Whether the landing height is derived from the carried unit's bounds (name); the barge carries an AT-AT below it. Both. |
| `Exit_Door_Angle_Degrees` | TransportUnit 8, GroundVehicle 1; 9 objects | float 0, 90, 180 | Direction, relative to the transport, in which carried units leave (name). Both. |
| `Exit_Door_Distance` | TransportUnit 8, GroundVehicle 1 | float 0 (written `0.0` 4 and `00.0` 4) or 50 | Distance from the transport at which carried units appear (name). Both. |
| `Stay_In_Transport_During_Ground_Battle` | HeroUnit 30, GenericHeroUnit 16, UniqueUnit 1; 48 uses | bool No 31 / Yes 17; repeats in `Mara_Jade` (Yes then No) | The hero does not land in a ground battle (smugglers, probe droids, Yes) (name, data). Both. |
| `Display_Contained_Hero_Grab_Bars` | UniqueUnit 3 (`Slave_I`, `Houndstooth`, `IG-2000`) | True | Shows the carried hero's health bar on the ship (name). Both. |
| `Can_Contain_Heroes_During_Ground_Battle` | SpecialStructure 26 (cantinas, communications arrays ...) | Yes | Heroes can enter and stay inside this structure in a ground battle (name). Both. |

#### Garrisons

All FoC only.

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Garrison_Category` | Container 41, HeroUnit 19, GroundVehicle 14, GroundInfantry 7, GenericHeroUnit 2, others 3; 88 objects, 90 uses | `Infantry` 77, `Vehicle` 9, `Infantry, Vehicle` 4; repeats with the same value in `Underworld_Saboteur` and its demo variant | On a unit or team: what kind of occupant it is; on a host (vehicle, bunker): which kinds it accepts (data). |
| `Garrison_Value` | GroundVehicle 7, Mobile_Defense_Unit 1 | int 1 or 2 | Slots the unit takes inside a garrison host, vehicles 2 (name, data). |
| `Num_Garrison_Slots` | GroundVehicle 3, SpecialStructure 1, UniqueUnit 1 | int 4, 5, 10 | Occupant capacity of the host (name). |
| `Num_Garrison_Guns` | SpecialStructure 1 (`Garrison_Bunker_Template`) | 8 | Number of firing positions of the bunker (name). |
| `Garrison_Bone_Names` | GroundVehicle 3, SpecialStructure 1, UniqueUnit 1 | bone list (`b_garrison_00` .. `_03`) | Bones where occupants enter, exit, or fire from (name). |
| `Garrison_Enter_Dist` | same 5 hosts | 0 | Distance at which an approaching unit enters (name). |
| `Garrison_Exit_Dist` | same 5 hosts | 0, or 50 on the bunker | Distance from the host at which occupants are released (name). |
| `Garrison_Radius` | GroundVehicle 3, SpecialStructure 2, UniqueUnit 1 | int 5-30 | Radius around the host used for entering (name). |
| `Spawn_Garrison_On_Load` | SpecialStructure 13 (barracks, palaces, infiltrator facility) | True | The structure's starting spawned units (see below) are created inside it when the map loads (name, data). In both games' data, not FoC only. |

#### Units produced by structures and carriers

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Starting_Spawned_Units_Tech_0` | StarBase 17, SpecialStructure 15, SpaceUnit 4, SecondaryStructure 4, Props_Story 2, UniqueUnit 2; 44 objects, 105 uses | "type, count" pair; repeats once per produced type (max 5); targets Squadron 49, SpaceUnit 23, GroundCompany 16, GroundVehicle 11 ... | Units the object launches at the start and keeps active at once ("2 squadrons at a time") (comment). Only `Tech_0` exists in the shipped data; the repeats are per unit type, not per tech level. Both. |
| `Reserve_Spawned_Units_Tech_0` | the same objects (42; 102 uses) | "type, count", count -1 meaning unlimited (comment) | Replacement stock after the starting units die; -1 keeps spawning until the fighter bay is destroyed, a missing reserve means spawn once (comment). Both. |
| `Spawned_Squadron_Delay_Seconds` | StarBase 17, SpecialStructure 13, SpaceUnit 4, SecondaryStructure 4, others 4; 42 objects | int 0-60 (10: 16, 25: 8) | Time between spawned replacements (name). The dump's attached "-1 means no limit" comment belongs to the adjacent reserve line. Both. |
| `Spawned_Squadron_Location_Bones` | SpecialStructure 13, SecondaryStructure 4 | `SPAWN_00` | Bone at which ground structures release their units (name). Both. |
| `Spawned_Squadron_Location_Flyout_Distances` | same 17 | float 110-250 | Distance a released unit travels from the bone before it is free (name). Both. |
| `Fighter_Bay_Flyout_Distance` | HardPoint 6 (station fighter bays) | float 400 (5) or 800 | Distance launched fighters fly out from a fighter-bay hardpoint; a comment records an older 1800 (comment, name). Both. |
| `Tactical_Respawn_Time_In_Secs` | SecondaryStructure 4, SpaceBuildable 2 | int 30-120 | Time after which a destroyed skirmish secondary structure rebuilds (name). Both. |
| `Spawn_Planet` | HeroUnit 14, Props_Story 1; 15 objects | planet name (Coruscant 5 ...), 2 empty | Galactic planet where the hero appears or respawns (name). Both. |

#### Indigenous spawners

All both games; on GroundStructure 49, Props_Temperate 2, Prop_Desert 1, Props_Story 1 (53 objects) unless stated.

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Spawned_Indigenous_Units_Type` | GroundStructure 50 + props 4 (54) | one or a list of types (43 distinct; Indigenous_Unit 93 refs) | Unit types the spawn house produces, list entries chosen among (name, data). |
| `Spawned_Indigenous_Pack_Type` | GroundStructure 40, Props_Story 1 | GroundCompany name (29 distinct), 2 empty | Company spawned when spawning in packs (data). |
| `Spawn_Indigenous_Units_In_Packs` | 53 | Yes 45 / No 8 | Spawn whole pack companies instead of single units (name). |
| `Spawned_Indigenous_Units_Quantity` | 53 | int 1-20 (10: 37) | Maximum alive population of the spawner (name). |
| `Spawned_Indigenous_Units_Delay_Seconds` | 53 | float 10-60 (20: 47) | Time between spawns (name). |
| `Spawn_Indigenous_Units_Chance` | 53 | float written with a C suffix: `1.0f` 52, `0.9f` 1 | Probability that a spawn happens (name). |
| `Spawn_Indigenous_Units_Radius` | 53 | float 50-200, all with `f` suffix (`90.0f` 35) | Radius around the spawner where units appear (name). |

#### Deploying units

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Deploys` | GroundVehicle 24, GroundInfantry 12, HeroUnit 10, Indigenous_Unit 4, LandBombingUnit 3, others 3; 56 objects | No 44 / Yes 12 | The unit has a deployed state it switches into (name, data). Both. |
| `Auto_Deploys` | GroundVehicle 4, UniqueUnit 1 | No 3 (AT-AT, AT-AA, Veers) / Yes 2 (Destroyer Droid, MAL) | Deploys on its own when stopping or engaging (name). Both. |
| `Deployment_Anim_Rate` | GroundVehicle 6, Mobile_Defense_Unit 1 | float 0.5-3.0 (2.0: 4) | Playback rate of the deploy animation (name). Both. |
| `DeployedBehavior` | Mobile_Defense_Unit 1 (abstract) | `TACTICAL_BUILD_OBJECTS` | Behavior added while deployed (mobile defense units become a build pad) (name, data). FoC only. |
| `Deployed_Walk_Speed` | GroundVehicle 1 (`Destroyer_Droid`) | 2.5 | Movement speed while deployed (name). FoC only. |
| `Deployed_Walk_Anim_Speed` | GroundVehicle 1 (`Destroyer_Droid`) | 2 | Walk animation rate while deployed (name). FoC only. |
| `Max_Dist_Walk_While_Deployed` | GroundVehicle 1 (`Destroyer_Droid`) | 150.0 | Farthest it walks before undeploying (name). FoC only. |
| `Shielded_Only_When_Deployed` | GroundVehicle 1 (`Destroyer_Droid`) | Yes | Shield active only while deployed (name). FoC only. |
| `Must_Face_Target_To_Attack_When_Deployed` | GroundVehicle 2 | No (Dark Trooper III), Yes (Destroyer Droid) | Deployed attack requires facing (name). FoC only. |
| `Deployed_Max_Attack_Distance_Multiplier` | GroundVehicle 1 (`MAL_Rocket_Vehicle`) | 1.0 | Range multiplier while deployed (name); compare the global `Garrisoned_Max_Attack_Distance_Multiplier` 1.5. FoC only. |
| `Deployed_Turret_Rotate_Extent_Degrees` | GroundVehicle 1 (`MAL_Rocket_Vehicle`) | 360 | Turret yaw limit while deployed (name). FoC only. |
| `Deployed_Turret_Elevate_Extent_Degrees` | GroundVehicle 1 (`MAL_Rocket_Vehicle`) | 180 | Turret pitch limit while deployed (name). FoC only. |
| `Turret_Deployed_Rest_Angle` | GroundVehicle 1 (`SPMAT_Walker`) | vector3 `0, -45, 0` | Turret rest orientation while deployed (name). Both. |
| `Custom_Hard_YExtent_Deployed` | GroundVehicle 2 (`MPTL`, `MAL_Rocket_Vehicle`) | 24.0 | Hard footprint Y extent while deployed (name). Both. |
| `Fire_Weapon_When_Deployed` | GroundVehicle 7, Mobile_Defense_Unit 1 | Yes 6 / No 2 | The unit's own weapon fires while deployed (name). Both. |
| `Fire_Weapon_When_Undeployed` | same 8 | No 5 / Yes 3 | The unit's own weapon fires while not deployed (name). Both. |
| `Fire_When_Deployed` | HardPoint 13 | Yes 9 / No 4 | Hardpoint-level equivalent: the hardpoint fires while its owner is deployed (name). Both. |
| `Fire_When_Undeployed` | HardPoint 13 | No 9 / Yes 4 | The hardpoint fires while its owner is undeployed (name). Both. |

#### Holstering

All on HeroUnit 6 (Palpatine, Obi-Wan, Vader, Vader expansion, Luke, Yoda) and Props_Story 1; both games.

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Holster_Weapon_Bone_Name` | 7 | `b_saber` | Bone of the weapon that is drawn and put away (data). |
| `Holster_Drawn_Bone_Translation` | 7 | vector3, always `0, 0, 0` | Weapon-bone offset when drawn (name). |
| `Holster_Holstered_Bone_Translation` | 7 | vector3 along Y, about ±4.4 to ±5.8 | Weapon-bone offset when holstered, which hides the blade inside the hilt (name, data). |
| `Holster_Transition_Time_In_Secs` | 7 | 0.17 (6) or 0.2 | Duration of the draw and holster move (name). |
| `Holster_Minimum_Drawn_Time_In_Secs` | 7 | 15.0 (6) or 0.5 | Minimum time the weapon stays drawn (name). |
| `Holster_Disable_Engine_Loops` | 7 | Yes | Stops the weapon's looping sound while holstered (name). |

#### Hero clash

`Hero_Clash` objects in `HeroClash.xml` (3 objects; a separate file registered with its own type, not a game-object file). Both games. The objects also hold nested action and damage tags (`Action_Object`, `Combat_Distance`, `Damage_Amount`, `Damage_Percentage`, `Attack_*`, speech) that are outside this dump.

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Clash_Type` | 3 | `Melee_Duel` 1 (Vader-Obi-Wan), `Action_Driven` 2 | Scripted duel versus an action sequence (name, data). |
| `Clash_Range` | 3 | 100.0, or 0.0 for the two action clashes | Distance at which the two heroes start the clash (name). |
| `Involved_Hero_Types` | 3 | list of 2-3 HeroUnit names | Heroes that trigger the clash (data). |
| `First_Hero_Type` | 3 | HeroUnit name | Hero in the first role (data). |
| `Second_Hero_Type` | 3 | HeroUnit name | Hero in the second role; a comment says the Boba Fett versus Han Solo clash replaced a vehicle-thief one and is broken, so disabled (comment). |
| `First_Hero_Win_Exchange_Chance` | 1 | 0.40 | Chance for the first hero to win one exchange, not the whole clash; the remainder after both chances is a draw (comment). |
| `Second_Hero_Win_Exchange_Chance` | 1 | 0.30 | Same for the second hero (comment). |
| `First_Hero_Damage_Multiplier` | 1 | 2.5 | Multiplier on the damage the first hero deals per exchange (name). |
| `Second_Hero_Damage_Multiplier` | 1 | 1.0 | Same for the second hero (name). |
| `First_Hero_Draw_Anim_Type` | 1 | `HC_DRAW` | Animation type for a drawn exchange (data). |
| `First_Hero_Win_Anim_Type` | 1 | `HC_WIN` | Animation type for a won exchange (data). |
| `First_Hero_Lose_Anim_Type` | 1 | `HC_LOSE` | Animation type for a lost exchange (data). |
| `First_Hero_Conversation_Anim_Type` | 1 | `IDLE` | Animation during the pre-duel conversation (name). |
| `Second_Hero_Draw_Anim_Type` | 1 | `HC_DRAW` | As above, second hero. |
| `Second_Hero_Win_Anim_Type` | 1 | `HC_WIN` | As above, second hero. |
| `Second_Hero_Lose_Anim_Type` | 1 | `HC_LOSE` | As above, second hero. |
| `Second_Hero_Conversation_Anim_Type` | 1 | `IDLE` | As above, second hero. |

#### Presence and glory

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Presence_Range` | GroundVehicle 4 (AT-AT death clones), HeroUnit 3 (Palpatine, Vader); 7 objects | float 100 or 200 | Range within which this object changes other units' animations (comment). Both. |
| `Presence_Induced_Animations` | GroundInfantry 5 | list: an animation name (`Attention`, `Celebrate`) followed by the object types that trigger it, spread over several lines | Infantry play the named animation when a listed type is within its presence range (stormtroopers stand to attention near Palpatine or Vader; rebels celebrate near a dying AT-AT) (data). Both. |
| `Glory_Cinematics` | UniqueUnit 4, GroundVehicle 3, SpaceUnit 3; 10 objects | `.tec` camera file or list, 3 empty | Death cameras played for a glory kill (AT-AT and capital death clones) (data). Both. |

#### Stealth, conversion, theft, infection

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Stealth_Capable` | HeroUnit 21, GroundInfantry 12, GroundVehicle 1, Props_Story 1; 35 objects | No 31 / Yes 4 | The unit can enter tactical stealth (name). Present in EAW source too. |
| `Conversion_Ability_Changes_To_Enemy` | GroundInfantry 12 | GroundInfantry name, the `*_Converted_To_Enemy` variant | The type the unit becomes when converted by the corrupt-to-enemy ability (comment). Present in EAW source (1 file) and FoC. |
| `Converted_To_Enemy_Die_Time_Seconds` | GroundVehicle 20, UniqueUnit 1 | int 60 (15) or 45 (6) | Time a converted or captured vehicle survives before it dies (name). The attached comment belongs to a neighbouring tag. Both. |
| `Vehicle_Thief_Inside_Clone` | GroundVehicle 17, UniqueUnit 3 | GroundVehicle name, the `*_Captured` variant (18 distinct) | The type a vehicle becomes when a vehicle thief takes it over (data). The dump's comment belongs to a neighbouring `Damage` tag. Both. |
| `Causes_Infection` | Projectile 1 (`Infection_Projectile`) | Yes | The projectile infects what it hits (name); GameConstants has a `Damage_Infection` damage type. FoC only. |

#### Field effects

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Gravity_Control_Field_Range` | SpecialStructure 2 | 100000 (stock `Ground_Gravity_Generator`, whole map, comment), 3000 (Underworld variant) | Radius of the generator's field (comment). Both. |
| `Gravity_Control_Field_Slow_Fraction` | SpecialStructure 1 (`U_Ground_Gravity_Generator`) | 0.9 | Speed reduction inside the field (name). FoC only. |
| `Gravity_Control_Field_Effect` | SpecialStructure 1 | Particle `Repulsor_Jammer_Effect` | Effect shown on affected units (name). FoC only. |
| `Is_Affected_By_Gravity_Control_Field` | GroundVehicle 10, GroundInfantry 1, Mobile_Defense_Unit 1, Props_Generic 1, UniqueUnit 1; 14 objects | Yes | The unit is repulsorlift-driven and slowed by gravity fields (comment). Both. |
| `Disable_Force_Abilities_Range` | GroundStructure 3 (ysalamiri tree and cage, Rebel mobile shield) | int 150 or 250 | Radius in which Force abilities are disabled (name). FoC only. |
| `Passive_Missile_Shield_Radius` | GroundStructure 2, SpaceBuildable 1 | float 150-1000 | Radius of a passive missile defense around the structure (name). FoC only. |
| `Bombing_Run_Prevention_Radius` | SpecialStructure 2 (Empire and Rebel shutter shields) | int 800, 1000 | Area where bombing runs cannot be called (name). Both. |

#### Dynamic transform

All FoC only, on HeroUnit 3 (`Cuddles_The_Rancor` and death clones), Particle 1 (`Shield_Flare_Effect`), Props_Story 1 (`UMP_CINE_HOLOJABBA`).

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Dynamic_Transform_Delay_Seconds` | 5 | 0-0.5 | Delay before the transform starts (name). |
| `Dynamic_Transform_Seconds` | 5 | int 1-120 | Duration of the scale and color change (name). |
| `Dynamic_Transform_Start_Scale` | 5 | 0.01-1.75 | Scale at the start (the summoned rancor grows from 0.01) (name, data). |
| `Dynamic_Transform_End_Scale` | 5 | 0.8-2.0 | Scale at the end (name). |
| `Dynamic_Transform_Start_Color` | 5 | vector4 RGBA, can exceed 1 (`6, 1, 1, 1`) | Tint at the start (name). |
| `Dynamic_Transform_End_Color` | 5 | vector4 RGBA | Tint at the end (name). |

#### Super weapons and bombing runs

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Is_Special_Weapon_In_Space` | SpecialStructure 2 (ground ion cannon, hypervelocity gun) | Yes | The ground structure fires into the space battle above (name, data). Both. |
| `Special_Weapon_Index` | SpecialStructure 3 | 0, 1, 2 | Index of the special weapon (ion cannon 0, HV gun 1, magnepulse 2) (data). Both. |
| `Special_Weapon_Target_Action_Index` | SpecialStructure 3 | 0, 1, 2 | Targeting action used: HV gun 0, ion cannon 1, magnepulse 2 (comment). Both. |
| `Special_Weapon_Valid_Targets` | SpecialStructure 2, SpaceUnit 1 (`Buzz_Droids`) | `\|`-separated category list | Target categories the weapon may hit (data). Both. |
| `Allows_Special_Weapon_Use` | HardPoint 2 (station comm arrays) | True | Losing this hardpoint disables the faction's ground super weapon for all its players (comment). FoC only. |
| `Is_Skirmish_Tactical_Super_Weapon` | UpgradeObject 3 | true | The upgrade unlocks a skirmish super weapon (name). Both. |
| `Is_Super_Weapon_Killer` | UniqueUnit 2, HeroUnit 1 (Luke, his X-wing, Wedge's X-wing) | Yes | The unit can destroy the Death Star (comment). Both. |
| `Lobbing_Superweapon_Chargeup_Frames` | SpecialStructure 1 (`Ground_Magnepulse_Cannon`) | 90 | Charge-up time in frames before the shot (name). Both. |
| `Lobbing_Superweapon_Chargeup_Particle` | same | Particle name | Charge-up effect (name). Both. |
| `Lobbing_Superweapon_Chargeup_Particle_Bone_Name` | same | `attack_cool_bone` | Bone the charge-up effect attaches to (name). Both. |
| `TSW_Start_Distance_From_Target` | SpaceUnit 1 (`Death_Star`) | 8000.0 | Tactical super weapon sequence (Death Star destroying a planet prop): starting distance from the target (comment). Both. |
| `TSW_Attack_Distance_From_Target` | `Death_Star` | 5800.0 | Distance at which it fires (name). Both. |
| `TSW_Start_Pos_Match_Targets_Z` | `Death_Star` | Yes | Start height matches the target's (name). Both. |
| `TSW_Z_Adjust_Relative_To_Target_Pos` | `Death_Star` | 1200.0 | Height offset relative to the target (name). Both. |
| `TSW_Power_Up_Countdown_Seconds` | `Death_Star` | 9.0 | Countdown before firing (name). Both. |
| `TSW_Attack_Anim_Duration_Seconds` | `Death_Star` | 2.5 | Duration of the firing animation (name). Both. |
| `TSW_Explosion_Debris_Creation_Frame_Delay` | `Death_Star` | 33 | Frames between the hit and the planet debris (name). Both. |
| `TSW_Post_Destruction_Wait_Frames` | `Death_Star` | 350 | Wait after destruction; a comment calls an associated value unused and relates timing to music length at 30 frames per second (comment). Both. |
| `Land_Bomber_Type` | SpaceUnit 4 (TIE bomber, Y-wing, B-wing, Skipray) | LandBombingUnit name | The bombing-run unit this space bomber provides to a ground battle (data). Both. |

#### Beacons, abilities, and planet leftovers

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Beacon_Lifetime_In_Secs` | MiscObject 1 (`R_Multiplayer_Beacon`) | 10.0 | How long a multiplayer map beacon lasts (name). Both. |
| `Beacon_Radar_Map_Event_Name` | MiscObject 1 | `Default_Beacon_Placement` | Radar-map event shown when the beacon is placed (name). Both. |
| `Ability_Names` | Item 10 (`BlackMarketItems.xml`) | ability name or list | Abilities a black-market item unlocks (data). FoC only. |
| `Special_Ability_Name` | HardPoint 8 (supply docks, comm arrays, palace) | ability name | Ability granted by the hardpoint while it stands (income bonus, radar) (data). Both. |
| `Hero_Ability` | UniqueUnit 1 (`Sundered_Heart`) | empty | Unknown; one empty use, probably ignored. Both. |
| `Force_Strength` | Planet 64 | 0.0 everywhere | Unknown planet Force setting (name); the uniform value suggests it is unused. Both. |
| `The_Force` | Planet 64 | `Neutral` everywhere | Unknown planet Force alignment (name); uniform, probably unused. Both. |

#### Findings

- **Stale research note.** `docs/compatibility-research.md` (around line 139) still says the engine takes the last `Squadron_Units` occurrence. Its later "Squadron lists" bullet, and the code, concatenate the repeats. The earlier paragraph should be corrected.
- **Squadrons without their own offsets.** `Darth_Vader_TIE_Fighter_Squadron`, `TIE_Fighter_Squadron_E3`, and `TIE_Bomber_Squadron_E3` have `Squadron_Units` but no `Squadron_Offsets`. In the Vader case the offsets are on the team container (`Darth_Vader_TIE_Fighter_Container`, 6 offsets) named by `Create_Team_Type`. EAWEngine stacks these members at the origin with a diagnostic. It should fall back to the `Create_Team_Type` container's offsets; the E3 squadrons need a formation default.
- **Offsets on containers.** `Red_Squadron_Container`, `Darth_Vader_TIE_Fighter_Container`, `Rogue_Squadron_Space_Container`, and `StarViper_Team` carry offsets without `Squadron_Units`. They are layout templates for the team, not expandable lists.
- **Companies become teams, which the engine skips.** A company is split into teams of `Max_Squad_Size` (18 `Squad_Stormtrooper` with 9 gives two teams of nine; `Rebel_Infantry_Squad` gives 15 in teams of 5). The teams use the `Create_Team_Type` container's movement, arrangement (`ContainerArrangement` Dartboard or Rows), and garrison category. EAWEngine expands only `Company_Units`, so selection grouping and team formations are missing. `Create_Team_Type` and `Max_Squad_Size` are the next tags to read. GameConstants `ShouldInfantryTeamsSplitAcrossFormations` (true) is the related global.
- **Contradictory duplicates.** `Is_Stealth_Company` is Yes then No in `IG88_Team` and `Bossk_Team`, and `Stay_In_Transport_During_Ground_Battle` is Yes then No in `Mara_Jade`. Which one the original honours is unverified, presumably the last; the engine's catalog needs one stated rule for single-valued tags. `Create_Team` (Noghri) and `Garrison_Category` (2 saboteurs) repeat harmlessly.
- **Float values with a C suffix.** `Spawn_Indigenous_Units_Chance` (`1.0f`) and `Spawn_Indigenous_Units_Radius` (`90.0f`) carry the suffix on all 53 objects. A strict `from_chars` parser rejects them, so a reader for these tags must accept a trailing `f`.
- **Boolean spellings.** Booleans are written Yes/No/True/False in any case (`Is_Bomber` mostly lowercase `no`/`yes`). `Exit_Door_Distance` writes `00.0`.
- **Spawn tech levels.** Only `_Tech_0` appears for spawned units, 105 starting and 102 reserve uses in the dump (a raw grep of the installed data counts 107 and 102). Each repeat is one "type, count" pair, -1 meaning an unlimited reserve. A reader must collect all repeats, not keep the last.
- **Same idea at two levels.** Units use `Fire_Weapon_When_(Un)Deployed`, hardpoints use `Fire_When_(Un)Deployed`.
- **Misattributed comments in the dump.** The dump attaches comments from neighbouring lines to some tags: `Vehicle_Thief_Inside_Clone`, `Converted_To_Enemy_Die_Time_Seconds`, `Spawned_Squadron_Delay_Seconds`, and the turret-priority comment on `Garrison_Category`. Those meanings above are not taken from them.
- **Present in EAW, not FoC additions.** Several systems that look FoC-specific are in the EAW source XML too: `Conversion_Ability_Changes_To_Enemy`, `Vehicle_Thief_Inside_Clone`, `Stealth_Capable`, `Is_Stealth_Company`, `Gravity_Control_Field_Range`, `TSW_*`, and the indigenous spawn tags. Garrisons, deployed movement, dynamic transforms, infection, and `Required_Orbiting_Units` are FoC only.
- **Likely ignored.** `Hero_Ability` (one empty use) and the planet tags `Force_Strength` and `The_Force` (uniform on all 64 planets) are probably ignored. `HeroClash.xml` defines `ObiWan_BobaFett` twice: the second, Boba Fett versus Han and Chewbacca, is the clash a comment marks as broken and disabled.
- **Related globals in GameConstants.** `Garrisoned_Max_Attack_Distance_Multiplier` 1.5, `Indigenous_Spawn_Destruction_Reward` 500, `Min/Max_Bombing_Run_Interval_Seconds` 120, `Bombing_Run_Reduction_Per_Squadron_Percent` 0, `BeaconPlaceDelay` 5000, `Default_Hero_Respawn_Time` 360, and `AutoResolveTransportLosses` 0.333.
- **Spawning in battles.** `Props_Story` objects with `Company_Units` (the prisoner companies and `UM05_Red_Guard_Company`) are expanded by the map layout through the `Company_Units` presence check. Spawning in battles depends on `spawn_catalog`'s kind list, which does not include `props_story`.

### Production, economy, tech, galactic play, AI, visibility, and identity

This family holds the tags that place an object in the strategic game rather than in tactical physics: who owns it, what it costs and when it may be built (galactic `Build_*` against tactical `Tactical_Build_*`, with separate campaign and multiplayer variants), base and upgrade chains, population, the planet definitions (planets are `Planet` game objects in Planets.xml), FoC's corruption, bribery, slicing and black market economy, capture and cash points, victory relevance, AI evaluators, and fog-of-war reveal. Most tags are flat scalars or booleans, a few are faction-headed lists; booleans are written loosely (`Yes`/`yes`/`True`/`true`/`No`/`false` all occur for one tag). EAWEngine reads only `Affiliation`, `AI_Combat_Power`, `Lua_Script` and (unused so far) `Galactic_Model_Name`. Counts are objects (uses when repeats differ) in the installed patched FoC data unless noted; "FoC only" means absent from the EAW source data.

#### Faction, identity, and scripts

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Affiliation` | 1351 objects: structures 446, units 316, props 204, containers 193, upgrades 192 | faction name, or comma list of factions (52 uses); 27 distinct; Empire 362, Rebel 351, Underworld 252, Neutral 196, Pirates 62, Hutts 48, Hostile 28; 3 objects repeat it | Owning or permitted factions; a list lets several factions own or build the type (data). Engine: the first listed faction's `Color` tints team-color masks in previews; the spawn catalog keeps the whole list as a faction filter. |
| `Faction` | 10 black-market `Item` objects | Empire 5, Rebel 5 | Faction whose technology the black-market item belongs to (data). FoC only. The engine's `Faction` literal is the XML definition type name, not this tag: engine_reads is a false positive. |
| `Faction_Anim_Subindex` | 4 markers (reinforcement-point pedestals), 25 uses | repeats: one "Faction, index" pair per faction (indices 0..3) | Maps the owning faction to an animation sub-index so the pedestal shows its owner (data). |
| `Political_Faction` | 12: heroes 4, space units 3, story props 2, unique 2, container 1 | Empire 8, Rebel 3, one empty | Faction credited with the object's political control (name); comments say heroes should use a political-control bonus ability instead (comment). |
| `Ship_Class` | 199: companies 91, space units 66, unique 21, slave companies 8, transports 8 | 15 words: infantry 68, corvette 20, frigate 20, tank 18, fighter 16, capital_ship 14, walker 9, Transport 8, capital 7, artillery, bomber, super | Broad class used for display and AI grouping (name); values coincide with priority-set names (data). Both `capital` and `capital_ship`, and case variants (`Fighter`, `Corvette`, `Capital`) occur. |
| `Moniker` | 2 space units (Alliance assault frigate and its FoC variant) | a `.txt` file name | Unknown; perhaps a name list for individual ships (name). |
| `Is_Named_Hero` | 85: hero units 30, hero companies 27, unique units 20 | Yes 77, No 7, true 2 | Marks a named (story) hero as opposed to a generic one (name). |
| `Is_Generic_Hero` | 30: generic hero units 16, hero companies 14 | Yes only | Marks generic heroes (smugglers, probe droids, field commanders) (data). |
| `Can_Be_Only_One` | 18 hero companies | true only | Only one instance may exist (name). |
| `Is_Homogeneous` | 10: hero companies 6, companies 2, squadrons 2 | No 7, Yes 3 | Whether a container's members are all one type (name). |
| `Unique_Ground_Unit` | 1 hero (Veers) | `Veers_AT_AT_Walker` | The unique unit the hero appears as in land battles (data). |
| `Unique_Space_Unit` | 3 heroes | unique ship names (TIE prototype, Executor, Accuser) | The unique ship the hero appears as in space battles (data). |
| `Loads_When_Faction_Present` | 3 ground structures (spawn houses, sandcrawler) | True only | Loaded only when a given faction is in the battle (name). |
| `Lua_Script` | 79 objects (80 uses): heroes 20, story props 12, space units 7, unique 7, generic heroes 6, markers 5 | script module name; 60 distinct; 4 empty | Game-object script under Data/Scripts (data). Engine: stored per type; the battle starts that script for each entity of the type when it appears (`battle_scripts.cpp`). |
| `Execute_Script_On_Type` | 8 generic hero units | true only | Runs the object script once for the type rather than per instance (name; unverified). |
| `GalacticBehavior` | 25: heroes 22, space units 2, story prop 1 | `SELECTABLE` only | Behaviors added only in galactic mode (name). |

#### Galactic production, limits, and tech

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Build_Cost_Credits` | 281: containers 107, structures 107, units 63 | int 0..24000; 0 in 75 | Galactic build price (name); some objects use a value to block galactic building (comment). |
| `Build_Time_Seconds` | 290: containers 130, structures 98, units 58 | int 0..310; 0 in 93 | Galactic build time (name); 310 s on the Death Star is timed to a five-minute voice-over (comment). |
| `Build_Time_Reduced_By_Multiple_Factories` | 59: companies 55, tech buildings 4 | Yes 47, No 12 | Several factories on a planet shorten the build (name). |
| `Build_Initially_Locked` | 163 (169 uses): structures 71, containers 76, units 16 | Yes 119, No 50 | Not buildable until unlocked by story, slicing or research (name). |
| `Build_Can_Be_Unlocked_By_Slicer` | 152: containers 74, structures 65, units 13 | No 125, Yes 27 | A slicer can unlock the locked build (name). Present in EAW source data too. |
| `Build_Limit_Current_For_All_Allies` | 176: upgrades 164, unique 6, containers 5 | 1 only | At most one alive at a time per team (name). |
| `Build_Limit_Current_Per_Player` | 50: hero companies 30, upgrades 9, unique 7, tech buildings 4 | 1 (43) or -1 (7) | Concurrent limit per player; -1 means unlimited (data). |
| `Build_Limit_Lifetime_For_All_Allies` | 32: hero companies 29, unique 3 | -1 only | Lifetime limit per team; -1 unlimited (data). |
| `Build_Limit_Lifetime_Per_Player` | 17: unique 9, containers 6 | 1 (8) or -1 (9) | Number of times a player may ever build it (name). |
| `Build_Max_Instances_Per_Planet` | 49 special structures | -1 (22), 1 (25), 2 (2) | Per-planet cap; -1 unlimited (data). |
| `Build_Requires_Initial_Placement` | 5 special structures (cantinas, Hutt palace) | true only | Cannot be built, only placed by the map or story (name). |
| `Build_Advances_Tech_Level` | 4 Death Star tech buildings | 2..5 | Completing it raises tech to that level (name/data). |
| `Build_Countdown_Timer` | 1 (Death Star) | Yes | Shows a countdown while building (name). |
| `Build_Countdown_Text_ID` | 1 (Death Star) | text id | Label of that countdown (name). |
| `Build_Countdown_Text_RGBA` | 1 (Death Star) | RGBA 0..255 | Color of that countdown (name). |
| `Build_Tab_Heroes` | 11 hero companies | Yes only | Appears in the heroes tab of the galactic build bar (name). |
| `Build_Tab_Land_Units` | 70 companies | Yes 44, No 26 | Land units tab (name). |
| `Build_Tab_Outpost` | 16 ground bases | Yes 15, No 1 | Ground base tab (name). |
| `Build_Tab_Space_Station` | 31 star bases | Yes 16, No 15 | Space station tab (name). |
| `Build_Tab_Space_Units` | 49: space units 29, squadrons 19 | Yes 44, No 5 | Space units tab (name). |
| `Build_Tab_Special_Structures` | 53: special structures 47, tech buildings 4, Death Star | Yes only | Special structures tab (name). |
| `IsBuildable` | 72: indigenous 27, heroes 16, slaves 8 | No only | Not buildable (name). Spelled without underscores, unlike every other build tag; only ever No. |
| `Available_In_Skirmish` | 20: hero companies 19, star base 1 | yes 17, No 2, no 1 | Available in galactic skirmish (name). |
| `Tech_Level` | 291: containers 118, structures 108, units 46, black-market items 10 | 0..10, plus 99 (31) and 100 (2) | Tech level needed to build (name); 99/100 put it out of reach (data). Global `MP_Default_Max_Tech_Level` is 5. |
| `Required_Ground_Base_Level` | 301 | 0..4; 0 in 227 | Ground base level needed on the planet (name). |
| `Required_Star_Base_Level` | 277 | 0..5; 0 in 193 | Star base level needed (name). |
| `Required_Planets` | 186; 174 empty | planet name or list | Must own one of these planets (name). |
| `Required_Special_Structures` | 200; 132 empty | structure name(s), 23 distinct | Required structure on the planet, such as a barracks or factory (name). |
| `Required_Tech_Structure` | 12: indigenous 7, squadrons 5 | always empty | Unknown; never given a value. |
| `Required_Timeline` | 238 | int 0..5 (218), empty 20 | Unknown gating level (name); 0 in 193. |
| `Maintenance_Cost` | 81: space units 38, companies 37 | 0.05..0.3 | Fleet upkeep, probably a fraction of cost per cycle (name; guess). Global `Fleet_Maintenance_Update_Delay_Seconds`. |
| `Score_Cost_Credits` | 419 (420 uses): containers 147, structures 143, units 120 | int 0..100000 | Value counted on the score screen (name). |

#### Tactical production, build pads, and skirmish

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Tactical_Build_Cost_Multiplayer` | 409 (412 uses): upgrades 169, containers 103, structures 94, units 42 | int 100..40000; 10 empty | Price in skirmish/multiplayer tactical building (name); comment markers delimit skirmish blocks in unit files (comment). |
| `Tactical_Build_Cost_Campaign` | 214: upgrades 171, buildables 43 | int 10..1500 | Price of the same build inside a campaign battle (name). |
| `Tactical_Build_Time_Seconds` | 413 (416 uses) | int 0..140 | Tactical build time (name); global `Tactical_Build_Time_Multiplier` 1.0. |
| `Tactical_Build_Prerequisites` | 321: upgrades 175, containers 103, units 42 | empty 172, or upgrade names (tech-level upgrades) and 12 star-base names | Required tactical upgrades or star-base levels (data). |
| `Tactical_Production_Queue` | 317 | `Tactical_Units` 164, `Tactical_Upgrades` 153 | Which tactical queue builds it (name). |
| `Tactical_Buildable_Objects_Multiplayer` | 104: buildables 58, special structures 18, star bases 15 | faction-headed list; some lists hold several faction groups | What a pad or structure can build in skirmish (comment). |
| `Tactical_Buildable_Objects_Campaign` | 56: buildables 21, star bases 15, special structures 13, mobile defense 4 | faction-headed list | Same for campaign battles (comment). |
| `Tactical_Buildable_Constructed` | 96 buildable pads | object type name | The structure that appears when the pad's build finishes (data). |
| `Tactical_Build_Start_Lower_Z` | 80 buildables | 0 or 40..50 | Depth the structure rises from while building (name). |
| `Tactical_Build_Attachment_Bone_Name` | 5 mobile defense units | `ATTACH_POINT_00` | Bone where the built item attaches (name). FoC only. |
| `Tactical_Sell_Credits` | 25: ground structures 14, buildables 8 | 250..1250 | Refund when sold (name). |
| `Tactical_Build_Increments_Tech_Level` | 8 upgrades | Yes | Building it raises the tactical tech level (name). |
| `Tactical_Destruction_Decrements_Tech_Level` | 8 upgrades (same ones) | Yes | Losing it lowers it again (name). |
| `Minimum_Time_Before_Pad_Can_Build_Again` | 40 buildables | 15..80 s | Cooldown after a pad's structure is lost (name). |
| `Respects_Skirmish_Pre_Build_Bases_Option` | 3 buildables | false | Ignores the skirmish "pre-built bases" option (name). FoC only. |
| `Tactical_Additional_Structure_Type` | 16: special structures 14 | power generators, palace | Extra tactical structure a galactic structure brings to land maps; derived types name their faction's version (comment). |
| `Place_At_Every_Specific_Marker_Position` | 1 special structure | True | Placed at every marker made for its type (name). |
| `Place_Other_Type_At_Every_Specific_Marker_Position` | 2 special structures | faction turbolaser tower type | Galactic object populates every land marker for that type; it survives until all those instances die (comment). |
| `Base_Position` | 71 (72 uses): special structures 59, buildables 9 | `In_Base` 37, `Out_Base` 26, `Orbital` 9 | Where a galactic structure stands in battle: inside the base, outside it, or in orbit (data). |

#### Upgrade chains

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Next_Upgrade_Level_Type` | 175 upgrades | upgrade name (64) or empty (111) | Next level of the upgrade (data). |
| `Previous_Upgrade_Level_Type` | 187 upgrades | upgrade name (64) or empty (123) | Previous level (data). |
| `Destroy_Previous_Upgrade_Level` | 8 upgrades | No only | Whether the earlier level is removed on upgrade (name). |
| `Show_In_Sidebar_While_Building` | 187 upgrades | Yes 178, No 9 | Shown in the sidebar while in production (name). |

#### Bases, levels, and slots

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Base_Level` | 36: star bases 18, ground bases 16 | 1..6 | Level of a star base or ground base (data). Global `MaximumStarbaseLevel` 5. |
| `Base_Level_Available` | 54: structures 46, props 8 | 0 (17), 1 (35), 3, 4 | Base level at which a tactical structure appears (name). |
| `Next_Level_Base` | 54: star bases 31, ground bases 21 | base name or empty | Upgrade successor (data). |
| `Prev_Level_Base` | 54 | base name or empty | Predecessor in the base upgrade sequence (comment). |
| `Max_Space_Base` | 65 planets | 0..5; 5 in 39 | Highest star base level on the planet (name). |
| `Max_Ground_Base` | 1 (template planet) | 0 | Probably ignored or legacy: only on the art template planet. |
| `Special_Structures` | 1 (template planet) | 0 | Legacy or alias of the land/space pair below (name); used once. |
| `Special_Structures_Land` | 64 planets | 0..8 | Ground structure slots (name). |
| `Special_Structures_Space` | 64 planets | 0..3 | Orbital structure slots (name). |
| `Requires_Base_Power` | 2 special structures | True | Turret switches off when base power is lost (comment). |

#### Population

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Population_Value` | 170: containers 118, units 51 | 0..15; 1 in 119 | Population cost (name). |
| `Additional_Population_Capacity` | 94: planets 64, star bases 15, markers 13 | 1..100; planets mostly 5 | Population capacity added: per planet in galactic, by star bases, and by reinforcement-point markers in battle (100 on the base point, 1..3 on "PlusN" points) (data). |
| `Land_Override_Population_Value` | 1 hero company (Executor file) | 1 | Population in land mode instead of `Population_Value` (name). FoC only. |
| `Space_Override_Population_Value` | 6: unique 5, Death Star | 0..3 | Population in space mode; the Death Star has 0 so it does not crowd out real ships (comment). |

#### Planets

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Galactic_Position` | 65 planets | vector3, z = 10 | Position on the galaxy map (data); the template planet is at the origin. |
| `Galactic_Model_Name` | 244: planets 65, markers 56, multiplayer structure markers 45, skydomes, star bases 17 | `.alo`, 105 empty | Model shown in galactic mode (name). Engine: the catalog resolves it for the galactic model context; no engine code uses that context yet. |
| `Destroyed_Galactic_Model_Name` | 64 planets | asteroid-field model (63) | Model after the planet is destroyed (data). |
| `Always_Instantiate_Galactic` | 65 planets | yes only | Planet always exists in galactic mode (name). |
| `Terrain` | 65 planets | Temperate 27, Desert 13, Swamp 7, Forest 6, Urban 5, Volcanic 3, Arctic 3 | Planet climate, used for ambient sound and art (data). The engine literal is a shader name, so engine_reads is a false positive. |
| `Land_Tactical_Map` | 63 planets | `.ted`, 2 empty | Land battle map (name). |
| `Space_Tactical_Map` | 64 planets | `.ted` | Space battle map (name). |
| `Destroyed_Space_Tactical_Map` | 25 planets | always empty | Space map after destruction (name); never set. |
| `Planet_Surface_Accessible` | 64 | Yes 61, No 3 | Land invasion possible (name). |
| `Planet_Credit_Value` | 65 | 0..1250 | Income (name). Global `Credit_Cap_Per_Planet` 40000. |
| `Planet_Destroyed_Credit_Value` | 64 | 2..60 | Income after destruction (name). |
| `Planet_Capture_Bonus_Reward` | 64 | 500..2400 | Credits when captured (name). |
| `Planet_Tech_Availability_Rating` | 64 | 0..5 | Unknown rating (name). |
| `Planet_Ability_Name` | 62 | text id | Planet bonus name (data). |
| `Planet_Ability_Description` | 62 | text id | Planet bonus text (data). |
| `Planet_Ability_Icon` | 64 | `.tga`, 25 distinct | Planet bonus icon (data). |
| `Describe_Population` | 64 | text id | Encyclopedia population text (data). |
| `Potential_Indigenous_Power` | 32 planets (37 uses, max 4) | repeats: "indigenous type, power" pairs | Native units on the planet and their strength (data). |
| `Political_Control` | 233: planets 65, special structures 43, space units 29, squadrons 21 | 0 only (one empty) | Contribution to political control (name); always 0, with commented-out 1s left in source (data). Global `MaximumPoliticalControl` 5. |
| `Planet_Retains_Residual_Influence` | 3 planets | Yes | Whether political control survives abandoning the planet (comment). |
| `Planet_Groundbase_Stealth` | 7 | Yes | Ground base hidden from enemies (name). |
| `Planet_Starbase_Stealth` | 1 (Alderaan) | Yes | Star base hidden (name). FoC only. |
| `Planet_Is_Destroyable_By_TSW` | 3 asteroid planets | No | Death Star cannot destroy already-asteroid planets (comment). |
| `Planet_Is_Mining_Colony` | 2 | Yes | Mining colony flag (name). |
| `Planet_Restricted_Unit_Types` | 1 (Jabiim) | list of company and vehicle types | Types that cannot fight there (name). |
| `Planet_Restricted_Unit_Categories` | 1 planet | empty | Same by category (name); unused. |
| `Planet_Black_Market_Influences` | 4 | Yes | Planet affects the black market (name). |
| `Planet_Black_Market_Modifier` | 63 | -1000..2000 | Black market price or income adjustment (name). FoC only. |
| `Planet_Slice_Affinity_Types` | 48; 28 empty | structure, ship and squadron names | Types a slicer can unlock at this planet (name/data). |
| `Planet_Slice_Difficulty_Rating` | 64 | 0..5 | Slicing difficulty (name). |
| `Planet_Corruption_Removal_Base_Cost` | 54 | 1500 only | Cost to remove corruption (name). FoC only. |
| `Galactic_Influence_Range` | 8: heroes 5, scanner structures 2, one planet (Bothawui) | 0..150 | Range of galactic spy and reveal abilities; the planet value covers the whole galaxy (comment). |

#### Galactic fleets and heroes

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Always_Spawn_In_Orbit` | 31 (34 uses): heroes 19, generic heroes 11 | Yes 26, No 6, NO 2 | Hero appears in orbit rather than on the ground (name). |
| `Attach_To_Flagship_During_Space_Battle` | 50 heroes | Yes 29, No 22 | Hero rides the flagship in space battles (name). |
| `Can_Be_Neutralized_By_Major_Heroes` | 53 heroes | Yes 47, No 6 | Major heroes may neutralize it (name). |
| `Can_Be_Neutralized_By_Minor_Heroes` | 53 heroes | No 33, Yes 20 | Minor heroes may neutralize it (name). |
| `Neutralization_Cost` | 52 heroes | 20..15000 | Credit price of neutralizing it (name). |
| `Can_Participate_In_Space_Battle` | 1 transport | No | Kept out of space battles (name). |
| `Galactic_Fleet_Override_Icon_Index` | 1 (Death Star) | 4 | Fleet icon override in galactic mode (comment). |
| `Galactic_Fleet_Override_Model_Name` | 1 (Death Star) | `.alo` | Fleet model override (comment). |
| `Is_Interdictor` | 5: gravity wells, Interdictor cruiser, IPV1, a story prop | Yes 3, No 2 | Blocks hyperspace (name). |
| `Prevents_Blockade_Run_Attrition` | 1 (ground ion cannon) | True | Protects blockade runners from losses (name). |
| `Use_Special_Submit_Rules` | 7 transports | true | Unknown (name only). |
| `Prevents_Story_Campaign_Autoresolve` | 4: unique 3, Death Star | yes | Battle cannot be autoresolved in story campaigns (name). |

#### FoC corruption missions

Each planet carries up to three corruption options, `Corruption_1_*` (60 planets), `Corruption_2_*` (49) and `Corruption_3_*` (8), all in Planets.xml and FoC only. Per option: a `Type` (the corruption chosen), its `Credit_Cost`, a `Mission_Briefing` text, the requirements to finish it (`Required_Mission_Type`/`_Count`, `Required_Unit_Type`/`_Count`), hologram movies on success and failure, and optional rewards (`Success_Credit_Bonus`, `Success_Unlock_Unit_Type`). Only level 1 can name its own tactical mission (`Tactical_Game_Mode`, `Tactical_Map_Name`, `Tactical_Script_Name`). Meanings are from names and values (data); none is read by the engine.

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Corruption_1_Type` | 60 planets | 8 kinds: Racketeering 16, Piracy 11, Intimidation 9, Kidnapping 9, Bribery 7, Corrupt_Militia 5, Black_Market 2, Bonded_Citizens 1 | First corruption kind offered (data). |
| `Corruption_1_Credit_Cost` | 60 | 100..2000 | Its price (data). |
| `Corruption_1_Mission_Briefing` | 60 | text id; 1 empty | Briefing text (data). |
| `Corruption_1_Required_Mission_Type` | 31 | Intimidation 15, Kidnapping 7, Bribery 5, Piracy 4 | Mission kind needed to complete it (data). |
| `Corruption_1_Required_Mission_Count` | 31 | 1..2 | How many such missions (data). |
| `Corruption_1_Required_Unit_Type` | 47 | hero or unit name, or list of a hero's variants | Unit that must be present (data). |
| `Corruption_1_Required_Unit_Count` | 47 | 1..3 | How many (data). |
| `Corruption_1_Success_Bink_Hologram_Name` | 60 | movie name, 6 distinct | Success hologram (data). |
| `Corruption_1_Failure_Bink_Hologram_Name` | 30 | movie name; 1 empty | Failure hologram (data). |
| `Corruption_1_Success_Credit_Bonus` | 9 | 7500..10000 | Credits on success (data). |
| `Corruption_1_Success_Unlock_Unit_Type` | 12 | Underworld ship, squadron or company | Unit unlocked on success (data). |
| `Corruption_1_Tactical_Game_Mode` | 29 | Land 18, Space 11 | Battle mode of the mission (data). |
| `Corruption_1_Tactical_Map_Name` | 29 | `.ted` | Mission map (data). |
| `Corruption_1_Tactical_Script_Name` | 29 | story-plot `.xml` | Mission story script (data). |
| `Corruption_2_Type` | 49 | Bribery 14, Black_Market 10, Bonded_Citizens 9, Racketeering 8, Corrupt_Militia 8 | Second corruption kind (data). |
| `Corruption_2_Credit_Cost` | 49 | 0..2000 | Price (data). |
| `Corruption_2_Mission_Briefing` | 49 | text id | Briefing (data). |
| `Corruption_2_Required_Mission_Type` | 49 | Piracy 19, Kidnapping 14, Intimidation 8, Bribery 8 | Mission kind (data). |
| `Corruption_2_Required_Mission_Count` | 49 | 1..3 | Count (data). |
| `Corruption_2_Required_Unit_Type` | 28 (29 uses; 1 planet repeats) | unit names, 8 distinct | Required unit; repeats pair with repeated counts (data). |
| `Corruption_2_Required_Unit_Count` | 28 (29 uses) | 1..4 | Count (data). |
| `Corruption_2_Success_Bink_Hologram_Name` | 49 | `Saboteur_Loop` only | Success hologram (data). |
| `Corruption_2_Failure_Bink_Hologram_Name` | 1 | `Saboteur_Loop` | Failure hologram (data). |
| `Corruption_2_Success_Unlock_Unit_Type` | 9 | slave companies | Unit unlocked (data). |
| `Corruption_3_Type` | 8 | Black_Market 7, Racketeering 1 | Third corruption kind (data). |
| `Corruption_3_Credit_Cost` | 8 | 500..2000 | Price (data). |
| `Corruption_3_Mission_Briefing` | 8 | text id | Briefing (data). |
| `Corruption_3_Required_Mission_Type` | 8 | Piracy 7, Intimidation 1 | Mission kind (data). |
| `Corruption_3_Required_Mission_Count` | 8 | 1..2 | Count (data). |
| `Corruption_3_Required_Unit_Type` | 1 | mobile defense unit | Required unit (data). |
| `Corruption_3_Required_Unit_Count` | 1 | 1 | Count (data). |
| `Corruption_3_Success_Bink_Hologram_Name` | 8 | `Saboteur_Loop` | Success hologram (data). |
| `Corruption_3_Failure_Bink_Hologram_Name` | 5 | `Saboteur_Loop` | Failure hologram (data). |
| `Is_Indigenous_Unit_Corruptible` | 22: indigenous 17, props 3, containers 2 | False/false only | Whether natives can be corrupted (name). FoC only. |

#### Bribery, slicing, sabotage, piracy, black market

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Cheap_Bribe_Cost` | 42: containers 25, units 17 | 0..2500 | Lower bribe price (name). FoC only. |
| `Expensive_Bribe_Cost` | 30: containers 25, units 5 | 0..1000 | Higher bribe price (name). FoC only. |
| `Tactical_Bribe_Cost` | 109: units 72, containers 36 | 10..9999 | Price of bribing it in battle (name). FoC only. |
| `Bribe_Modifier_On_Corrupted_Planets` | 1 (Tyber Zann) | 0.75 | Bribe price factor on corrupted planets (name). FoC only. |
| `Bribe_Modifier_On_Uncorrupted_Planets` | 1 (Tyber Zann) | 1.0 | Same on uncorrupted planets (name). FoC only. |
| `Slice_Cost_Credits` | 28: structures 14, containers 7, units 7 | 250..4800 | Price of slicing it (name). Present in EAW source data too. |
| `Sabotage_Cost_Credits` | 37 special structures | 500..10000 | Price of sabotaging it (name). FoC only. |
| `Tutorial_Sabotage_Cost_Credits` | 3 special structures | 4000 or 100000 | Tutorial override of the sabotage price (name). FoC only. |
| `Piracy_Value_Credits` | 41: containers 27, units 14 | 50 (39) or 250 | Credits gained by piracy against it (name). FoC only. |
| `Base_Cost_Credits` | 10 black-market items | 1000..4800 | Black-market price (name). FoC only. |
| `Tutorial_Base_Cost_Credits` | 5 items | 1000 or 100000 | Tutorial price override (name). FoC only. |
| `Tutorial_Tech_Level` | 5 items | 0..5 | Tutorial tech-level override (name). FoC only. |
| `Uncommon_Price_Multiplier` | 10 items | 5.5 only | Price factor when the item is uncommon (name). FoC only. |

#### Capture points, cash points, reinforcement, and victory

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Capture_Point_Radius` | 29: buildables 14, markers 5, secondary structures 5 | 0..800 | Capture radius; build pads also refuse to build while enemies are inside it, and the default is 200 (comment). |
| `Capture_Point_Transition_Time_Seconds` | 27 | 3..20 | Time to change hands (name). Global `Land_`/`Space_Capture_Allowed_Countdown_Seconds`. |
| `Influences_Capture_Point` | 331 (335 uses) | False 291, True 38, no 6 | Whether the object counts toward capturing (name). |
| `Cash_Point_Credits_Per_Second` | 3 cash-point markers | 100/200/300 | Income while held (name). |
| `Cash_Point_Radius` | 3 | 30/40/50 | Capture radius (name). |
| `Cash_Point_Transition_Time_Seconds` | 3 | 5.0 | Time to change hands (name). |
| `Influences_Cash_Point` | 3 | no | The cash point itself does not count (data). |
| `Is_Community_Property` | 147: buildables 67, special structures 47, star bases 15 | Yes 140, No 5, false 1, no 1 | Object can change owner or be used by any side (name). |
| `Ownership_Sticks` | 14: secondary structures 8, special structures 4 | Yes 13, No 1 | Keeps its owner after the capturer leaves (name). |
| `Control_Point_Domination_Condition_Relevant` | 1 (Reinforcement_Point) | Yes | Counts for the control-point domination victory (name). Global `Control_Point_Domination_Victory_Time_In_Secs`. |
| `Reinforcement_Enabling_Radius` | 4 markers | 250 | Area where a held reinforcement point allows reinforcements (name). Global `Use_Reinforcement_Points` True. |
| `Reinforcement_Prevention_Radius` | 48: star bases 33, special structures 9 | 200..2200; 2000 in 34 | Enemy cannot reinforce inside it (name). |
| `Reinforcement_Region_Blob_Name` | 4 markers | overlay name | Overlay that draws the reinforcement area (name). |
| `Should_Reinforcements_Move` | 4 ground vehicles | false | Reinforcements of this type stay where they land (name). |
| `Victory_Relevant` | 509 (515 uses): structures 270, units 208, props 21 | yes 234, No 194, no 70, Yes 17 | Must be destroyed for a side to lose (name). |
| `Land_Victory_Relevant` | 2 (ground HV gun, ion cannon) | Yes | Same, land only (name). |
| `Space_Victory_Relevant` | 2 (space-mode HV gun, ion cannon) | No | Same, space only (name). |
| `HQ_Win_Condition_Relevant` | 4 command centers and palace | yes | Counts for the headquarters win condition (name). |
| `Map_Load_Spawn_Table` | 4 markers | list of "type, chance" pairs | Spawns one of the listed types with these chances when the map loads (data). |

#### AI and autoresolve

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `AI_Combat_Power` | 383 (384 uses): units 189, projectiles 115, structures 71 | int 0..600000 | AI and autoresolve strength rating (name); comments tune it to deter AI attacks (comment). Engine: read as the type's combat rating, returned to Lua by the object-type combat-rating method. |
| `Combat_Power_Value` | 39 hero companies | 0 (36), 5, 500 | Unknown; possibly a company-level combat rating (name). |
| `Has_Land_Evaluator` | 236 (255 uses) | True 219, Yes 34, False 1, No 1 | AI evaluates the object in land battles (name). Repeats in 19 objects. |
| `Has_Space_Evaluator` | 108 | True 92, False 16 | Same for space (name). |
| `SpaceAutoResolveHitRate` | 1 special structure | 3 | Autoresolve hit rate of a ground-to-space weapon (name). |
| `SpaceAutoResolveStunRate` | 1 special structure | 3 | Autoresolve stun rate (name). |
| `Size_Value` | 410: units 220, structures 183 | 4..360 | Unknown size measure (name). |
| `Can_Indigenous_Unit_Stop` | 9: indigenous 8 (birds, sketto), 1 infantry | No 8, false 1 | Native wanderer never stops moving (name). |
| `CanCellStack` | 86: indigenous 27, heroes 20, infantry 18 | yes only | Several small units may share a cell (name; unverified). |

#### Fog of war and reveal

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Land_FOW_Reveal_Range` | 446 (449 uses): structures 217, units 152, containers 42 | 0..10000; 300 in 154 | Sight radius in land battles (name). |
| `Space_FOW_Reveal_Range` | 158 (168 uses; 10 repeat) | 0..18000 | Sight radius in space battles (name). |
| `Dense_FOW_Reveal_Range_Multiplier` | 188 | 0..1; 0.2 in 83, 1.0 in 79 | Factor on reveal range under dense fog, perhaps nebulae (name). |
| `Initial_State_Visible_Under_FOW` | 193 structures and props | False 167, True 24, Yes 2 | Visible under fog from the start (name). |
| `Last_State_Visible_Under_FOW` | 212 (215 uses) | True 129, Yes 86 | Last seen state stays shown under fog (name). |
| `Multisample_FOW_Check` | 111 (112 uses), mostly structures | Yes only | Fog visibility is tested at several points of the footprint (name). |
| `Visible_To_Enemies_When_Empty` | 58: buildables 40, star bases 15 | True 57, false 1 | Empty pad or slot shown to enemies (name). |
| `Model_Visible_To_Enemy` | 3: 2 projectiles, 1 particle | No | Hides the effect from the enemy; to be used with care (comment). |
| `Reveal_During_Setup_Phase` | 212 structures and props | true 126, False 52, false 33, True 1 | Revealed to the opponent during the setup phase (name). Global `SetupPhaseFOWColor`. |
| `Reveal_During_Setup_Phase_Only` | 9 buildables | False 5, True 4 | Revealed only during setup (name). |
| `Reveal_For_Attacker` | 2 markers | true | Marker reveals its area for the attacker (name). |
| `Reveal_For_Defender` | 1 marker | true | Same for the defender (name). |
| `Modifies_Reveal_Range` | 9 structures (jamming stations, gravity wells) | yes 7, no 2 | Changes reveal range around it (name). |
| `Reveal_Range_Modifier` | 9 (same objects) | -0.5 (8), -0.75 (1) | Factor of that change, written as `-.50` (data). |
| `Sensor_Range` | 36: infantry 12, heroes 10, indigenous 7, vehicles 7 | 20..200 | Detection range (name); unknown how it differs from reveal range. |

#### Markers, dummies, decorations, and editor flags

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Is_Marker` | 144: markers 83, multiplayer structure markers 48, script markers 6 | Yes only | Logical marker, not a real object (name). |
| `Is_Dummy` | 325: containers 167, structures 155 | Yes 236, No 89 | Abstract placeholder that never exists as itself, such as a company or an unbuilt pad (name). |
| `Is_Decoration` | 1063 (1066 uses): props 841, particles 167 | No 694, Yes 372 | Not simulated or synchronized; only idle objects may be decorations, otherwise multiplayer desyncs (comment). |
| `Is_Discardable` | 455 (473 uses; 18 repeat): space props 239, markers 79 | No 346, false 127 | May be dropped when not needed (name); never true in stock. |
| `Is_Branched_Map_Discardable` | 43: ground structures 30, story props 12 | False 31, True 11, No 1 | Dropped from branched map variants (name). |
| `Is_Editor_Placed` | 5 props and particles | false only | Editor placement flag (name). |
| `Marker_For_Specific_Object_Type` | 53: multiplayer structure markers 48, markers 5 | list of types (usually both factions' pads) | Types that may be placed at this marker (data). |
| `Is_Asteroid_Field` | 17 space props | No 11, Yes 6 | Asteroid-field hazard (name). |
| `Is_Impassable_Asteroid` | 13: space props 9, special structures 4 | true 11, Yes 2 | Asteroid that blocks movement (name). |
| `Is_Ion_Storm` | 10 space props | No 6, Yes 4 | Ion-storm hazard (name). |
| `Is_Nebula` | 10 space props | Yes only | Nebula hazard (name). |
| `Permanent` | 19 terrain decals | false 13, true 6 | Decal never fades (name). |
| `Tactical_Model_Name` | 2 props (tree, bush) | `.alo` | Probably ignored or legacy model field; used twice (name). |

#### Tactical overview camera

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Tactical_Overview_Clicks` | 3 tactical cameras | 4, 10, 99 | Number of clicks that opens the overview view (comment). |
| `Tactical_Overview_Click_Time` | 3 | 0..1.8 s | Window for those clicks; 0 disables it (comment). |
| `Tactical_Overview_Distance` | 3 | 1000..2500 | Overview camera distance (name). |
| `Tactical_Overview_Distance2` | 2 | 1600..2900 | Second overview stage distance (name). |
| `Tactical_Overview_FOV` | 2 | 55..60 | Overview field of view (name). |
| `Tactical_Overview_FOV2` | 2 | 70 | Second stage field of view (name). |
| `Tactical_Overview_Pitch` | 1 | 65 | Overview pitch (name). |
| `Tactical_Overview_Pitch2` | 2 | 80 | Second stage pitch (name). |

#### Findings

- Of the six tags whose names appear in EAWEngine's source, four are real reads: `Affiliation` (team color and spawn filter), `AI_Combat_Power` (Lua combat rating), `Lua_Script` (object scripts in battles), and `Galactic_Model_Name` (resolvable, but nothing uses the galactic context). `Faction` and `Terrain` match only as a definition type name and a shader name.
- `Affiliation` lists several factions in 52 uses, and the engine colors by the first one only. The order in those lists varies (`Neutral, Rebel, Empire, ...` against `Underworld, Rebel, Empire`), so a list's first faction is not a reliable owner. Battles should color by the owning player instead.
- Spellings: booleans mix `Yes`/`yes`/`True`/`true`/`NO` within one tag (`Victory_Relevant`, `Reveal_During_Setup_Phase`, `Has_Land_Evaluator`, `Always_Spawn_In_Orbit`). `Ship_Class` has `capital` and `capital_ship` plus case variants. `IsBuildable` and `CanCellStack` break the underscore naming. `Reveal_Range_Modifier` is written `-.50`. Parsers must accept all of these, case-insensitive.
- Some tags carry no information: `Political_Control` is always 0, and `Required_Tech_Structure`, `Destroyed_Space_Tactical_Map` and `Planet_Restricted_Unit_Categories` are always empty. `Is_Discardable` and `Is_Editor_Placed` are never true. `Max_Ground_Base` and `Special_Structures` appear only on the art template planet, and `Tactical_Model_Name` only twice. These are probably ignored or legacy.
- Many tags come in campaign and multiplayer pairs: `Tactical_Build_Cost_Campaign`/`_Multiplayer` and `Tactical_Buildable_Objects_Campaign`/`_Multiplayer`, plus tutorial overrides (`Tutorial_Base_Cost_Credits`, `Tutorial_Sabotage_Cost_Credits`, `Tutorial_Tech_Level`). Comment markers bracket the skirmish-only value blocks inside unit definitions. A skirmish mode must pick the variant by game mode.
- FoC-only tags: all 33 `Corruption_N_*` tags, bribe costs and modifiers, `Sabotage_Cost_Credits`, `Piracy_Value_Credits`, the black-market item tags, `Planet_Black_Market_Modifier`, `Planet_Corruption_Removal_Base_Cost`, `Planet_Starbase_Stealth`, `Is_Indigenous_Unit_Corruptible`, `Land_Override_Population_Value`, `Respects_Skirmish_Pre_Build_Bases_Option` and `Tactical_Build_Attachment_Bone_Name`. `Slice_Cost_Credits`, `Build_Can_Be_Unlocked_By_Slicer` and the planet slice tags also appear in the EAW source data.
- Several tags are overridden or bounded by globals in GameConstants: `MaximumPoliticalControl` 5, `MaximumStarbaseLevel` 5, `MP_Default_Max_Tech_Level` 5, `Credit_Cap_Per_Planet` 40000, `Tactical_Build_Time_Multiplier` 1.0, `Use_Reinforcement_Points` True, the land and space capture countdowns, `Control_Point_Domination_Victory_Time_In_Secs`, and the autoresolve attrition constants. A comment gives a hidden default of 200 for `Capture_Point_Radius`.
- For the deterministic simulation, `Is_Decoration` is a content contract: decorations are unsynchronized, idle-only objects. The engine can keep them out of the simulation and the state hash. Whatever has a non-idle behavior must stay simulated.
- `Victory_Relevant` (509 objects) is the next tag to read for the M3/M4 battle end condition, together with `HQ_Win_Condition_Relevant`, `Land_`/`Space_Victory_Relevant`, and the capture and reinforcement radii (`Capture_Point_Radius`, `Reinforcement_Prevention_Radius`, `Reinforcement_Enabling_Radius`). Skirmish fog needs `Land_`/`Space_FOW_Reveal_Range`.
- Many dump "developer comments" are commented-out XML lines next to the tag (for example a disabled `Build_Cost_Credits` or `Political_Control` value), not explanations of it. They were weighed as such.

### Models, rendering, animation, user interface, and text

This family holds the tags that decide how an object looks and how the interface presents it: which model it draws in each context and at what scale, which parts drop out at low detail, shadows and ground marks, damage states, animation selection, selection brackets and radar icons, and the text and encyclopedia entries shown for it. It also carries the core structural tags (inheritance, behaviors, categories), which the overview chapters cover in depth. Several groups belong to non-unit object kinds with their own files: `TacticalCamera` (camera limits), `Planet` (galactic art and descriptions), `LightSource`, `LensFlare`, `Decal` (terrain decal FX), and skydome kinds. Most tags are written once per object; the few that repeat are noted. Counts are objects in the installed (patched FoC) data unless stated; "FoC only" means absent from the EAW source.

#### Core structure (summary; see the overview chapters)

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Variant_Of_Existing_Type` | 794 objects: units 312, structures 218, props 153, projectiles 55, containers 49, upgrades 7 | an object name (317 distinct; top `Urban_Civilian_A` 18) | Inherit every tag not set locally from the named type (data). Engine: `GameObjectCatalog::field` walks the chain, preferring a parent of the same XML kind, with a depth limit and diagnostic. |
| `Behavior` | 1,492 objects of every group (structures 316, props 222, containers 217, upgrades 187, units 165, effects 150, projectiles 119) | comma list of behavior words (118 distinct; `DUMMY_UPGRADE` 179, `DUMMY_GROUND_COMPANY` 140, `PARTICLE` 136, `MARKER` 122); 45 empty; one object repeats it | Behaviors active in every mode (data). Comments say some behaviors (`TACTICAL_UNDER_CONSTRUCTION`, `TACTICAL_SELL`) only work here, not in `LandBehavior` (comment). Engine: checked for `TURRET` (aims with body) and in schema validation. |
| `LandBehavior` | 642 objects: structures 212, units 197, props 183, containers 42 | comma list (77 distinct; `LAND_OBSTACLE` 110) | Behaviors added in land battles (data). Engine: read beside `Behavior` for the turret test. |
| `SpaceBehavior` | 322 objects: props 145, units 81, structures 67 | comma list (16 distinct; `DEBRIS` 139) | Behaviors added in space battles (data). Engine: `FIGHTER_LOCOMOTOR` in it enables the minimum flight speed. |
| `TacticalBehavior` | 9 units (8 transports, 1 unique) | `SELECTABLE` only | Behaviors for both tactical modes (name); used only on transports. |
| `CategoryMask` | 831 objects: structures 304, units 214, upgrades 187, containers 88, props 37 | `\|`-separated category words (50 distinct; `Structure` 274, `Upgrade` 187, `Infantry` 87) | Targeting and AI categories (data); a comment says a category is needed for an object to be a land bombing-run target (comment). Engine: parsed (separators `\|`, comma, space) into script type categories. |
| `Property_Flags` | 112 objects: structures 43, units 32, containers 27, props 10 | `\|` list (15 distinct; `SmallShip` 43, `InBase` 32, `NotOpportunityTarget` 12) | Gameplay/AI property flags (data). Not read by the engine. |
| `Type` | 418 objects: hardpoints 355, units 61, props 2 | hardpoints: `HARD_POINT_*` (laser 168, ion 37, missile 35...); units: `Walk` 50, also `Fly`, `Hover`, `Tracked` | Two meanings by kind: a hardpoint's kind (data), and on units a movement style word (name; probably for presentation such as footsteps or move animations, unknown). The same tag name is also used inside ability blocks and GUI files (outside this count). Engine: reads the hardpoint `Type` only (weapon, engine, shield... in `combat_content.cpp`). |
| `Weather_Category` | 93 objects (units 92, prop 1); 2 repeat it | `Infantry` 55, `Walker` 11, `Hover` 10, `Flying` 9, `Tracked` 5, `LargeWalker` 3, `Speeder` 2 | Class used to pick weather effects on a unit (name). Not read. |

#### Models and model placement

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Land_Model_Name` | 1,536 objects: props 772, structures 313, units 190, effects 143, projectiles 76, other 30 | `.alo` file (845 distinct); 7 empty, 4 without extension; 2 objects repeat it | Model drawn in land battles; for `Particle` objects a particle-system ALO (data). Engine: the model for the land context (`GameObjectCatalog::model`, inherited through variants); particles and projectiles fall back to the space model when absent. |
| `Space_Model_Name` | 745 objects: props 332, structures 130, units 119, effects 102, projectiles 30 | `.alo` file (523 distinct); 59 empty | Model drawn in space battles (data). Engine: as above for the space context. |
| `xxxSpace_Model_Name` | 17 units (15 space units, 2 unique), all death clones | `.alo` file | A `Space_Model_Name` disabled by prefixing `xxx` (data); the game sees an unknown tag, so the clone inherits its parent's model. Must stay ignored. |
| `Model_Name` | 1 object (`MiscObject` barrage target dummy) | `w_smallbomb.alo` | Probably a stray generic spelling of a model tag (data, used once); likely ignored by the game (inference). |
| `Land_Terrain_Model_Mapping` | 3 infantry (`Stormtrooper`, `Plex_Soldier`, `Rebel_Trooper`) | list of pairs: terrain type, model (Temperate, Urban, Arctic, Volcanic, Desert, Forest, Swamp) | Choose a model variant by the map's terrain type, such as snow or sand troopers (data). Not read; the engine always draws `Land_Model_Name`. |
| `Scale_Factor` | 1,939 objects: props 913, structures 322, units 308, effects 155, projectiles 120, other 116; 9 repeat it | float 0.05-500 (`1.0` 870, `1.5` 143, `2.0` 79; `500.0` 34 for skydome/backdrop art) | Uniform model scale (data). Engine: `type_scale_factor` scales the model, attached effects, and model-derived footprints; non-positive values warn and draw at 1; projectiles and particle objects read their own. |
| `Scale` | 21 objects: decals 19, space props 2 | decals: two numbers (width, height; `25, 25` most); props: one float | On decals the decal size (data). On the two `Destroyed_Planet_Remains` props it sits beside `Scale_Factor` and is probably ignored (inference). Engine: not read as an XML tag (the engine_reads hit is an unrelated particle track name). |
| `LOD_Bias` | 79 objects: units 54, containers 24, structure 1 | 0.5-10 (`5` 40, `4` 13, `1` 9, `10` 7) | Bias on distance-based mesh detail selection, higher keeping detailed meshes longer (data). FoC only. Engine: `type_lod_bias` (default 1, non-positive becomes 1) feeds `VisualInstance::lod_bias`, used by the renderer's detail-level position; how the original applies it is unverified. |
| `Facing_Adjust` | 82 objects: planets 64, structures 12, props 6 | always `0.0, 0.0, 0.0` | Rotation correction applied to the model (name); all stock values are zero, so its units and order are unknown. |
| `Camera_Aligned` | 65 planets | `yes` 64, `No` 1 (galaxy core art) | Galactic planet model turned to face the camera (name). |
| `Pre_Lit` | 67 objects: planets 65, containers 2 (galactic fleet and squadron); one repeats it | `no` on planets, `Yes` on the fleet containers | Model uses baked lighting instead of scene lights (name). |
| `No_Colorization_Color` | 40 objects: units 36, structures 3, container 1 | RGBA 0-255 (`255, 255, 255, 255` 15, `75, 75, 75, 255` 13, `128, 101, 79, 255` 7); 1 empty | Team color to use when the object has no faction colorization, e.g. neutral fauna browns (name, data). Not read; the engine's team tint comes from the owner. |
| `Overall_Length` | 28 objects (ground vehicles 23) | 0-166 | Vehicle length used to pitch it along terrain slopes (comment). Not read. |
| `Overall_Width` | 28 objects (ground vehicles 23) | 0-56; `0.0` 11 | Vehicle width used to roll it across slopes; 0 disables rolling (comment). Not read. |
| `Is_Sprite` | 15 units (generic heroes 13, vehicles 2); 4 repeat it | always `No` | Draw as a billboard sprite instead of a model (name); stock only ever disables it. |
| `Movie_Object` | 59 objects: units 52, structures 7 | always `false` | Marks an object used only in movies or cinematics (name); stock only clears it. |
| `Cinematic_Object_Only` | 3 props | `Yes` | Object exists only for cinematics (name). FoC only. |

#### Detail levels, fading, and culling

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `No_Reflection_Below_Detail_Level` | 532 objects: structures 240, props 182, units 81, other 29 | int 0-99 (`1` 453, `99` 40, `0` 30, `2` 9) | Leave the object out of water reflections below this detail level (name); the scale matches the graphics presets' `WaterDetailLevel` 0-2, with 99 meaning never (data). Not read. |
| `No_Refraction_Below_Detail_Level` | same 532 objects | int 0-99 (`1` 451, `99` 45, `3` 24) | As above for refraction (name, data). Not read. |
| `No_Particle_Below_Detail_Level` | 21 particle objects | float 0.2-0.7 | Skip the effect when particle detail is below this value; the scale matches the presets' `ParticleDetail` 0.4-1.0 (name, data). Not read. |
| `Blob_Shadow_Below_Detail_Level` | 295 objects: units 147, props 144, structures 4; 9 repeat it | int 1-99 (`2` 112, `3` 89, `1` 76, `99` 27) | Use the blob shadow when shadow detail is below this level, real shadows above it (name); matches the presets' `ShadowDetail` 0-3, 99 meaning always blob (data). Not read. |
| `Exclude_From_Distance_Fade` | 345 objects: props 255, other (skydomes) 51, units 18, structures 12, effects 9; one repeats it | bool (`true` 335, `No` 9) | Keep the object from fading out with camera distance (name); skydomes, backdrops, build pads. Related to the camera's `Object_Fade_Begin`/`End` (data). Not read. |
| `Allow_Particle_Model_Culling` | 8 particle objects (explosions, bombs) | `yes` | Let the particle object be culled when off-screen (name). Not read. |
| `Occlusion_Silhouette_Enabled` | 95 objects: units 74, structures 18, props 3 | `1` 86, `0` 9 | Draw a silhouette when the object is hidden behind others (name); GameConstants holds a global `Occlusion_Silhouettes_Enabled` switch (data). Not read. |

#### Shadows, selection blobs, and ground marks

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Blob_Shadow_Material_Name` | 308 objects: units 147, props 144, structures 17; 16 repeat it | `Generic_Shadow` 165, `Tree_Shadow` 90, `Rock_Shadow` 29, `Tree_Shadow_Light` 13, `Square_Shadow` 8; 19 empty | Material of the projected blob shadow (name, data). Not read. |
| `Blob_Shadow_Scale` | 308 objects (same set); 9 repeat it | two floats (`5.0, 5.0` to `200.0, 100.0`) | Blob shadow size in X and Y (name, data). Not read. |
| `Blob_Shadow_Bone_Name` | 84 objects (units 83) | always `B_Chest` | Bone the blob shadow follows, for animated infantry and heroes (name, data). Not read. |
| `Selection_Blob_Material_Name` | 22 objects (heroes 21) | `Selection_Empire_Hero` 14, `Selection_Rebel_Hero` 8 | Ground material drawn under a selected hero (name). Not read. |
| `Highlight_Blob_Material_Name` | 22 objects (heroes 21) | `Highlight_Rebel_Hero` 8, `Highlight_Empire_Hero` 7, `Highlight_Underworld_Hero` 7 | Ground highlight drawn under a hero (name). Not read. Note Underworld heroes use the Empire selection blob. |
| `SurfaceFX_Name` | 101 objects (units 99) | list: `Generic_Footprint, Infantry_Terrain_Modifier` 80, `Hover` 14, `Generic_Track` 3 | Surface effects the unit leaves while moving: footprints, tracks, hover wash (data); a comment marks repulsorlift units (comment). Needs the `SURFACE_FX` behavior. Not read (the audio track plays footsteps from animation events instead). |
| `Terrain_Texture_Modifier_Material` | 92 objects (structures 91) | always `62` | Terrain texture index painted under a structure (name, data). Pairs with the `TERRAIN_TEXTURE_MODIFICATION` behavior. Not read. |
| `Terrain_Texture_Modifier_Square` | 102 objects (structures 101); 2 repeat it | bool (`false` 71, `true` 32) | Paint the modified terrain as a square rather than the footprint shape (name). Not read. |
| `Terrain_Texture_Modifier_Join_Distance` | 92 objects (structures 91) | `500.0` 64, `1.0` 28 | Distance within which neighbouring modified areas join (name). Not read. |
| `Hides_When_Built_On` | 35 structures (build pads) | `True` 34, `False` 1 | Hide the pad's model once a structure is built on it (name, data). Not read. |

#### Damage visuals and hardpoint art

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Land_Damage_Thresholds` | 219 objects: structures 184, units 27, props 8 | list of health fractions, descending (`1, 0.66, 0.33, 0` 127, `1, 0.66, 0.33` 45, `1` 35) | Health fractions at which the land model switches damage state (data; recorded in compatibility-research). Not read by the engine. |
| `Land_Damage_Alternates` | same 219 objects | parallel list of `_ALT<n>` indices (`0, 1, 2, 3` 127, `0, 1, 2` 45, `3` 35) | The model alternate shown for each threshold (data). Build pads use a single `1` / `3` pair. Not read: the engine keeps every `_ALT<n>` mesh but has no health-driven state switch yet. |
| `Asteroid_Damage_Hit_Particles` | 57 objects (units 56) | particle name: `Small_Damage_Space` 34, `Large_Damage_Space` 21 (one lower-case `large_...`) | Effect played when an asteroid field damages the ship (name, data). Not read. |
| `Model_To_Attach` | 220 hardpoints | `.alo` file (189 distinct); 2 empty | Separate model drawn for the hardpoint, attached to the host (data). Engine: `scene_models.cpp` loads it and attaches it at `Attachment_Bone`, warning on a missing file or bone. |
| `Attachment_Bone` | 267 hardpoints | bone name (144 distinct; `HP_E_BONE` 15) | Bone of the host the hardpoint model and its position attach to; a comment notes it also centers the HUD reticle when nothing is attached (comment). Engine: attachment point for `Model_To_Attach` and the hardpoint's position (falling back to `Fire_Bone_A`). |
| `Engine_Particles` | 21 hardpoints (engines) | bone name (`HP_E_MAINENGINES` 18); 1 empty | Despite the name, a bone in the host model whose engine-glow effects belong to this hardpoint (data). Engine: resolved as a bone (`HardpointProfile::engine_bone`). |
| `Engine_Hardpoint` | 1 hardpoint (`CIN_HP_Star_Destroyer_Engines`) | `Yes` | Marks an engine hardpoint (name). Installed only (a cinematic hardpoint file added by the patch); stock engine hardpoints say this through `Type` instead. Not read. |
| `Tooltip_Text` | 260 objects: hardpoints 259, marker 1 | text key (17 distinct; `TEXT_WEAPON_TURBOLASER` 78) | Name shown when hovering a hardpoint (data). Not read. |
| `Blast_Ability_Fire_Projectile_Type` | 1 hardpoint (Krayt megaweapon damage) | projectile name | Projectile fired by a blast ability from this hardpoint (name). FoC only. Not read; belongs with abilities. |
| `Internal_Damage_Type` | 5 projectiles | `Damage_Force_Whirlwind` 3, `Damage_Remote_Bomb` 2 | Damage type the projectile deals internally (name); likely selects matching death clones such as the whirlwind deaths (inference). Not read; belongs with combat. |

#### Animation

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Loop_Idle_Anim_00` | 356 objects: other 116 (planets, skydomes), units 86, props 78, structures 61, projectiles 15; one repeats it | bool (`Yes` 197, `No` 160) | Loop the first idle clip continuously instead of playing idles at intervals (name, data). Not read; the engine picks idle clips by its own rules. |
| `Idle_Anim_00_Rate_Mod` | 66 props (space props 63); 2 repeat it | `1.0` 64, `0.25` 2, `8.0` 2 | Playback rate multiplier of the first idle clip (name). The two repeating objects (huge asteroid masses) give `8.0` then `1.0`, so which wins decides the look (data). Not read. |
| `Allow_Idle_When_Moving` | 8 fauna (birds, sketto) | `Yes` | Keep playing the idle clip while moving, for flyers without a move clip (name, data). Not read. |
| `Land_Model_Anim_Override_Name` | 26 objects: units 22, props 4 | model name with or without `.ALO` (`NI_GMale_Urban_A.ALO` 9, `RI_INFANTRY.ALO` 6; 5 without extension, mixed case such as `.ALo`) | Model whose animation clips the type plays instead of its own (data; recorded). Engine: `type_animation_source`, inherited through variants; clips are matched to the model's bones by name. The engine also checks a `Space_Model_Anim_Override_Name`, which stock does not use. |
| `Specific_Death_Anim_Index` | 102 death-clone units | int 0-2; 79 empty | DIE clip variant a death clone plays (data). Engine: read from the `Damage_Normal` death clone as `DeathStyle::die_variant`; its presence also marks a type as a death clone. An empty value reads as variant 0. |
| `Specific_Death_Anim_Type` | 96 objects (death clones: units 95) | `FW_DIE` 30, `FL_DIE` 30, `Crushed` 13, `DIE` 11, `FIRE_DIE` 4, `FTK_DIE` 2; 4 empty | Clip role a death clone plays: force whirlwind, force lightning, crushed, fire, telekinesis... (data). Not read: the engine always uses DIE. |
| `Should_Death_Clone_Play_Idle` | 17 structures (space station death clones) | `true` | The death clone keeps playing its idle clip while dying (name). Not read. |
| `Cinematic_Anim_Index` | 4 objects (2 infantry, 2 story props) | `00` | Clip index played when the object is in a cinematic (name). Not read. |
| `Cinematic_Anim_Blend_Seconds` | same 4 | `1.0` | Blend time into the cinematic clip (name). Not read. |
| `Pause_During_Cinematic_Anim` | same 4 | `yes` 2 (infantry), `no` 2 (props) | Whether the object stops acting during its cinematic clip (name). Not read. |
| `Particle_Lifetime_Frames` | 139 particle objects | int 10-7,500 (`30` 87) | How long a particle object lives, in frames (name); comments say long values let debris finish (comment). Frame rate base unknown. Not read (the engine plays emitters to their own end). |

#### Layering of skydomes and backdrops

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Layer_Z_Adjust` | 262 objects: units 99, props 78, skydomes 51, structures 33 | float -2,500 to 100 (`0.0` 55, `-330.0` 52, `-2500.0` 39) | Depth offset that moves the object between render layers (name); large negatives on backdrops push them behind (data). Not read. |
| `In_Background` | 109 objects: skydomes 52, props 45, particles 10, units 2 | bool (`Yes`/`yes` 68, `no`/`No` 41) | Draw in the background layer behind the battle (name). Not read. |
| `In_Midground` | 2 particles (galactic corruption) | `true` | Draw in a middle layer (name). FoC only. |
| `In_Midforeground` | 1 particle (galactic corruption line) | `true` | Draw in a layer in front of the midground (name). FoC only. |
| `Sort_Order_Adjust` | 248 objects: props 226 (space props 224), skydomes 22 | `-1` 247; 1 empty | Draw-order correction for transparent backdrop art (name). Not read. |

#### Selection, brackets, and interface models

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Select_Box_Scale` | 435 objects: units 195, structures 185, containers 42, props 13; 5 repeat it (up to 3 times) | int 0-1,800 (`180` 66, `50` 46) | Size of the selection circle (name, data). Not read. |
| `Select_Box_Z_Adjust` | 258 objects (units 194) | `3.0f` 142, `-30` 55, `-50` 28 | Height offset of the selection circle; values often carry a C-style `f` suffix (data). Comments note teams disable it in favour of a big team circle (comment). Not read. |
| `GUI_Bracket_Size` | 299 objects: structures 203, units 84; 9 repeat it | `2` 175, `0` 74, `1` 59 | Size class of the corner brackets drawn around a hovered object (name). Not read. |
| `GUI_Bracket_Width` | 71 objects | 5-500 | Explicit bracket width (name). Not read. |
| `GUI_Bracket_Height` | 73 objects | `0` 70, `500` 3 | Explicit bracket height (name). Not read. |
| `GUI_Bounds_Scale` | 185 objects (structures 153) | 0.005-2.0 | Scale of the bounds used for the health bar or brackets (name). Not read. |
| `GUI_Bias` | 1 structure (Empire anti-infantry turret) | `5` | Unknown; an interface offset (name). FoC only, used once. |
| `Mouse_Collide_Override_Sphere_Radius` | 164 objects: units 97, planets 64 | 5-50 (`10.0` 78, `19.77` 49 on planets) | Replace the model's mouse-pick shape with a sphere of this radius (name). Not read. |
| `Not_Really_Selectable` | 2 (proximity mine, Eclipse) | `True` | Object has selection behavior but should not be picked by the player (name). FoC only. |
| `User_Bound_Min` | 22 objects (transports 12, unique 9) | 3 floats (`-700, -700, 0` 17) | Lower corner of an author-given bounding box, used for landing craft (name, data). Not read. |
| `User_Bound_Max` | same 22 | 3 floats (`700, 700, 1500` 17) | Upper corner of that box (name). Not read. |
| `GUI_Model_Name` | 119 objects: planets 64, structures 25, containers 17, units 12; 2 repeat it | `.alo` file (64 distinct; planet art `W_planet_*`, ship models) | Model shown in interface panels such as the galactic planet view or build buttons (data). Not read. |
| `GUI_Model` | 1 company (`Rebel_Speeder_Wing`) | `.alo` file | Probably a misspelling of `GUI_Model_Name`, likely ignored (inference). |
| `GUI_Distance` | 58 objects | 65-4,700 | Camera distance for the interface model view (name). Not read. |
| `GUI_Offset` | 62 objects | 3 floats, Z only (`0 0 40`, `0 0 -300`) | Offset of the interface model (name). Not read. |
| `GUI_Velocity` | 62 objects | `45` 61, `90` 1 | Spin rate of the interface model (name; probably degrees per second). Not read. |
| `GUI_Angles` | 5 star bases | `90 0 0` | Fixed orientation of the interface model (name). Not read. |
| `GUI_X_Rot` | 5 star bases | `0` | X rotation of the interface model (name). Not read. |
| `GUI_Row` | 237 objects | `0` 119, `1` 118 | Row of the build or roster bar the object's button goes in (name). Not read. |
| `Icon_Name` | 982 objects: structures 306, units 249, containers 213, upgrades 187; 14 repeat it | `.tga` file (394 distinct); 11 lists, 6 empty | Button and portrait icon (data); a comment says leaving it blank keeps a paired unit (R2 and 3PO) to a single icon (comment). Not read. |
| `Native_Icon_Name` | 53 planets | `load_*.tga` (21 distinct); 3 empty | Icon of the planet's native species or unit (data). Not read. |
| `Show_Hero_Head` | 9 objects (generic heroes 7, hero companies 2) | `Yes` | Show the hero portrait for a non-named hero (name; comment says these are not named heroes). Not read. |
| `Show_In_Sidebar_When_Complete` | 188 objects (upgrades 187) | `Yes` 147, `No` 41 | Keep a researched upgrade's icon in the sidebar (name). Not read. |
| `Show_In_Sidebar_Tray` | 9 upgrades (corruption) | `False` | Hide the upgrade from the sidebar tray (name). FoC only. |
| `Show_Name` | 112 objects: planets 65, containers 47 | `Yes` 64, `No` 48 | Draw the object's name label on the galactic map (data). Not read. |
| `Name_Adjust` | 67 objects: planets 65, containers 2 | 3 floats (`-12, -12, 0.1` 51) | Offset of that name label (name). Not read. |
| `Ranking_In_Category` | 146 objects (units 117) | 0-25 | Order among objects of one category, e.g. which icon represents a fleet (comment for one case; otherwise name). Not read. |
| `Include_In_UI_Map_Header` | 62 objects (multiplayer structure markers 48, markers 11) | bool (`false` 33, `true` 29) | Show the marker on the map preview in the skirmish lobby (name). FoC only. |

#### Radar

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Is_Visible_On_Radar` | 603 objects: structures 263, units 197, props 95, containers 47; 12 repeat it | `Yes` 493, `No` 88, `True` 34 | Show the object on the radar map (name). Not read. |
| `Is_Visible_On_Enemy_Radar` | 3 heroes (Obi-Wan `No`, Luke and Yoda `Yes`) | bool | Show the object on enemy radars; Obi-Wan is hidden from enemies but seen by allies (comment). Not read. |
| `Visible_On_Radar_When_Fogged` | 45 objects (star bases 33) | `true` 28, `False` 17 | Keep the radar blip under fog of war (name). Not read. |
| `Radar_Icon_Name` | 100 objects: props 48, structures 43, units 9 | `.tga` (`i_radar_planet.tga` 37, `mini_map_structure.tga` 16); 4 empty | Radar icon texture (data). Not read. |
| `Radar_Icon_Size` | 276 objects (structures 254); 2 repeat it | two floats (`0.1  0.1` 125) | Radar icon size (name). Not read. |
| `Radar_Icon_Scale_Land` | 70 objects (containers 47) | `80` on containers, `1.0` on units | Icon scale in land radar; for containers the icon's actual world size, since containers have no stable radius (comment). Not read. |
| `Radar_Icon_Scale_Space` | same 70 | `200` on containers, `1.0` on units | As above for space (comment). Not read. |
| `Radar_Range_Icon_Name` | 12 objects (structures 7, markers 5) | `i_radar_shield.tga` 6, `i_radar_landing.tga` 5, one `i_radar_shield.tga.tga` | Icon showing a range on the radar, e.g. while a shield is up (comment). One value has a doubled extension (data). Not read. |
| `Radar_Rotate_Icon` | 5 objects (star bases) | `Yes` | Rotate the icon with the object (name). Not read. |
| `Radar_Show_Facing` | 18 objects | `No` | Hide the facing indicator on the radar (name). Not read. |
| `Radar_Clip_To_Visible_Region` | 1 (Death Star) | `Yes` | Clip the blip to the visible radar region (name). Not read. |
| `Radar_Register_As_Foreground_Object` | 2 (Death Star, Eclipse) | `Yes` | Draw the blip above others (name). Not read. |
| `Begin_Control_Transition_Radar_Event` | 6 objects (reinforcement markers 5, 1 structure) | `DEFAULT_CONTROL_POINT_TRANSITION` | Radar event played when capture of the point starts (name). Not read. |
| `End_Control_Transition_Radar_Event` | same 6 | same | Radar event when capture ends (name). Not read. |

#### Text and encyclopedia

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Text_ID` | 2,327 objects of every group (props 991, structures 404, units 292); 1 repeats it | text key (684 distinct); `TEXT_NONE` 729, empty 290 | Display name key in the DAT string table (data). Not read by the engine (localization tools read DAT files separately). |
| `Encyclopedia_Text` | 989 objects (structures 265, containers 224, units 197, upgrades 189, planets 64); 2 repeat it | text keys, often several separated by spaces or commas (506 distinct) | Description lines of the tooltip/encyclopedia panel, shown in order (data). Not read. |
| `MP_Encyclopedia_Text` | 99 objects (structures 98) | text keys | Multiplayer variant of the description (name, data). Most keys start with `MP_TEXT_` and some reuse single-player `TEXT_` keys (data). Not read. |
| `Encyclopedia_Unit_Class` | 939 objects | text key (63 distinct; `TEXT_ENCYCLOPEDIA_CLASS_STRUCTURE` 244) | Class line of the panel (data). Not read. |
| `Encyclopedia_Good_Against` | 226 objects (units 119, containers 97) | space-separated object names (126 distinct); 5 empty | Units listed as countered by this one (data). Not read. |
| `Encyclopedia_Vulnerable_To` | same 226 | space-separated object names; 4 empty | Units listed as countering this one (data). Not read. |
| `Localized_Name` | 10 black-market items | text key | Display name of the item (data). FoC only. |
| `Localized_Description` | same 10 | text key | Description of the item (data). FoC only. |
| `Used_By_Units` | same 10 | unit names (list for 2) | Units whose panel lists the item, or that benefit from it (name, data). FoC only. |
| `Friendly_Spawn_Text` | 21 objects (spawn houses 20) | text key | Objective text when the indigenous spawner is friendly (data). Not read. |
| `Enemy_Spawn_Text` | 29 objects (spawn houses 27) | text key or list (one leads with `TEXT_DUMMY`) | Objective text when it is hostile (data). Not read. |

#### Planets (galactic map)

All on the 64 `Planet` objects in `Planets.xml`. None are read by the engine.

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Describe_Advantage` | 64 planets | text key | Planet panel line: bonus for the owner (data). Several planets reuse another planet's key (Bonadan's 7 times) (data). |
| `Describe_History` | 64 planets | text key | Planet panel line: history (data). |
| `Describe_Tactical` | 64 planets | text key | Planet panel line: tactical notes (data); some point at other planets' ADV or ENCYC keys. |
| `Describe_Terrain` | 64 planets | text key | Planet panel line: terrain (data). |
| `Describe_Weather` | 64 planets | text key | Planet panel line: weather (data). |
| `Describe_Wildlife` | 64 planets | text key | Planet panel line: wildlife (data). |
| `Encyclopedia_Weather_Icon` | 64 planets | `i_pa_weather_*.tga` (rain 34, sun 12) | Weather icon in the battle-preview panel (data). |
| `Encyclopedia_Weather_Name` | 64 planets | text key (5 distinct) | Weather name (data). |
| `Encyclopedia_Weather_Info` | 64 planets | text key (5 distinct) | Weather effect description (data). The 5 planets with the `none` icon still name a rain or sun description (data). |
| `Zoomed_Terrain_Index` | 64 planets | int 0-12 | Index of the terrain art shown when zooming into the planet (name). |

#### Tactical cameras

All on `TacticalCamera` objects (`Land_Mode`, `Space_Mode`, `Unlocked`) in `TacticalCameras.xml`, not on units. Many values carry a C-style `f` suffix, which a reader must accept. Comments beside several are old values, not explanations. None are read by the engine, which has its own camera (a candidate to read next).

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Distance_Default` | 3 cameras | 460 / 1,000 / 900 | Starting distance from the focus point (name). |
| `Distance_Min` | 3 | 170 / 200 / 0 | Closest zoom (name). |
| `Distance_Max` | 3 | 460 / 1,900 / 60,000 | Farthest zoom; land default and max are equal, so land starts fully zoomed out (data). |
| `Distance_Per_Mouse_Unit` | 3 | 100 / 500 / 50 | Zoom step per wheel or mouse unit (name). |
| `Distance_Smooth_Time` | 3 | 0.21 / 0.1 / 0.1 | Smoothing time of zoom changes, seconds (name). |
| `Distance_Spline` | 1 (land) | 6 numbers: fraction, distance pairs | Maps zoom fraction to distance (data); comment says it compensates a zoom-in creep bug (comment). |
| `Spline_Steps` | 1 (land) | `15` | Sampling steps of the splines (name); same bug-workaround comment. |
| `Fov_Default` | 3 | 55 | Field of view, degrees (name). |
| `Fov_Min` | 3 | 25 (land) / 55 | Narrowest FOV (name). |
| `Fov_Max` | 3 | 55 | Widest FOV (name). |
| `Fov_Per_Mouse_Unit` | 3 | 0 | FOV change per mouse unit; disabled (data). |
| `Fov_Smooth_Time` | 3 (spelt `Fov_Smooth_time` once) | 0.05 / 0.1 | Smoothing of FOV changes (name). Mixed case confirms tags are case-insensitive. |
| `Pitch_Default` | 3 | 50 / 50 / 34 | Starting pitch, degrees (name). |
| `Pitch_Min` | 3 | 50 / -10 / 0 | Lowest pitch (name). |
| `Pitch_Max` | 3 | 50 / 85 / 80 | Highest pitch; land is fixed at 50 (data). |
| `Pitch_Per_Mouse_Unit` | 3 | 0 / -1.5 / -1.5 | Pitch change per mouse unit; land disallows (data). |
| `Pitch_Per_Zoom_Unit` | 3 | -30 (land) / 0 | Pitch change as zoom changes (name). |
| `Pitch_Spline` | 1 (land) | 8 numbers: fraction, pitch pairs (27 to 55) | Maps zoom fraction to pitch (data). |
| `Pitch_When_Zoomed_In` | 3 | 15 / 50 / 34 | Pitch at closest zoom (name). |
| `Pitch_Zoom_Begin_Fraction` | 3 | 0.1 (land) / -1 | Zoom fraction where pitch starts to follow zoom; -1 disables (comment, data). |
| `Yaw_Default` | 3 | 0 | Starting yaw (name). |
| `Yaw_Min` | 3 | -1,000 / -1,000 / -360 | Yaw limit; ±1,000 is effectively unlimited (data). |
| `Yaw_Max` | 3 | 1,000 / 1,000 / 360 | As above. |
| `Yaw_Per_Mouse_Unit` | 3 | 1.5 | Yaw change per mouse unit (name). |
| `Near_Clip` | 3 | 10 / 10 / 1 | Near clipping plane (name). |
| `Far_Clip` | 3 | 7,000 / 7,000 / 60,000 | Far clipping plane (name). |
| `Object_Fade_Begin` | 3 | 1,100 / 6,000 / 10,000 | Camera distance where objects start fading out (name); see `Exclude_From_Distance_Fade`. |
| `Object_Fade_End` | 3 | 1,300 / 7,000 / 11,000 | Distance where they are fully faded (name). |
| `Location_Height_Smooth_Time` | 2 (space, unlocked) | 0.1 | Smoothing of the focus point's height over terrain (name). |
| `Location_Height_Up_Smooth_Time` | 1 (land) | 0.3 | Same, when rising (name). |
| `Location_Height_Down_Smooth_Time` | 1 (land) | 1.0 | Same, when falling (name). |
| `Top_Bounds_Buffer` | 2 (land 0, space -300) | float | Margin between camera movement limit and map edge (name). |
| `Bottom_Bounds_Buffer` | 2 | same | As above. |
| `Side_Bounds_Buffer` | 2 | same | As above. |

#### Lights, lens flares, and terrain decals

`LightSource` (1 object in `LightSources.xml`), `LensFlare` (1, FoC only), and `Decal` (19 in `TerrainDecalFX.xml`) are effect definitions referenced by name, not units.

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Diffuse` | 1 light source | RGB 0-1 (`0,1,0`) | Light color (data). The engine_reads hit is the unrelated material parameter of the same name; the XML light is not read. |
| `Intensity` | 20 effects: decals 19, light 1 | 0.15-1.0 | Light brightness, or decal opacity (data). Not read. |
| `Radius` | 1 light | 40 | Light range (name). Not read. |
| `Falloff_Start` | 1 light | 20 | Distance where light starts to fall off (name). Not read. |
| `Blob_Radius` | 1 light | 30 | Radius of the additive terrain glow under the light, often smaller than the light (comment). Not read. |
| `Blob_Color` | 1 light | RGB | Color of that glow (name). Not read. |
| `Blob_Intensity` | 1 light | 0.15 | Strength of that glow, meant to be subtle (comment). Not read. |
| `Flare` | 1 lens flare, repeated 12 times | element with nested `Diffuse` (RGB), `Radius` (0.1-2.0), `Fraction` (-0.1 to 2.0, position along the sun-to-centre line), `Slot_X`/`Slot_Y` (0-1) and `Num_Slots` (2, a 2x2 texture atlas cell) | One flare element per repeat (data; comments group them into thirds and end rings). FoC only. The engine_reads hit for `Flare/Diffuse` is the material parameter, not this. Not read. |
| `Category` | 19 decals | `Footprints` 18, `DEFAULT` 1 | Decal category, probably sharing a budget or atlas (name). Not read. Despite the name, not the object category system. |
| `Render_Mode` | 19 decals | `DECAL_BUMP_ALPHA` 11, `DIFFUSE_ALPHA` 7, `ADDITIVE` 1 | Blend and shading mode of the decal (name, data). Not read. |
| `UV_Slot` | 19 decals | 2 or 4 ints (`0,0`; `0,3, 1,3` lists alternatives) | Atlas cell(s) the decal texture comes from; four values give two cells to pick from (data). Not read. |
| `Slot_Scale` | 1 decal (`EXPLOSION_SCORCH_MASSIVE`) | `2` | Cell spans 2x2 atlas slots (name). FoC only. |
| `Z_Angle` | 19 decals | `0` | Fixed rotation of the decal (name). |
| `Random_Rotate` | 7 decals (explosion scorches) | `Yes` | Randomize the decal rotation (name). Not read. |
| `Random_Size_Offset` | 7 decals | 0.1-0.2 | Random size variation fraction (name). Not read. |
| `Fadeout_Time` | 19 decals | int 0-99 (`30` 8) | Time before the decal fades, likely seconds (name). Not read. |

#### Wind disturbance

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Wind_Disturbance_Radius` | 23 objects: units 22 (vehicles 11, fauna 8), prop 1 | 50-300 | Radius in which the object bends grass and foliage, e.g. hover and flying craft (name, data). Not read. |
| `Wind_Disturbance_Strength` | same 23 | 4-100 | Strength of that disturbance (name). Not read. |
| `Wind_Disturbance_Sphere_Alpha` | same 23 | 0.1-0.5 | Falloff or opacity of the disturbance sphere (name; unknown). Not read. |

#### Other tags assigned to this family

These are not presentation tags; they appear here because the classifier grouped them by name. Short rows only.

| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Ambient` | 1 `AmbientMapSound` object, repeated 3 times | list: sound event, then four numbers | One ambient sound per repeat with ranges (data); comments mark the whole object as stub test data (comment). Belongs to audio. |
| `Play_Conversation_Events` | 1 `Hero_Clash` (`Vader_ObiWan`) | `No` | Suppress dialogue events during the hero clash (name). |
| `Destroy_When_Child_Dies` | 2 space build pads | `True` | Remove the pad when the structure built on it dies (name). |
| `Destroy_When_Used_As_Corruption_Prereq` | 5 Underworld heroes | `No` | Keep the hero when it fulfils a corruption requirement (name). FoC only. |
| `Tactically_Built_Child_Object_Persists` | 1 (abstract mobile defense unit) | `No` | A structure built in battle does not survive the battle (name). FoC only. |
| `Override_Bounty_MP` | 4 objects | 50-1,800 | Bounty credits for killing it in multiplayer, overriding the computed value (name). FoC only. |
| `Override_Bounty_SP` | 5 objects | 0-1,800 | As above in single player (name). FoC only. |
| `Surface_Bombardment_Capable` | 10 objects (space units 8) | bool (`yes` 5, `no`/`No` 5) | Ship can bombard the surface during a land battle (name). |

#### Findings

- Engine reads in this family: `Land_Model_Name`, `Space_Model_Name`, `Variant_Of_Existing_Type`, `Scale_Factor`, `LOD_Bias`, `Land_Model_Anim_Override_Name`, `Specific_Death_Anim_Index`, `CategoryMask`, `Behavior`/`LandBehavior`/`SpaceBehavior` (narrow checks), and the hardpoint tags `Model_To_Attach`, `Attachment_Bone`, `Engine_Particles`, `Type`. The engine_reads flags on `Diffuse`, `Flare/Diffuse`, and `Scale` are false positives: the engine has those words as a material parameter and a particle track name, not as XML tags.
- `Engine_Particles` holds a bone name (`HP_E_MAINENGINES`), not a particle type; the engine already reads it as a bone, which matches the data.
- Disabled tags: `xxxSpace_Model_Name` (17 death clones) is a model switched off by renaming. The tag reader must not match it loosely, or clones would get wrecked models the authors disabled.
- Likely ignored stray spellings: `Model_Name` (1, a MiscObject), `GUI_Model` (1, beside the 119 `GUI_Model_Name`), `Scale` on 2 space props that also set `Scale_Factor`, and one `Radar_Range_Icon_Name` with a doubled `.tga.tga`. One `Asteroid_Damage_Hit_Particles` writes `large_Damage_Space` in lower case, which is harmless because names are case-insensitive.
- Contradictory repeats: two huge-asteroid props set `Idle_Anim_00_Rate_Mod` twice (8.0 then 1.0); `Select_Box_Scale` repeats up to 3 times in 5 objects; `Blob_Shadow_*`, `GUI_Bracket_Size`, `Is_Visible_On_Radar`, `Icon_Name` each repeat in a few objects. Which occurrence wins (first or last) has not been confirmed for the original, so these are good test cases.
- FoC only: `LOD_Bias`, `LensFlare`/`Flare`, `In_Midground`, `In_Midforeground`, `Slot_Scale`, `GUI_Bias`, `Not_Really_Selectable`, `Include_In_UI_Map_Header`, `Show_In_Sidebar_Tray`, black-market `Localized_Name`/`Localized_Description`/`Used_By_Units`, `Cinematic_Object_Only`, and the bounty and corruption tags. Installed only (patch): `Engine_Hardpoint` in a cinematic hardpoint file.
- Damage states: `Land_Damage_Thresholds` and `Land_Damage_Alternates` (219 objects, parallel lists) are not read. The engine keeps `_ALT<n>` meshes but still needs a health-driven state switch that reads both lists and treats the build-pad form (`1` with `3`) correctly.
- Death animation: the engine reads `Specific_Death_Anim_Index` but not `Specific_Death_Anim_Type` (FW_DIE, FL_DIE, Crushed, FIRE_DIE...). 79 of the 102 index values are empty, and an empty value reads as variant 0. Damage-type clones need the type to choose their clip.
- `Land_Terrain_Model_Mapping` (3 stock infantry) swaps models by terrain type, for snow and sand troopers. The engine always draws `Land_Model_Name`, so Arctic, Desert, and Volcanic maps show the wrong trooper skins.
- The detail-cutoff tags fit the graphics presets: `Blob_Shadow_Below_Detail_Level` matches `ShadowDetail` 0-3, `No_Reflection`/`No_Refraction` match `WaterDetailLevel` 0-2, and `No_Particle_Below_Detail_Level` matches `ParticleDetail` 0.4-1.0. The value 99 means "never". This mapping is inferred from value ranges, not confirmed.
- Distance fade: `TacticalCamera` `Object_Fade_Begin`/`End` (land 1,100-1,300) and the per-object `Exclude_From_Distance_Fade` (345 objects, mostly backdrops and skydomes) go together; a renderer that adds distance fading must honour the exclusion. GameConstants holds a global `Occlusion_Silhouettes_Enabled`, beside the per-object `Occlusion_Silhouette_Enabled`.
- Numeric parsing: `TacticalCameras.xml` and `Select_Box_Z_Adjust` write floats with a C-style `f` suffix (`3.0f`, `1100.0f`). The engine's strict `from_chars` readers, which require the whole text to parse, would reject these if they ever read these tags.
- The `Type` tag means a hardpoint kind on hardpoints and a movement style (`Walk`, `Fly`, `Hover`, `Tracked`) on units; the same name also appears inside ability blocks. Readers have to scope it by context.
- `TacticalCamera` is the obvious next read for the sandbox camera: the land mode fixes pitch at 50 degrees, zooms only between 170 and 460, and ties pitch to zoom through `Pitch_Spline`. Planet, light, lens-flare, and decal tags belong to the galactic and effects work and are not unit data.

### Sound events and speech

These tags name audio events that an object plays: unit voice responses, weapon and impact sounds, engine and ambient loops, death sounds, health and base warnings, build and production announcements, hardpoint reports, super-weapon cues, and campaign speech and music. Nearly every value is a single event name, resolved against `SFXEvent` definitions in the `SFXEvents*.xml` files (listed by `SFXEventFiles.xml`), against `SpeechEvent` in `SpeechEvents.xml` (corruption, hero-clash, and Death Star build speech), or against `MusicEvent` in `MusicEvents.xml`. Three tags carry lists instead: `Land_Damage_SFX` (one event per damage state) and the hardpoint pairs `SFXEvent_Attack_Hardpoint` / `SFXEvent_Hardpoint_Destroyed` (a `HARD_POINT_*` kind and an event, repeated once per kind). An empty value means no sound. Faction prefixes on the events are consistent: `EHD_`/`RHD_`/`UHD_` are Empire/Rebel/Underworld HUD and advisor voice (data). Counts below are objects (uses where they differ) in the installed FoC data. EAWEngine reads 16 of the 91 tags.

#### Unit responses to orders and selection
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `SFXEvent_Select` | units 213, structures 170, props 11 (394 objects, 396 uses) | event (156 distinct; structures `Structure_*_Select_SFX`, units `Unit_Select_<type>`); 6 empty; 2 objects repeat it with the same value (TIE_Bomber, TIE_Bomber_Bombing_Run) | voice or click played when the object is selected (name, data). Engine: read from the type (first value through the variant chain) and played when the sandbox selects a unit. |
| `SFXEvent_Move` | units 214, props 10, containers 1 (225) | event `Unit_Move_<type>` (133 distinct); 6 empty | response to a move order (name). Engine: played on sandbox move orders. |
| `SFXEvent_Attack` | units 213, structures 16 (star bases), props 11 (240) | event `Unit_Attack_<type>` (132 distinct); 8 empty | response to an attack order (name). Engine: played on sandbox attack orders. |
| `SFXEvent_Guard` | units 200, props 5 (205) | event `Unit_Guard_<type>` (120 distinct); 4 empty | response to a guard order (name). Engine: played on sandbox guard orders. |
| `SFXEvent_Stop` | space units 18, unique units 1 (19) | event `Unit_Stop_<ship>` (13 distinct) | response to a stop order, capital ships and frigates only (name, data). Not played. |
| `SFXEvent_Move_Opposite` | 1 space unit (Generic_Star_Destroyer) | `Unit_Move_Opposite_Star_Destroyer` | voice response, presumably to a move order that reverses the ship's heading (name). Not played. |
| `SFXEvent_Fleet_Move` | units 140, containers 66 (ground and hero companies), props 4 (210 objects, 213 uses) | event `Unit_Fleet_Move_<type>` (109 distinct); 7 empty; 3 objects repeat it with the same value | response when the unit or its fleet is sent somewhere; set on ground companies and infantry too, so not a space-only order. The owner's guess, already in compatibility-research: the galactic-map fleet move (unverified). Not played. |
| `SFXEvent_Group_Move` | heroes 16, generic heroes 5, unique 2, props 2 (25) | event (16 distinct; some reuse `Unit_Assist_Move_Sabateur`, `Unit_Move_Bossk`) | a hero's move response when ordered with a group (name). Not played. |
| `SFXEvent_Group_Attack` | same 25 objects as `SFXEvent_Group_Move` | event (16 distinct) | a hero's attack response when ordered with a group (name). Not played. |
| `SFXEvent_Assist_Move` | infantry 30, vehicles 27, indigenous 24, unique 13, slaves 8, props 7, space 1 (110) | event `Unit_Assist_Move_<type>` (52 distinct); 14 empty | move response of a unit in a supporting role; exact trigger unknown (name). Not played. |
| `SFXEvent_Assist_Attack` | same 110 objects as `SFXEvent_Assist_Move` | event `Unit_Assist_Attack_<type>` (52 distinct); 14 empty | attack counterpart of `SFXEvent_Assist_Move`; trigger unknown (name). Not played. |
| `SFXEvent_Attack_With_Non_Hero_Ability` | 1 space unit (Broadside_Class_Cruiser) | `Unit_Barrage_Broadside_Cruiser` | response when attacking through a unit (non-hero) ability (name). Not played. |
| `SFXEvent_Barrage` | space units 24, star bases 18, unique 3, props 2 (47) | event `Unit_Barrage_<type>` (23 distinct) | response to the area-fire (barrage) order (name, data: space ships and stations only). Not played. |
| `SFXEvent_Attacked` | 1 hero (Droid_R2D2) | `Unit_Attacked_C3PO` | chatter when the droid pair is attacked (data: the event is a 3D ambient-preset event with R2 post-samples). Not played. |
| `SFXEvent_Announce` | 1 hero (General_Veers) | `Unit_Announce_Veers` | hero announcement. The exact name is not defined; events `Unit_Announce_Veers_<soldier type>` exist (Rebel/Storm Trooper/Underworld soldiers), so either the game appends a listener suffix (data, inference) or the tag is dead. Not played. |
| `SFXEvent_Deploy` | heroes 4, unique 4 (8) | event (`Unit_Deploy_Han_Solo`, `Unit_Deploy_Boba_Fett`); 2 empty | response when the unit deploys or lands (name). Not played. |
| `SFXEvent_Draw_Weapon` | heroes 6, props 1 (7) | always `Unit_Light_Saber_On` | lightsaber ignition when the hero draws its weapon (comment: lightsaber holstering data). Not played. |
| `SFXEvent_Holster_Weapon` | same 7 objects | always `Unit_Light_Saber_Off` | lightsaber switching off on holstering (comment). Not played. |
| `SFXEvent_Hero_Respawned` | hero companies 27, unique 5, squadrons 2, props 1 (35) | event (28 distinct; `Unit_Respawn_<hero>`, some reuse select or galactic announcements) | announcement when a hero becomes available again after dying (name, data). Not played. |
| `Play_SFXEvent_On_Sighting` | heroes 16, generic heroes 5, unique 5, props 3, space 1 (30) | bool, always `True` | presumably plays a sound or announcement when the unit is first sighted; which event is unknown (name). Not read. |
| `SFXEvent_Cough_Override` | 1 infantry (Vornskr_Wolf) | `Unit_Coughing_Wolf` | FoC only. Replaces the default cough sound, presumably of infantry in a gas or poison effect (name; the event is a death-preset cough). Not played. |
| `SFXEvent_Move_Into_Nebula` | space units 42, unique 4, props 1 (47) | event `Unit_Nebula_<type>` (36 distinct) | voice line when the ship enters a nebula (name). Not played. |
| `SFXEvent_Move_Into_Asteroid_Field` | space units 25, unique 4, props 1 (30) | event `Unit_Asteroids_<type>` (24 distinct) | voice line when the ship enters an asteroid field (name). Not played. |
| `SFXEvent_Damaged_By_Asteroid` | space units 11, unique 3, props 1 (15) | always `SFX_Asteroid_Detonation` | impact sound when an asteroid hits the ship (name). Not played. |

#### Combat and weapon sounds
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `SFXEvent_Fire` | units 152, structures 45, props 7, projectiles 5 (209) | event (55 distinct; `Unit_Rebel_Infantry_Fire`, `Unit_Turbo_Fire`, ...); 4 empty | sound of the object's own weapon firing (name; a comment names the Death Star's super-weapon fire). Engine: `combat_content` stores its name CRC on each weapon as the fallback; `BattleAudio` plays one per shooter and event per tick at the shot origin. |
| `Fire_SFXEvent` | hardpoints 279 | event (23 distinct; `Unit_Turbo_Fire` 60, `Unit_Ion_Fire` 37, ...) | the firing sound of a weapon hardpoint (name, data). Engine: read per hardpoint in `combat_content`, overriding the owner's `SFXEvent_Fire`. |
| `Projectile_SFXEvent_Detonate` | projectiles 86 (87 uses) | event (20 distinct; `SFX_Concussion_Missile_Detonation` 18, ...); 2 empty; repeated once with the same value (Proj_Ship_Krayt_Megaweapon_Ion) | impact sound of the projectile (name; a comment marks one as a placeholder). Engine: played at the impact of every hit that is not armor-reduced; shots that run out of range are silent (an engine choice). |
| `Projectile_SFXEvent_Detonate_Reduced_By_Armor` | projectiles 61 (62 uses) | event (5 distinct: `SFX_Small_Damage_Detonation` 41, `Unit_Projectile_Infantry_Ricochet` 18, ...); repeated once (same projectile) | impact sound when armor reduced the hit, e.g. a ricochet off armor (name, data). Engine: played for `hull_reduced` impacts, falling back to `Projectile_SFXEvent_Detonate`. |
| `Lobbing_Superweapon_SFXEvent_Chargeup` | 1 structure (Ground_Magnepulse_Cannon) | `Structure_Magnepulse_Charge` | charge-up sound of a ground super-weapon that fires a lobbed shot (name). Not played. |
| `Lobbing_Superweapon_SFXEvent_Fire` | same structure | `Unit_Magnepulse_Fire` | firing sound of that super-weapon (name). Not played. |

#### Engine and ambient loops
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `SFXEvent_Engine_Moving_Loop` | units 75, props 10, projectiles 2 (87 objects, 88 uses) | looping event (34 distinct; `Unit_<type>_Moving_Engine_Loop`, lightsaber and jet-pack loops on heroes); repeated once with the same value | loop while the unit moves (name; comments about Boba Fett's jet pack). Engine: played while the entity moves, among the 16 loops nearest the listener. |
| `SFXEvent_Engine_Idle_Loop` | units 69, props 9, projectiles 2 (80 objects, 81 uses) | looping event (29 distinct); 1 empty; repeated once | loop while the unit stands still (name). Engine: played instead of the moving loop while the entity is still; a type with only a moving loop is silent when still (whether the original falls back is unknown). |
| `SFXEvent_Engine_Cinematic_Focus_Loop` | units 79, structures 9, props 3 (91 objects, 93 uses) | looping event (33 distinct; `Unit_<type>_Cinematic_Engine_Loop`); 3 empty; TIE_Scout has an empty value then a real one | loop used when a cinematic camera focuses on the unit (name). Not played. |
| `SFXEvent_Ambient_Loop` | structures 51, units 33, props 4 (88) | looping event (18 distinct; `Unit_Starbase_Engine_Loop` 21, `Structure_Barracks_Loop` 21, ...); 4 empty | a continuous sound of the object whether it moves or not (name, data: stations, buildings, some ships). Engine: played either way, in the same nearest-16 pool. |
| `XSFXEvent_Ambient_Loop` | 1 land bombing unit (TIE_Bomber_Bombing_Run) | `Unit_TIE_Fighter_Engine_Loop` | `SFXEvent_Ambient_Loop` disabled by an `X` prefix (data); the named event is not defined either. Ignored by the game presumably and by the engine. |
| `SFXEvent_Ambient_Moving` | units 56, props 2 (58) | one-shot event (27 distinct; fly-bys `Unit_X_Wing_Fly_By` 11, creature chatter `Ambient_<species>_Random_Chatter`); 8 empty | a positional one-shot repeated at random intervals while the unit moves (comment: each fighter plays a 3D fly-by every X to Y seconds; the `AMBIENT_SFX` behavior must be in the behavior list; 37 of the 50 non-empty users list it in their own definition, 13 do not or inherit it). Engine: played every min..max seconds while moving, first after one wait; does not check `AMBIENT_SFX`. |
| `SFXEvent_Ambient_Moving_Min_Delay_Seconds` | 56 (units 54, props 2) | integer seconds 1-7 (5 on 44) | lower bound of the wait between `SFXEvent_Ambient_Moving` plays (comment). Engine: read as float, clamped to at least 0. |
| `SFXEvent_Ambient_Moving_Max_Delay_Seconds` | same 56 | integer seconds 1-12 (10 on 42) | upper bound of that wait (comment). Engine: clamped to at least the minimum; 0 disables the sound. |
| `SFXEvent_Turret_Rotating_Loop` | structures 27, units 9, props 1 (37) | `Structure_Turret_Motor` 25, `Vehicle_Turret_Motor` 8, `Structure_Turret_Motor_Turbo` 4 | motor loop while a turret turns to aim (comment). Not played. |
| `SFXEvent_Powered_Active_Loop` | 3 structures (base shield generators) | `Structure_Shutter_Shield_Generator_Loop` | loop while the structure is powered and active (name, data). Not played. |
| `SFXEvent_Debug_Test_Loop` | 1 marker (Debug_Audio_Emitter_Marker) | `Debug_Audio_Test_Loop` | developer audio test emitter (comment). Not played. |

#### Death and damage sounds
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Death_SFXEvent_Start_Die` | units 247, structures 239, props 208, projectiles 14 (708 objects, 733 uses) | event (67 distinct; `Unit_Generic_Building_Death_SFX` 198, `Unit_Corvette_Death_SFX` 195, ...); 8 empty; repeats on 25 objects (max 2) | sound when the object starts dying (name). The 25 repeats are duplicate lines, 24 with the same value (power generators, command centers, prisons, an Ewok bomber, the MZ8 tank, ...); only Merchant_Freighter differs (`Unit_X_Wing_Death_SFX` then `Unit_Corvette_Death_SFX`). Engine: the last occurrence (`GameObjectCatalog::field`) played at the death position. |
| `Death_SFXEvent_End_Die` | vehicles 14, unique 4 (18) | event (4 distinct: corvette, AT-ST, Calamari, tank death sounds) | sound at the end of a death sequence, on death clones such as `AT_AT_Walker_Death_Clone_*` (name, data). Not played. |
| `Spin_Away_On_Death_SFXEvent_Start_Die` | space units 14, bombing units 3, unique 3, vehicles 2 (22) | event `Unit_<fighter>_Spinning_By` (5 distinct) | sound of a fighter spinning away when it dies, paired with `Spin_Away_On_Death_Explosion` (name, data). Not played. |
| `Selt_Destruct_SFXEvent_Start_Die` | vehicles 2, space units 2, infantry 1 (5) | `Unit_ZC_Self_Destruct` 3, `Unit_TIE_Mauler_Self_Destruct`, `Unit_Ewok_Death` | sound when a self-destruct sequence begins (comment). Misspelt (`Selt`), but the only spelling in all three data sets, so presumably what the game parses. Not played. |
| `Land_Damage_SFX` | structures 183, units 27, props 8 (218) | comma list, one per damage state: `null,SFX_Grenade_Detonation, SFX_Grenade_Detonation,null` 134, all-`null` 35, ...; `null` is none | sound on entering each `Land_Damage_Thresholds` state (data; already in compatibility-research). Not played. |

#### Health and base warnings
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `SFXEvent_Health_Low_Warning` | unique 24, heroes 19, space 15, props 4, infantry 1 (63) | event `Unit_Health_Low_<unit>` (42 distinct); 5 empty | the unit's own voice when its health drops low (name); the threshold is presumably GameConstants `Health_Low_Percent_Threshold` 0.33 (name). Not played. |
| `SFXEvent_Health_Critical_Warning` | same 63 objects | event `Unit_Health_Critical_<unit>` (44 distinct); 3 empty | the same at critical health, presumably `Health_Critical_Percent_Threshold` 0.10 (name). Not played. |
| `SFXEvent_Enemy_Health_Low_Warning` | unique 13, heroes 1 (14; Luke, Rogue Squadron pilots) | event `Unit_Enemy_Health_Low_<hero>` (11 distinct); 2 empty | voice line about an enemy at low health (name); exact trigger unknown. Not played. |
| `SFXEvent_Enemy_Health_Critical_Warning` | unique 12, heroes 1 (13) | event (8 distinct); 4 empty; `Unit_Enemy_Health_Critical_Derek_Klivian` is undefined | critical-health counterpart (name). Not played. |
| `SFXEvent_Enemy_Damaged_Health_Low_Warning` | space fighters 7, unique 1 (8) | event `Unit_Enemy_Health_Low_<fighter>` (7 distinct); 1 empty | voice line when this unit has damaged an enemy to low health (name); its relation to `SFXEvent_Enemy_Health_Low_Warning` (heroes) is unknown; both use the same event family. Not played. |
| `SFXEvent_Enemy_Damaged_Health_Critical_Warning` | same 8 | event (7 distinct); 1 empty | critical counterpart (name). Not played. |
| `SFXEvent_Unit_Under_Attack` | structures 102, props 2 (104) | advisor event `<faction>HD_<building>_Under_Attack` (52 distinct); 1 empty | advisor warning to the owner that the structure is under attack (name, data); AUDIO.XML `Delay_Between_Land/Space_Base_Attack_Announcement_Seconds` 60 presumably throttles it, and FACTIONS.XML adds faction-wide base-under-attack events (name). Not played. |
| `SFXEvent_Unit_Lost` | structures 86, units 35, props 2 (123) | advisor event `<faction>HD_<building>_Destroyed`, `..._Unit_Destroyed` (59 distinct); 8 empty | advisor report to the owner that the object was destroyed (name, data). Not played. |

#### Build and production announcements
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `SFXEvent_Build_Started` | structures 204, upgrades 186, containers 102, units 46, props 5 (543) | advisor event (43 distinct; `EHD_Build_Structure` 74, `UHD_Upgrade_Progress` 64, `*_Request_Assistance`, ...) | announcement when the item enters a production queue (name, data: galactic and skirmish builds, upgrades, companies). Not played. |
| `SFXEvent_Build_Complete` | upgrades 186, structures 173, containers 102, units 46, props 5 (512) | event (115 distinct; `EHD_Construction_Complete` 82, ...; heroes and ships use their own lines such as `Unit_Complete_A_Wing`) | announcement when production finishes (name). Not played. |
| `SFXEvent_Build_Cancelled` | upgrades 186, structures 170, containers 102, units 42, props 5 (505) | advisor event (23 distinct; `EHD_Building_Canceled` 78, ...) | announcement when production is cancelled (name). Not played. |
| `SFXEvent_Tactical_Build_Started` | units 44, containers 11, props 1 (56) | `<faction>HD_Reinforcements_En_Route` (53), `Unit_Produce_Troops_Arc_Hammer` 3 | tactical (in-battle) production or reinforcement started (name, data). Not played. |
| `SFXEvent_Tactical_Build_Complete` | same 56 | `<faction>HD_Reinforcements_Available` (53), `Unit_Complete_Troops_Arc_Hammer` 3 | tactical production finished (name, data). Not played. |
| `SFXEvent_Tactical_Build_Cancelled` | same 56 | `<faction>HD_Reinforcements_Cancelled` only | tactical production cancelled (name, data). Not played. |
| `SFXEvent_Sold_Tactical` | structures 29 (turrets, bacta tanks, repair stations, sensor nodes) | advisor event `<faction>HD_<structure>_Sold` (12 distinct; one uses `UHD_Turret_Build`) | announcement when a tactical build-pad structure is sold (name, data). Not played. |
| `SFXEvent_Special_Weapon_Ready` | structures 9, upgrades 3, props 1 (13) | advisor event (`*_Turbo_Tower_Available`, `*_Ion_Cannon_Available`, `EHD_Hypervelocity_Available`, ...) | announcement that a special weapon (turbolaser tower, ion cannon, hypervelocity gun) is ready to fire (name, data). Not played. |
| `SFXEvent_Beacon_Placed` | 1 misc object (R_Multiplayer_Beacon) | `GUI_Toggle_Lure_On` | GUI sound when a multiplayer beacon is placed (name). Not played. |

#### Hardpoint sounds
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `SFXEvent_Attack_Hardpoint` | space units 42, star bases 15, unique 4, props 1 (62 objects, 412 uses) | pair `HARD_POINT_<kind>, <event>`; repeats once per hardpoint kind (max 7: laser, missile, torpedo, ion cannon, shield generator, engine, gravity well) | this unit's voice when ordered to target an enemy hardpoint of that kind (data: values are the attacker's own VO, e.g. `Unit_HP_LASER_Corvette` on the corvette). Not played. |
| `SFXEvent_Hardpoint_Destroyed` | star bases 15, space units 14, unique 5, props 2 (36 objects, 352 uses) | pair `HARD_POINT_<kind>, <event>`, the event often blank; repeats once per kind (max 10, incl. tractor beam, gravity well, fighter bay, special ability) | report in the unit's voice that it lost a hardpoint of that kind (data, e.g. `Unit_Lost_Laser_Rebel_Space_Station`). Not played. |
| `SFXEvent_Hardpoint_All_Weapons_Destroyed` | 2 star bases (Rebel_Star_Base_1, Underworld_Star_Base_1) | empty in both | report when every weapon hardpoint is gone (name); no stock event set. Not played. |
| `SFXEvent_Hardpoint` | 1 bombing unit, 1 space unit (Z-95_Headhunter) | `Unit_Hardpoint_TIE_Bomber`, `Unit_Hardpoint_TIE_Fighter` | unknown; both events are undefined, so the tag is probably ignored or dead (data). Not played. |
| `Death_Explosion_SFXEvent` | hardpoints 259 | event (13 distinct; `Unit_Hardpoint_Turbo_Laser_Death` 76, ...); 1 empty | sound of a hardpoint's destruction, beside its `Death_Explosion_Particles` (name, data; comments mark many as placeholders). Engine: read from the dying unit's own type, where stock never sets it, and played on unit death; so it never plays on stock data, and hardpoint destruction is silent. |

#### Special weapons and bombing runs
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `SFXEvent_Bomb_Run_Select_Target` | 3 land bombing units | `Unit_Select_Y_Wing`, `Unit_Select_Tie_Bomber`, `Unit_Select_Skipray` | voice when the player picks a bombing-run target (name). Not played. |
| `SFXEvent_Bomb_Run_Incoming` | same 3 | `Unit_Bombing_Run_Y_Wing`, `Unit_Bombing_Run_Tie_Bomber`, `Unit_Attack_Skipray` | voice as the bombers come in (name). Not played. |
| `SFXEvent_Bombard_Select_Target` | space 7, unique 5, props 1 (13) | `EHD_`/`RHD_`/`UHD_Bombard_Select_Target` | FoC only. Advisor voice when a target is picked for orbital bombardment by this ship (name). Not played. |
| `SFXEvent_Bombard_Incoming` | same 13 | `EHD_`/`RHD_`/`UHD_Bombard_Incoming` | FoC only. Advisor voice as the bombardment arrives (name). Not played. |
| `TSW_SFXEvent_Activate_Voice` | 1 space unit (Death_Star) | `EHD_Death_Star_Activate` | advisor voice when the tactical super-weapon (TSW, name) is activated (name). Not played. |
| `TSW_SFXEvent_Weapon_Power_Up` | Death_Star | `GUI_Death_Star_Activate` | power-up sound of the super-weapon (name). Not played. |
| `TSW_SFXEvent_GUI_Bttton_Press` | Death_Star | `GUI_Death_Star_Button_Press` | GUI sound of the super-weapon button; misspelt (`Bttton`) in all data sets, so presumably the parsed name. Not played. |
| `TSW_MusicEvent_Activation_Start` | Death_Star | `Space_Map_Activate_Death_Star_Music_Event` (a `MusicEvent`) | music started on activation (name). Not played. |
| `TSW_Post_Music_Wait_Frames` | Death_Star | integer 1 | frames to wait after that music; a comment says it is no longer used. |

#### Speech and music
| Tag | Used by | Values | Meaning |
|---|---|---|---|
| `Build_Speech_Underway` | 1 space unit (Death_Star) | `SpeechEvent` `Speech_Death_Star_Build_Underway` | galactic speech when the Death Star's construction starts (name). Not played (no MP3 decoder yet). |
| `Build_Speech_Completed` | Death_Star | `SpeechEvent` `Speech_Death_Star_Build_Complete` | speech when it is finished (name). |
| `Build_Speech_Stopped` | Death_Star | `SpeechEvent` `Speech_Death_Star_Build_Stopped` | speech when construction stops (name). |
| `Build_Speech_Countdowns` | Death_Star | prefix `Speech_Death_Star_Build_` | a name prefix, not an event: the game appends a remaining time to reach events such as `..._10_Minutes`, `..._60_Seconds` (comment, data). |
| `Build_Music_Completed` | Death_Star | `MusicEvent` `Galactic_Death_Star_Complete_Music_Event` | music when construction completes (name). |
| `Corruption_1_Mission_Commencing_Speech_Event` | planets 29 | `SpeechEvent` `<PLANET>_<org>_LOAD_COR<nn>_<nn>` (25 distinct) | FoC only. Speech when the first corruption mission on the planet starts (name). Not played. |
| `Corruption_1_Success_Speech_Event` | planets 60 | `SpeechEvent` `..._VICTORY_COR..` (54 distinct); 2 empty | FoC only. Speech when the first corruption succeeds (name). |
| `Corruption_1_Failure_Speech_Event` | planets 30 | `SpeechEvent` `..._LOSS_COR..` (25 distinct); 1 empty | FoC only. Speech when it fails (name). |
| `Corruption_2_Success_Speech_Event` | planets 49 | `SpeechEvent` (42 distinct) | FoC only. Second corruption level's success speech (name). |
| `Corruption_2_Failure_Speech_Event` | planets 1 (AlzocIII) | empty | FoC only. Never set to a value. |
| `Corruption_3_Success_Speech_Event` | planets 8 | `SpeechEvent`; 6 of 8 use `CARIDA_BLK_VICTORY_COR11_06` | FoC only. Third level's success speech; the shared Carida line on other planets looks like a copy (data). |
| `Corruption_3_Failure_Speech_Event` | planets 5 | empty | FoC only. Never set to a value. |
| `First_Hero_Win_Speech` | 1 hero clash (Vader_ObiWan) | `SpeechEvent` `Hero_Clash_Obiwan_Vader__Vader_Win` | speech when the first hero of a hero clash wins (name). |
| `Second_Hero_Win_Speech` | same hero clash | `SpeechEvent` `..._Obiwan_Win` | speech when the second hero wins (name). |

#### Findings
- Resolution (installed FoC, comments removed): every named event resolves except `Unit_Announce_Veers` (`SFXEvent_Announce`; only suffixed variants exist), `Unit_Barrage_MC30_Frigate` (`SFXEvent_Barrage` on the MC30), `Unit_Enemy_Health_Critical_Derek_Klivian`, both `SFXEvent_Hardpoint` values, and `Unit_TIE_Fighter_Engine_Loop` on the disabled `XSFXEvent_Ambient_Loop`. `Build_Speech_Countdowns` holds a prefix by design. Speech and music tags resolve to `SpeechEvent` / `MusicEvent`, never `SFXEvent`.
- Case mismatches are common and harmless when lookup ignores case: about 450 references differ from the definition only in case (404 inside `SFXEvent_Attack_Hardpoint` lists, such as `Unit_HP_LASER_Corvette` against `Unit_HP_Laser_Corvette`; `TIE` against `Tie` in fighter responses, warnings, nebula and asteroid lines; `Unit_Hardpoint_ion_Death`, `Unit_ion_Fire`; `RHD_SENSOR_ARRAY_*`). EAWEngine's `SoundEventCatalog::find` lowercases and the shot CRC uppercases, so they resolve. Any new lookup must stay case-insensitive.
- `XSFXEvent_Ambient_Loop` is `SFXEvent_Ambient_Loop` switched off with an `X` (a way to disable a tag without deleting it); its event is undefined as well. Misspellings that are the only spelling, and so presumably the parsed names: `Selt_Destruct_SFXEvent_Start_Die`, `TSW_SFXEvent_GUI_Bttton_Press`.
- Repeats of `Death_SFXEvent_Start_Die` (25 objects) are duplicate lines, not a list: 24 repeat the same value; Merchant_Freighter has two different values. Similar single-object duplicates: `SFXEvent_Select` (2), `SFXEvent_Fleet_Move` (3), engine loops and `Projectile_SFXEvent_Detonate` (1 each), and TIE_Scout's `SFXEvent_Engine_Cinematic_Focus_Loop`, empty first and set second. Whether the original takes the first or the last is unknown; EAWEngine takes the last (`GameObjectCatalog::field`), which matters for Merchant_Freighter (corvette death sound) and TIE_Scout.
- Real repeats carry meaning only in the hardpoint pairs: `SFXEvent_Attack_Hardpoint` and `SFXEvent_Hardpoint_Destroyed` repeat once per `HARD_POINT_*` kind; a reader must collect all of them, keyed by kind, and accept a blank event after the comma.
- Misread in EAWEngine: `Death_Explosion_SFXEvent` is a HardPoint field (259 of 259 uses), but `BattleAudio::type_sounds` reads it from the dying unit's type, so it never plays on stock data. The simulation already counts destroyed hardpoints (`hardpoints_destroyed`); playing this sound, and the `SFXEvent_Hardpoint_Destroyed` report for the owner, needs a hardpoint-destroyed event in the snapshot.
- Played by EAWEngine (16 tags): select/move/attack/guard responses (sandbox orders), `SFXEvent_Fire` with per-hardpoint `Fire_SFXEvent`, both projectile detonations, `Death_SFXEvent_Start_Die`, the moving/idle engine loops and `SFXEvent_Ambient_Loop` (nearest 16), and `SFXEvent_Ambient_Moving` with its delay pair. `SFXEvent_Ambient_Moving` is played without checking the `AMBIENT_SFX` behavior a comment says it needs; 13 of 50 users do not list it in their own definition (some may inherit it), so the engine may play fly-bys the original does not.
- Natural next reads, in battle: `SFXEvent_Stop`, `SFXEvent_Barrage`, `SFXEvent_Turret_Rotating_Loop`, `Land_Damage_SFX` (per damage state), `Death_SFXEvent_End_Die`, `Spin_Away_On_Death_SFXEvent_Start_Die`, the health warnings (thresholds 0.33 / 0.10 in GameConstants), `SFXEvent_Unit_Under_Attack` / `SFXEvent_Unit_Lost` (throttled by AUDIO.XML's 60 s announcement delay), and the tactical build and bombing-run cues. Galactic and campaign-only: `SFXEvent_Build_*`, `SFXEvent_Fleet_Move` (presumably), `SFXEvent_Hero_Respawned`, the corruption and Death Star speech, and the music tags (these need the MP3 decoder).
- The `EAWAssetInspector sound-audit` field check covers only names containing `SFXEvent_` / `_SFXEvent` with a single value: it skips the hardpoint lists, `Land_Damage_SFX`, the X-prefixed tag, and all speech and music tags, so the unresolved `Build_Speech_Countdowns` prefix is not reported and speech references are never checked against `SpeechEvents.xml`.
- Placeholders: developer comments mark many responses, death sounds, hardpoint deaths, and detonations as stubs to be replaced by the sound designer; in shipped data they still name defined events, so they are real content.
- Unused or never-set tags: `TSW_Post_Music_Wait_Frames` (a comment says it is unused), `SFXEvent_Hardpoint_All_Weapons_Destroyed`, `Corruption_2_Failure_Speech_Event`, and `Corruption_3_Failure_Speech_Event` (all empty), and `SFXEvent_Debug_Test_Loop` (a test marker).

## Engine coverage

EAWEngine reads about 95 of the 1,005 tags (2026-11-11). By area:

- **Identity and inheritance:** `Variant_Of_Existing_Type`, `Affiliation` (first faction: preview
  colour, spawn filter), `CategoryMask` and the behavior lists (spawn categories, flight, body
  aiming), `Lua_Script` (object scripts in battles), `AI_Combat_Power` (the Lua combat rating),
  `Company_Units`, `Squadron_Units`, `Squadron_Offsets`.
- **Models and presentation:** `Land_Model_Name`, `Space_Model_Name`, `Galactic_Model_Name`
  (resolved, unused), `Scale_Factor`, `LOD_Bias`, `Land_Model_Anim_Override_Name`,
  `Specific_Death_Anim_Index`, `Death_Persistence_Duration`, `Engine_Particles`, hardpoint
  `Model_To_Attach`, `Attachment_Bone`, `Damage_Particles`, `Damage_Decal`.
- **Movement:** `Max_Speed`, `Max_Rate_Of_Turn`, `OverrideAcceleration`, `OverrideDeceleration`,
  `Min_Speed_Fraction_For_Turn`, `Stopped_Rate_Of_Turn`, `Min_Speed`, `MovementClass`,
  `Space_Layer`, `UnitCollisionClass`, `OverridePassability`, `Custom_Soft_Footprint_Radius`,
  `FormationOrder`, `FormationSpacing`, `Hover_Offset`.
- **Combat:** `Tactical_Health`, `Shield_Points`, `Shield_Refresh_Rate`, `Armor_Type`,
  `Shield_Armor_Type`, `HardPoints` and the hardpoint fire tags (`Type`, `Health`,
  `Is_Targetable`, `Is_Destroyable`, `Fire_Bone_A`, `Fire_Cone_Width`, `Fire_Projectile_Type`,
  `Fire_Min/Max_Recharge_Seconds`, `Fire_Pulse_Count`, `Fire_Pulse_Delay_Seconds`,
  `Fire_Range_Distance`), `Projectile_Types` (first entry), `Projectile_Fire_Recharge_Seconds`,
  `Projectile_Fire_Pulse_*`, `Targeting_Max_Attack_Distance`, `Ranged_Target_Z_Adjust`,
  `Death_Clone`, `Death_Explosions`.
- **Projectiles:** `Projectile_Damage`, `Damage_Type`, `Projectile_Does_Shield_Damage`,
  `Projectile_Does_Hitpoint_Damage`, `Projectile_Max_Flight_Distance`, the render tags
  (`Projectile_Custom_Render`, `_Width`, `_Length`, `_Texture_Slot`, `_Laser_Color`), and the
  detonation particles and sounds.
- **Audio:** the death, engine, ambient, response, fire, and detonation events listed in the
  audio section.

The literal-match survey also flagged `Damage`, `Terrain`, `Scale`, `Faction`, and `Diffuse`;
those are false matches (the same words as a definition kind, a shader parameter, or a particle
track), not tag reads. Numbers are read with `std::from_chars` without requiring the whole value
to parse, so the C float suffix (`0.5f`) is accepted already.

## Implications for EAWEngine

Collected from the sections above, most consequential first. Each is a proposal to check and
schedule, not a decided change; several rest on inference marked in their rows. Done on
2026-11-11 (see [status.md](status.md)): items 1 (without the shield interval), 2 (the fire
cadence; second weapons wait for SWAP_WEAPONS), 3, 4, 5, and the inaccuracy and
`Targeting_Priority_Set` parts of 2.

1. **Global multipliers.** `GameConstants.xml` scales every top speed by game mode
   (`Object_Max_Speed_Multiplier_Land` 1.56, `_Space` 1.2, `_Galactic` 5.0) and max health
   (`Object_Max_Health_Multiplier_Space` 1.5, `_Land` 1.0, the latter confirmed by a developer
   comment). The engine applies neither, so land units may be relatively slow and space objects
   have two thirds of their health. Shield regeneration may be per `ShieldRechargeIntervalInSecs`
   (3 s) rather than per second (data, unverified).
2. **Weapons.** Units with two `Projectile_Types` (Bossk, the Disruptor merc, MAL) carry their
   fire rates on the projectiles; the engine fires only the first and times it from the shooter.
   `Projectile_Damage` on 15 units and 10 hardpoints differs from the fired projectile's value (a
   per-shooter override, unverified). `Fire_Inaccuracy_Distance` and `Targeting_Fire_Inaccuracy`
   (keyed by target category) and `Targeting_Priority_Set` are not read: shots never miss and
   target choice ignores priorities. `Projectile_Max_Flight_Distance` 0 means "detonate at once"
   in the data (19 ability and death projectiles) but "unlimited" in the engine.
3. **Hardpoint destruction sounds.** All stock `Death_Explosion_SFXEvent` values sit on
   `HardPoint` definitions (267 uses), but the engine reads the tag from the dying unit's type, so
   it never plays; hardpoint destruction needs a snapshot event.
4. **Acceleration spelling.** The 40 infantry team containers spell `Override_Acceleration` and
   `Override_Deceleration`; the engine reads only `OverrideAcceleration`. Also
   `Formation_Priority` (189) beside `FormationPriority` (8) and the misspelt `Formation_Prority`.
5. **Companies and squadrons.** A company forms teams of at most `Max_Squad_Size` members of its
   `Create_Team_Type` container; the engine skips the team step. Three squadrons have no
   `Squadron_Offsets` of their own (Vader's takes them from its team container), so the engine
   stacks their members.
6. **Damage states.** `Land_Damage_Thresholds` and `Land_Damage_Alternates` (219 objects) drive
   the health-based `_ALT` model states; `Specific_Death_Anim_Type` picks directional and special
   death clips. Neither is read.
7. **Determinism.** `Is_Decoration` (1,063 objects) marks objects that the original keeps
   unsynchronized and idle-only (developer comment), so they can stay out of the simulation and
   its hash.
8. **Battle rules.** `Victory_Relevant` (509), `HQ_Win_Condition_Relevant`, capture, cash, and
   reinforcement tags define battle ends and points; none is read yet (M4).
9. **Abilities.** Activated abilities pair a `Unit_Ability` button with an `<Abilities>` element
   through `GUI_Activated_Ability_Name`; element order matters (a developer comment); four FoC
   objects have two `<Abilities>` blocks, which a last-occurrence lookup would cut to one.
10. **Ambient gating.** `SFXEvent_Ambient_Moving` needs the `AMBIENT_SFX` behavior per a developer
    comment; the engine plays it without checking.
11. **Undefined names.** `Damage_Sation_Turbolaser` (8 hardpoints) is not a declared damage type;
    seven sound references do not resolve (audio section). Diagnostics should name them rather
    than fall back silently.

## Regenerating the numbers

The counts come from one-off scripts over the three corpora. Since mods are the compatibility
baseline, the survey belongs in `EAWAssetInspector` as a tag-usage audit over a mounted profile
(per-tag object counts, value classes, repeats, spellings, variants, and unresolved names), so
the same tables can be produced for any mod stack. That command does not exist yet.
