# features/decorate-uzdoom

UZDoom's DECORATE features ours lacked that need no new netcode: the expression math builtins and actor queries,
the fields UZDoom's DECORATE reads, float user variables, the parameters UZDoom added to shared functions, new
jump and setter functions, and class properties. Ported from UZDoom `62afe5f0a9` (`uzdoom@62afe5f0a9`), which
compiles DECORATE onto its ZScript VM; each is rewritten in C++ from its ZScript or native source.

## What it adds

| Area | Added |
|---|---|
| Expression math | `Min`, `Max` (int when every argument is), `Clamp`, `Floor`, `Ceil`, `Round`, `Exp`, `Log`, `Log10`, `Tan`, `ASin`, `ACos`, `ATan`, `ATan2`, `VectorAngle`, `SinH`, `CosH`, `TanH`, angles in degrees |
| Actor queries | `CountInv`, `GetCVar`, `GetDistance`, `GetAngle`, `GetZAt`, `GetSpawnHealth`, `GetGibHealth`, `GetMissileDamage`, `GetCrouchFactor`, `CountProximity`, `GetPlayerInput`, `OverlayX`, `OverlayY` |
| Readable fields | `threshold`, `FloatSpeed`, `tics`, `gravity`, `floorclip`, `bouncefactor`, `wallbouncefactor`, `bouncecount`, `DamageFactor`, `DamageMultiply`, `MaxStepHeight`, `MaxDropOffHeight`, `projectilepassheight`, `PainThreshold`, `Friction`, `meleethreshold`, `maxtargetrange`, `DesignatedTeam`, `lastpush`, `lastbump`, `weaponspecial`, `StartHealth`, `PoisonDamage`, `PoisonDuration`, `PoisonPeriod`, `skillrespawncount` |
| User variables | `var float`, float arrays, `A_SetUserVarFloat`, `A_SetUserArrayFloat`; ACS reads and writes a float one as 16.16 |
| Parameters | `src`, `inflict` on `A_Damage*` and `A_Kill*`, `DMSS_INFLICTORDMGTYPE`; `damagetype`, `nailrange`, `XF_EXPLICITDAMAGETYPE`, `XF_NOSPLASH` on `A_Explode`; `noz` on `A_JumpIf*Closer`; `usezero` on `A_SetScale`; `SXF_ISTARGET`, `SXF_ISMASTER`, `SXF_ISTRACER`; `local` on `A_Log`, `A_LogInt` |
| Functions | `A_CheckSpecies`, `A_CheckProximity`, `A_CheckBlock`, `A_JumpIfHigherOrLower`, `A_SetSpecies`, `A_SetFloatSpeed`, `A_SetPainThreshold`, `A_LogFloat`, `A_fua_ResetTranslation` (ours) |
| Properties | `Threshold`, `DefThreshold`, `InfightingGroup`, `ProjectileGroup`, `SplashGroup`, `MissileChanceMult`, `SelfDamageFactor`, `StealthAlpha`, `FriendlySeeBlocks`, `MinRespawnTics`, `RespawnDice` |
| Constants | `GAF_`, `GZF_`, `INPUT_`, `MODINPUT_`, `CPXF_`, `CBF_`, `MISSILERANGE`, `PLAYERMISSILERANGE` |

- A field a mod's global constant shares a name with now resolves to the field, as in UZDoom; none of the user's mods
  define one.
- The class properties are class metadata, read where they apply (`classprops.h`), so the actor grows no field and saves
  keep their format. The MBF21 groups go into the fields our DEHACKED fills: `0` is no group as in UZDoom, a positive
  group is the one DEHACKED stores for that number, and `ProjectileGroup -1` is groupless.
- A float user variable's doubles sit on an 8-byte boundary; saves write them as doubles, which only a float variable of
  that name reads back. A map's UDMF `user_` keys still set int variables only.
