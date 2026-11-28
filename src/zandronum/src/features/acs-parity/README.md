# features/acs-parity

UZDoom's ACS functions and actor properties at UZDoom's numbers, answering as UZDoom's do, and ZDaemon's
`SetTeamScore`, which our function list declared without code. Modder
reference: [`docs/modding/acs/UZDoom_functions.md`](../../../../../docs/modding/acs/UZDoom_functions.md).

| Number | Function | Upstream |
|---|---|---|
| 93 | `GetMaxInventory` | uzdoom@c51abb0161 |
| 94 | `SetSectorDamage`, its amount and damage type only | uzdoom@d0c372692b |
| 95 | `SetSectorTerrain` | uzdoom@7f454358b9 |
| 98 | `CheckProximity` | uzdoom@6633e41cca |
| 99 | `CheckActorState` | uzdoom@df4a8e38c5 |
| 200 | `CheckClass` | uzdoom@3299a29c44 |
| 201 | `DamageActor` | uzdoom@d7b5bdc0f7 |
| 202 | `SetActorFlag` | UZDoom's `ACSF_SetActorFlag` and `ModActorFlag` |
| 203 | `SetTranslation` | UZDoom's `ACSF_SetTranslation` and `AActor::SetTranslation` |
| 205 | `GetActorFloorTerrain` | uzdoom@85488f2f30 |
| 206 | `StrArg` | uzdoom@f82ab889b2 |
| 207-209 | `Floor`, `Round`, `Ceil` | uzdoom@1ff4016498 |
| 215, 216 | `GetNetID`, `SetActivatorByNetID` | uzdoom@f5ceaafbbc |
| 300, 301 | `GetLineX`, `GetLineY` | uzdoom@3a569f176d |
| APROP 44, 45 | `MaxStepHeight`, `MaxDropOffHeight` | uzdoom@1d286d5bd5 |
| APROP 46 | `DamageType` | uzdoom@d09ec5a930 |
| APROP 47 | `SoundClass` | UZDoom's `player_t::SoundClass` |
| APROP 48 | `FriendlySeeBlocks` | UZDoom's `friendlyseeblocks` |
| APROP 49 | `WaterDepth` | UZDoom's `UpdateWaterDepth` |
| 19621 | `SetTeamScore` (ZDaemon) | ours, on `ChangeTeamScore` |
| PLAYERINFO 10 | `FVIEWBOB`, the `fviewbob` userinfo | UZDoom's `fviewbob` |

- None of the numbers clash: Zandronum's own functions run 100 to 191, and 204 is already UZDoom's
  `GetActorFloorTexture`.
- `Ceil` keeps a whole number whole, where UZDoom's `(x & ~0xffff) + 0x10000` adds one.
- `DamageActor` on a tid no actor holds gives -1. UZDoom's `P_DamageMobj` reads the null target's class
  and crashes, and ours read its flags in `isFakePain` before its own null check.
- In a CLIENTSIDE script on a client, `DamageActor` hurts only the client's own actors and gives -1 for a
  synced one, whose health the server holds; a client's `P_DamageMobj` would take the health off locally
  and keep it (`ComputeClientMayDamage`).
- Net ids are Zandronum's `NetID`, the same on a server and its clients; 0 names no actor, as UZDoom's
  world id does.
- A step or drop-off height a script changes goes to clients through `SetThingProperty`, and to a
  joining client when it differs from the default, since clients move their own pawn and walking
  monsters through `P_TryMove`. A damage type stays on the server.
- `SetTranslation` names a translation as `features/trnslate` does, `""` giving the class's back, as UZDoom's
  `AActor::SetTranslation` does; it goes through `trnslate::SetActorTranslation`, which the server sends and a
  clientside script keeps on its own machine. A tid acts on every actor with it, 0 on the activator.
- `APROP_FriendlySeeBlocks` is an actor's own value over its class's: kept in `AActor::fua_seeBlocks` with its
  top bit flipped (`ComputeSeeBlocksStore`), so 0 means none and every value but `INT32_MIN` reads back, and
  saved from SAVEVER 4544. `decorateuzdoom::FriendlySeeBlocks` reads it through `FriendlySeeBlocks`, and the
  look's search takes at most `kMaxFriendlySeeBlocks` (128) blocks (`ComputeSeeBlocksSearch`).
