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

## Files

- `computation/decoratemath_compute` — the math table and builtins, `GetDistance`, `GetAngle`, `GetZAt`'s point and its
  3D floor search.
- `computation/uservar_compute` — a user variable's layout, lookups, and its int, double and 16.16 conversions.
- `computation/decorateparams_compute` — `A_Explode`'s flags and damage type, `A_JumpIf*Closer`, `A_SetScale`,
  `A_JumpIfHigherOrLower`, `A_CheckBlock`.
- `computation/decorateprops_compute` — the group values, respawn tics and self damage.
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

## Tests

- The four `computation/*_compute_test.cpp`, and `features/acs-parity`'s counting test.
- `fuactl scenarios decorateuzdoom` (`tools/fuactl/scenarios/decorateuzdoom.mjs`): one check actor per feature spawns a
  pass or fail marker, judged offline, then on a server and on a player and an observer; translation and scale are read
  off the actors' rows. A build without this feature crashes loading it.
