# monster-stairs

Walkers climb and descend stairs narrower than themselves, when a map, a mod or the server asks for it.

## Why they could not

`P_CheckPosition` keeps the highest floor under a walker's box (`tm.floorz`) and the lowest (`tm.dropoffz`), and
`P_TryMove` refuses a walker without `DROPOFF` or `FLOAT` whose box spans more than `MaxDropOffHeight` (24)
between them. That test is Doom's and UZDoom keeps it. On stairs the box covers several steps, so a box `2r`
wide over steps `h` high and `d` deep spans up to `ceil(2r / d) * h`. Any step over 12 high then has to be at
least as deep as the walker is wide, or the walker turns back at it, going up or down.

## What changes

With the setting on, the box is judged by the largest drop across any **one** line it crosses
(`tm.maxlinedrop`, from `PIT_CheckLine`). Each line of a staircase drops only its own step, so the stairs pass,
while a ledge drops past 24 on one line and still blocks. It never blocks more than the old rule
(`ComputeDropoffBlocked` takes the smaller of the two), so a 3D-midtex bridge and a walker standing on a thing
(`MF2_ONMOBJ`, or a thing it steps onto) keep the old rule, since no line measures those.

## Walkers placed on stairs

A spawn, teleport, `summon` or ACS `SetActorPosition` reads only the floor under an actor's centre
(`FFCF_ONLYSPAWNPOS`, or `SetOrigin`), so a walker wider than the steps sits below the ones its box covers, and every
move is then too big a step up (`P_TryMove`'s `MaxStepHeight` check): it can turn and attack but never walk again.
With the setting on, a walk that fails calls `LiftOntoStairs`, which stands the walker on the highest floor under its
box when every line there rises no more than `MaxStepHeight` (`ComputeStairLift`), the room above allows it and no
thing is in the way. It runs only after a failed move, so a walk that succeeds pays nothing for it.

## Choosing it

The first of these that says something decides (`ComputeMonsterStairsEnabled`):

1. The map's MAPINFO: `fua_monsterstairs` or `fua_nomonsterstairs`. A `defaultmap` block gives it to every map
   after it.
2. MAPINFO's `gameinfo` block: `fua_monsterstairs = true|false`, for every map of the mod that does not choose.
3. The server cvar `sv_fua_monsterstairs` (off by default), for maps and mods that say nothing.

It changes how monsters move, so old demos of a map it is on for will not play back the same.

## Netcode

Clients walk monsters between the server's updates (`A_Chase`'s client branch calls `P_Move`), so they must
judge stairs as the server does. MAPINFO and GAMEINFO come from the same wads on both sides; the cvar is a
`features/net-session` `SyncedCVar`, sent when it changes and to each joiner through the existing
`SERVERCOMMANDS_SetCVar`, so no wire format changes.

## Files

- `computation/monsterstairs_compute.{h,cpp}` (`ComputeSettingFromBool`, `ComputeMonsterStairsEnabled`,
  `ComputeLineFloorDrop`, `ComputeDropoffBlocked`, `ComputeStairLift`) with its `_test.cpp`.
- `monsterstairs.{h,cpp}`: the cvar, `Active()` reading the three layers, `LiftOntoStairs()`, `ResetGameInfo()`.

## In-engine hooks

- `p_local.h`: `FCheckPosition::maxlinedrop`.
- `p_map.cpp`: `P_CheckPosition` zeroes it, `PIT_CheckLine` raises it per line, and `P_TryMove`'s dropoff test
  goes through `ComputeDropoffBlocked`.
- `g_level.h` / `g_mapinfo.cpp`: `level_info_t::fua_monsterStairs`, reset in `Reset()`, set by the two map
  options; `G_ParseMapInfo` calls `ResetGameInfo()` before parsing.
- `gi.h` / `gi.cpp`: `gameinfo_t::fua_monsterStairs` and its `fua_monsterstairs` key.
- `p_enemy.cpp` `P_Move`: `LiftOntoStairs` after a walk that failed.
- `sv_main.cpp`: `SendSyncedCVarsTo` for a joiner (`features/net-session`).

Scenarios `monsterstairs` (thin stairs up and down, a real ledge, a map that opts out, stock enemies, and stock enemies
teleported onto the flight, under the cvar) and
`monsterstairsgameinfo` (a mod's GAMEINFO turning it on with the cvar off).
`netmonsterstairs` hosts the stock enemies with a player and an observer on loopback, at 100 ms and with 10% loss:
every client gets the cvar, no client holds an enemy below its steps, and every corpse, a wave killed on the steps
too, lies where the server holds it for every client and two late joiners, with a flat MAP07 as the control.