- Jumps that read pointers or other actors run on the server (or for a client's own actors) and send the new state, as
  `A_JumpIfCloser` does.
- `A_SetTranslation("None")` (and `""`, which DECORATE hands over as `None`, as UZDoom's DECORATE does) clears the
  translation; `A_fua_ResetTranslation()` restores the class's, which UZDoom's DECORATE cannot ask for.
- An unknown function in a DECORATE expression is a script error. It crashed while resolving.

Not here yet, since each needs more than a flag or a parameter: `A_SetChaseThreshold` (a per-actor `DefThreshold`),
`FloatBobStrength`/`FloatBobFactor` (the render copy's bob), `A_Explode`'s thrust, ally and circular `XF_` flags (in
`P_RadiusAttack`), `A_Chase`'s new `CHF_` flags, `A_RadiusThrust`/`A_RadiusGive`'s new parameters, the inventory
`ISARMOR`/`ISHEALTH`/`ISKEYITEM` flags, `GetCVarString` (DECORATE has no string expressions) and `GetSpriteAngle`/
`GetSpriteRotation` (no sprite angle here). `A_CheckBlock`'s `CBF_DROPOFF` still tests lines and actors, since our
`P_CheckMove` takes no `PCM_` flags. `GetZAt` does not pass through portals.

## Flag words

`flags7`, `flags8` and `flags9` hold UZDoom's bits exactly, so its flags port verbatim. Our own flags (the ripper set)
moved to `fua_flags`, with its own `FLAGSET_FUAFLAGS`. A save before 4538 has its `flags7` and `flags8` moved to the
new bits on load, by `ComputeRehomedSaveFlags`; its old `flags9` slot now loads as `fua_flags`. `MF8_FULLVOLDEATH` is
gone: MBF21's `FULLVOLSOUNDS` sets `MF3_FULLVOLDEATH`, as UZDoom does, and `A_Scream` now reads it, so `+FULLVOLDEATH`
makes a monster's death scream full volume as well as a missile's death.

## Render and bounce flagsets

`FLAGSET_RENDERFLAGS` and `FLAGSET_BOUNCEFLAGS` (appended) carry `A_ChangeFlag` on `renderflags` and `BounceFlags`
to clients; a client never changes them itself, so before this a change stayed on the server. A joiner gets either
word when it differs from the class's, a player body's `renderflags` excepted, and a client keeps its own
`RF_INVISIBLE` on a player body (`ComputeClientAppliedFlags`). `BounceFlags` widened to 32 bits for UZDoom's bounce
flags (`DONTBOUNCEONSHOOTABLES`, `BOUNCEONUNRIPPABLES`, `DONTBOUNCEONSKY`, `KEEPBOUNCEANGLE`, `BOUNCEMODIFIESPITCH`),
read as 16 bits from a save before 4538.

`renderflags` holds UZDoom's `RF_` bits exactly and `renderflags2` (UZDoom's `RF2_` word) is new, with its own
`FLAGSET_RENDERFLAGS2`; no bit of it is a client's own, so a joiner gets a player body's too. `LIGHTMULTALPHA` moved
to `RF2_LIGHTMULTALPHA`, since its old bit is UZDoom's `DONTFLIP`, and a save before 4539 has it moved on load by
`ComputeRehomedSaveRenderFlags`. `XFLIP`, `YFLIP` and `SPRITEFLIP` are drawn as UZDoom draws them, on GL and the
backend alike: one actor frame pick (`gl_GetActorSpriteFrame`) serves GL, the derived sprites and the tic pieces, and
`ComputeSpriteVFlip` swaps the texture's top and bottom. `DONTFLIP` is a name only, as in UZDoom, whose hardware
renderer never reads it. The render copy takes `renderflags2`, which holds `fillcolor`'s old place on the render line (features/actor-layout). A mirror
leaves out `INVISIBLEINMIRRORS` and the view outside one leaves out `ONLYVISIBLEINMIRRORS` (`ComputeMirrorHidesSprite`),
a mirror being GL's portal or the backend's mirror walk; the backend's reflective floors and ceilings redraw the frame's
own sprites, so they hold neither flag yet. `NODYNAMICLIGHTING` leaves an actor's sprite, model and a player's weapon
to the sector's light. The other `RF_`/`RF2_` names are declared but not yet settable: each comes with its renderer work.

## Gameplay flags

UZDoom's `LAXTELEFRAGDMG`, `FORCEZERORADIUSDMG`, `HITOWNER`, `STOPRAILS`, `NOSHIELDREFLECT`, `ICESHATTER`,
`SMASHABLE`, `NOINFIGHTSPECIES`, `FORCEINFIGHTING`, `FRIGHTENING`, `SEEFRIENDLYMONSTERS`, `DONTFOLLOWPLAYERS`,
`MINVISIBLE`, `MVISBLOCKED`, `DONTFACETALKER`, `RETARGETAFTERSLAM` (with a class's own `Slam` state),
`ONLYSLAMSOLID`, `NOAUTOOFFSKULLFLY` and `GETOWNER`, each read where UZDoom reads it. All of them act on the
server's world: what they change reaches clients as any damage, state or movement does.

Left for later: `NOSECTORDAMAGE` and `FORCESECTORDAMAGE` need monsters to take sector damage (and UDMF's
`hurtmonsters`); `AVOIDHAZARDS` and `STAYONLIFT` need MBF's `P_SmartMove`; `MASTERNOSEE` is a render flag;
Dehacked does not yet give its classes `RETARGETAFTERSLAM` as UZDoom does.

## Files

- `computation/decoratemath_compute` — the math table and builtins, `GetDistance`, `GetAngle`, `GetZAt`'s point and its
  3D floor search.
- `computation/uservar_compute` — a user variable's layout, lookups, and its int, double and 16.16 conversions.
- `computation/decorateparams_compute` — `A_Explode`'s flags and damage type, `A_JumpIf*Closer`, `A_SetScale`,
  `A_JumpIfHigherOrLower`, `A_CheckBlock`.
- `computation/decorateprops_compute` — the group values, respawn tics and self damage.
- `computation/flagrehome_compute` — the bit moves an older save's flag words take.
- `decorateexpr.{h,cpp}` — the expression calls, one class for the math table and one for the query table.
- `uservars.{h,cpp}` — the user-variable lookup every reader and writer shares.
- `classprops.h` — the class properties' values.

## In-engine hooks

- `thingdef/thingdef_function.cpp`: `FxGlobalFunctionCall::StaticCreate` asks `CreateGlobalCall` for a name it lacks.
- `thingdef/thingdef_expression.cpp`: the new `DEFINE_MEMBER_VARIABLE`s; `FxFunctionCall::Resolve` no longer resolves a
  null call; `FxArrayElement` reads float arrays.
- `thingdef/thingdef_parse.cpp`: `ParseUserVariable` takes `float` and lays it out through `ComputeUserVarLayout`.
- `thingdef/thingdef_codeptr.cpp`: the user-variable setters, `A_Damage*` and `A_Kill*` on one helper each, `A_Explode`,
  `DoJumpIfCloser`, `A_SetScale`, `InitSpawnedItem`'s `SXF_IS*`, `A_Log`, `A_LogInt`, `A_LogFloat`, the new jumps and
  setters, `A_SetTranslation` and `A_fua_ResetTranslation`.
- `thingdef/thingdef_properties.cpp`: the new properties.
- `actor.h`: the `AMETA_` keys of the class properties.
- `p_acs.cpp`, `p_acs.h`: `P_PlayerInputValue` and `P_FindScriptCVar`, shared with ACS's `GetPlayerInput` and
  `GetCVar`; ACS `SetUserVariable` and `GetUserVariable` through `uservars`.
- `dobject.cpp`: `ResetUserVars` and `SerializeUserVars` by element type.
- `p_enemy.cpp` (`SuggestMissileAttack`, `P_LookForEnemies`), `p_interaction.cpp` (self damage, `DefThreshold`),
  `p_mobj.cpp` (stealth fade, nightmare respawn), `g_strife/a_rebels.cpp` (`DefThreshold`).
- `features/acs-parity`: the proximity scan's counting mode and `Proximity`, which `A_CheckProximity` and
  `CountProximity` share with ACS.
- `features/net-snapshot/capture.cpp`: `A_SetUserVarFloat` is cosmetic-safe as `A_SetUserVar` is.
- `wadsrc/static/actors/actor.txt`, `constants.txt`: the declarations, fields and constants.
- Flag words: `actor.h` (UZDoom's `MF7_`/`MF8_`/`MF9_`, `FUAF_`, `fua_flags`), `p_mobj.cpp` (`AActor::Serialize`'s remap
  and the asserts tying it to the enums), `version.h` (SAVEVER 4538), `network.h` (`FLAGSET_FUAFLAGS`), `cl_main.cpp`,
  `sv_commands.cpp`, `thingdef/thingdef_codeptr.cpp` (`A_ChangeFlag`), `thingdef/thingdef_data.cpp`,
  `thingdef/thingdef_properties.cpp` (`ClearFlags`), `p_map.cpp` (the ripper), `d_dehacked.cpp` and `p_enemy.cpp`
  (`FULLVOLDEATH`).
- Render and bounce flagsets: `network.h`, `sv_commands.cpp` (`SetThingFlags`, the joiner's flags), `cl_main.cpp`,
  `thingdef/thingdef_codeptr.cpp` (`A_ChangeFlag`), `actor.h` and `p_mobj.cpp` (32-bit `BounceFlags`), `version.h`
  (4538); `features/net-replication`'s `ComputeClientAppliedFlags`.
- Render words: `actor.h` (UZDoom's `RF_`/`RF2_`, `renderflags2`), `p_mobj.cpp` (`AActor::Serialize`'s `renderflags2` and
  its remap), `version.h` (4539), `network.h` (`FLAGSET_RENDERFLAGS2`), `cl_main.cpp`, `sv_commands.cpp`,
  `thingdef/thingdef_codeptr.cpp`, `thingdef/thingdef_data.cpp` (`LIGHTMULTALPHA`), `features/gamecopy/lights.cpp`.
- Mirror and light flags: `actor.h` (`renderflags2` and `fillcolor` swap places), `features/gamecopy/fields.h` and
  `gamecopy.cpp` (`renderflags2`, and `fillcolor` read only for a stencil style), `features/sprites/spritebuild.cpp` (`ViewRejects`),
  `features/levelmesh/pointlight.cpp` (`ActorLight`), `gl/scene/gl_sprite.cpp`, `gl/scene/gl_spritelight.cpp`, and their rows
  in `thingdef/thingdef_data.cpp`.
- Sprite flips: `features/materials/spriteframe.{h,cpp}` (`gl_GetActorSpriteFrame`), `gl/scene/gl_sprite.cpp`,
  `features/sprites/spritebuild.cpp` (`DeriveSprite`, `TicPieceHolds`), `features/sprites/computation/spriteframe_compute`,
  and their rows in `thingdef/thingdef_data.cpp`.
- Gameplay flags: `p_interaction.cpp` (`P_DamageMobj`, `P_PoisonDamage`, `OkayToSwitchTarget`), `p_map.cpp`
  (`PIT_CheckThing`, `P_TestMobjZ`, `P_RadiusAttack`, `ProcessRailHit`), `p_mobj.cpp` (`P_XYMovement`,
  `P_ZMovement`, `AdjustReflectionAngle`, `Slam`, `CanSeek`), `p_enemy.cpp` (`P_NewChaseDir`, `P_IsVisible`,
  `LookForEnemiesInBlock`, `P_LookForPlayers`, `A_Chase`), `p_sight.cpp`, `p_conversation.cpp`,
  `g_shared/a_fastprojectile.cpp`, and their rows in `thingdef/thingdef_data.cpp`.

## Tests

- The four `computation/*_compute_test.cpp`, and `features/acs-parity`'s counting test.
- `fuactl scenarios decorateuzdoom` (`tools/fuactl/scenarios/decorateuzdoom.mjs`): one check actor per feature spawns a
  pass or fail marker, judged offline, then on a server and on a player and an observer; translation and scale are read
  off the actors' rows. A build without this feature crashes loading it.
- `node scenarios/decorateuzdoom.mjs --uzdoom` runs the checks without ours-only ones in a UZDoom build and judges its
  log, so each expectation is UZDoom's own: 50 of 50 pass there.
- `fuactl scenarios decorateflags` (`tools/fuactl/scenarios/decorateflags.mjs`): the moved flags read back at map start,
  from this build's save, from a save an older build wrote (`--old PATH` or `FUACTL_OLD_ENGINE`), and on a server, its
  player, observer and late joiner. It fails with the remap left out, and online with `SetThingFlags` dropped.
- `tools/flag_bits_snapshot.py` (CI): every DECORATE flag's bit, refusing a moved one until SAVEVER is bumped.
- The gameplay flags are decorateuzdoom checks, so `--uzdoom` holds them to UZDoom too (50 of 50 pass there); each
  behaviour check fails with its flag taken out.
- `node uzdoom/flipparity.mjs` (from `tools/fuactl`): a four-colour sprite plain, XFLIP, YFLIP and both, and an
  eight-rotation sprite plain, SPRITEFLIP and SPRITEFLIP with XFLIP, each read by its quarters' colours in UZDoom's
  picture and ours on each API.
- `node uzdoom/mirrorlightparity.mjs`: sprites before a line mirror and in direct view, plain, INVISIBLEINMIRRORS and
  ONLYVISIBLEINMIRRORS, read by colour, and two grey sprites beside a red light, plain and NODYNAMICLIGHTING, read as lit
  or not, each against UZDoom's picture.