- `APROP_WaterDepth` is worked out when a script asks (`ComputeWaterDepth`), from UpdateWaterLevel's own tests
  without the splash ones, so no actor stores a depth each move.
- `SetActorFlag` changes a flag through `features/actor-flags`, which `A_ChangeFlag` shares: a tid acts on every
  actor with it and reports no miss, 0 on the activator and reports one; it answers how many have the flag. A
  clientside script changes its client's copy and not the map's totals.
- `fviewbob` is a synced userinfo cvar (`NAME_FViewBob` appended to `namedef.h`, whose indices go over the wire):
  off, `P_ZMovement` adds no fly bob and `P_CalcHeight` no flying view bob, as UZDoom's do. The server and the
  player's prediction both read the player's own userinfo.
- `APROP_SoundClass` keeps a script's sound class in `player_t::SoundClass` (None for none), which
  `APlayerPawn::GetSoundClass` answers first, a corpse with its player's, before the skin's and class's; it survives
  respawn and is saved from SAVEVER 4545. A server sends it (`SVC2_SETPLAYERSOUNDCLASS`, through
  `netreplication::SendPlayerSoundClass`) and resends it to a joiner; a clientside script changes its own copy.
  Reading it of an actor no player holds gives `"player"`.
- `SetSectorTerrain` gives a plane its own terrain in `sector_t::terrainnum` (-1 for its flat's), which
  `sector_t::GetTerrain` answers first (`ComputeSectorTerrain`); friction, footclip, splashes, floor damage, decals
  and `P_GetThingFloorType` all ask it, the boundary rule `terrain-through-the-sector` counting the readers left that
  go by flat. An actor on a 3D floor of another flat takes that flat's (`ComputeThingFloorTerrain`). UDMF's
  `floorterrain` and `ceilingterrain` set it at load (names appended to `namedef.h`), a map reset restores it from
  `SavedTerrainnum`, and it is saved by name from SAVEVER 4546. A server sends it as the net-replication family
  `kSectorTerrain` (`SVC2_SETSECTORTERRAIN`), to a joiner too; a clientside script changes its own map.
- `SetTeamScore` sets the score `GetTeamScore` reads (`ComputeTeamScoreKind`: frags, wins or points by the game
  mode) through `ChangeTeamScore`'s code, `SetTeamScoreOf`, announced.

## Hooks

- `p_acs.cpp`: the numbers in `EACSFunctions`, their cases in `CallFunction`, `CheckInventory` through
  `InventoryAmount`, and the three properties in `DoSetActorProperty`, `GetActorProperty` and
  `CheckActorProperty`.
- `p_acs.h`: `APROP_MaxStepHeight`, `APROP_MaxDropOffHeight`, `APROP_DamageType`, `APROP_FriendlySeeBlocks`,
  `APROP_WaterDepth`.
- `p_acs.cpp`: `GetTeamScore`, `TeamScoreNow` and `SetTeamScoreOf`, which `ChangeTeamScore` and `SetTeamScore` share.
- `actor.h`, `p_mobj.cpp`: `AActor::fua_seeBlocks` and its save; `version.h`: SAVEVER 4544.
- `p_enemy.cpp`: `P_LookForEnemies`' search capped by `ComputeSeeBlocksSearch`.
- `d_netinfo.cpp`, `d_player.h`, `namedef.h`: the `fviewbob` cvar, `userinfo_t::GetFViewBob` and its name;
  `p_mobj.cpp` `P_ZMovement` and `p_user.cpp` `P_CalcHeight`: its two gates; `p_acs.h`: `PLAYERINFO_FVIEWBOB`.
- `features/actor-flags`: `ChangeActorFlag`, for `SetActorFlag`.
- `d_player.h`, `p_user.cpp`, `g_game.cpp`: `player_t::SoundClass`, its copy, save and respawn, and `GetSoundClass`;
  `protocolspec/spec.misc.txt`, `network_enums.h`, `sv_commands.cpp`, `cl_main.cpp`, `sv_main.cpp`: its command and
  the joiner's copy; `features/net-replication/playerstate`: `SendPlayerSoundClass`; `version.h`: SAVEVER 4545.
- `features/decorate-uzdoom/classprops.h`: `FriendlySeeBlocks` reads the actor's own value.
- `features/trnslate`: `SetActorTranslation`, shared with `A_SetTranslation`.
- `sv_commands.cpp`, `cl_main.cpp`, `sv_main.cpp`: the two heights in `SetThingProperty` and in
  `SERVER_UpdateActorProperties`.
- `p_interaction.cpp`: `isFakePain` answers no for a null target.
- `features/acs-argcount`: each function's minimum argument count, UZDoom's `MIN_ARG_COUNT`.
- `r_defs.h`, `p_sectors.cpp`: `terrainnum`, `SavedTerrainnum` and `GetTerrain`; `p_terrain.{h,cpp}`: `P_FindTerrain`;
  `p_setup.cpp`, `p_udmf.cpp`, `p_buildmap.cpp`: its load; `p_map.cpp`, `p_mobj.cpp`, `features/decals`: its readers;
  `p_spec.cpp`, `g_game.cpp`: its baseline and reset; `p_saveg.cpp`, `version.h`: its save, SAVEVER 4546;
  `features/net-replication/mapstate`, `protocolspec/spec.misc.txt`, `network_enums.h`, `sv_commands.cpp`,
  `cl_main.cpp`: `kSectorTerrain` and its command.

## Tests

- `computation/acsparity_compute_test.cpp`: the rounding, `GetLineX`/`GetLineY`, net ids, the
  `CheckProximity` scan and its flags, `GetMaxInventory`, the stored `FriendlySeeBlocks`, the water depth and the
  team score's kind, each against UZDoom's formula.
- `fuactl scenarios acsparity`: every function and property in one script (a barrel 48 deep in a 3D-floor pool,
  three barrels' translations read back from `world.actors`), and `DamageActor` on a missing tid; it fails on a build without this feature, and crashes on one without the
  `isFakePain` check.
- `fuactl scenarios netacsparity`: `acsparity`'s checks run by the player on the server and as CLIENTSIDE
  copies on the player's client, an observer and a late joiner, each machine against its own answers; it
  fails on a build whose client `DamageActor` hurts a synced barrel. `SetSectorDamage` runs the same way:
  a client's own call hurts no one, the server's hurts the player, and every client then holds the health
  the server left (`fua_debug_netdrop=12`, `SVC_DAMAGEPLAYER` unsent, fails them). Every machine's
  barrels end on the translations the server set, a server-only script's barrel included, and every client
  reads the sound class and the mud floor's terrain server-only scripts gave
  (`fua_debug_netdrop=64`, `SVC_SETTHINGTRANSLATION` unsent, and `232/94`, `SVC2_SETSECTORTERRAIN` unsent, fail
  the clients and the late joiner).
- `soundclass_wire_nettest.cpp`: `SVC2_SETPLAYERSOUNDCLASS`'s bytes, both name forms and a cut packet;
  `sectorterrain_wire_nettest.cpp`: `SVC2_SETSECTORTERRAIN`'s.
- `fuactl scenarios acsviewbob`: a hovering player's height and view bob with `fviewbob 1` and stay still with
  0; `netacsviewbob`: the server moves the pawn by the client's own setting and the client predicts the same. Both
  fail on a build without the gates.
- `fuactl scenarios acsteamscore`: `SetTeamScore` in team deathmatch, CTF and team LMS, read back by
  `GetTeamScore`; `netacsteamscore`: the server's scores read back on every client and a late joiner
  (`fua_debug_netdrop=108`, `SVC_SETTEAMSCORE` unsent, fails them).
- `fuactl scenarios netacsstepheight`: the server, its player's client and a late joiner hold the
  heights a server script set; `--side lan --cvar fua_debug_netdrop=65` leaves `SetThingProperty`
  unsent and fails it.
- Against UZDoom itself, the scenario's pk3 logged the same 72 values but for `Ceil(2.0)`, the
  default terrain's spelling (`SOLID`) and the three `SetSectorTerrain` reads, where UZDoom's standing barrel keeps the
  terrain it last moved onto (`AActor::floorterrain`); ours asks the sector each time.
