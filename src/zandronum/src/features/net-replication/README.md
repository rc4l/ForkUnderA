# net-replication

What a server sends its clients about the world: the map state only a server writes, sent from one place
when it changes, and the `SERVERCOMMANDS_*` calls game code still makes for everything else.

## The change set

The families are the ones clients never compute for themselves: a sector's flats, colour, fade, gravity,
friction, reflection, scale, rotation and special; a line's alpha, the flags clients move and draw by
(`features/net-protocol/lineflags.h`) and its six textures; a side's flags. Movers, lights, scrollers and
polyobjects run on clients too and keep their own sends.

- Game code marks the row it changed (`TouchSector`, `TouchLine`, `TouchSide`) where it used to send a
  command, and `SendChanges` at the end of the server's tic sends each touched row's changed families once
  (`ComputeSectorChanges`, `ComputeLineChanges`), whatever touched it and however often.
- A slice of every row is compared each tic too, every row once a second, so a change nobody touched still
  reaches clients and is counted as missed (`net/replication_missed`, `fua_net_replication`, and
  `fua_net_replication_misses 1` prints each). Every netgame scenario fails on a miss, which is how a path that
  forgot to touch is found.
- Where game code sends one command for a whole tag or line id (`Sector_SetColor`, `Sector_SetFade`,
  `Sector_SetRotation`, ACS `SetLineTexture`), it keeps that compact form and notes the family as sent
  (`NoteSentSector`, `NoteSentLine`, `ComputeTakeFamilies`), leaving the row's other families to the change set.
- A joiner gets every family that differs from the level's start (`SendToJoiner`), taken where the old
  `Saved*` baselines are, before any open script runs. That replaced the join path's own comparisons, which
  sent a sector's gravity, reflection and plane angle to every client on each join, and never sent a sector's
  special or a line's monster, projectile or hitscan blocking at all.

## Owns

`sv_commands.cpp`, `sv_commands.h`, `network/packetarchive.cpp` and `network/packetarchive.h`.

## Public header

`mapstate.h` for the map state above, and `sv_commands.h` for the rest. Calls to `sv_commands.h` from game
code are counted by `net-sends-through-replication` (`tools/boundary-rules.json`), which may only shrink.

## Files

- `computation/mapstate_compute.{h,cpp}` (the families, what changed, what a compact send covered) with its
  `_test.cpp`.
- `mapstate.{h,cpp}` (the snapshots, the touched rows, the audit, the joiner, `fua_net_replication`).

## In-engine hooks

- `p_spec.cpp` `P_SpawnSpecials`: `BeginLevel` beside the `Saved*` baselines.
- `sv_main.cpp` `SERVER_Tick`: `SendChanges` after `G_Ticker`; the two full updates call `SendToJoiner` between
  `SERVER_UpdateLines` and `SERVER_UpdateSectors`, which keep only the families not here.
- Touches in `p_acs.cpp`, `p_ceiling.cpp`, `p_doors.cpp`, `p_floor.cpp`, `p_lnspec.cpp`, `p_plats.cpp`,
  `p_sectors.cpp`, `p_spec.cpp`, `p_switch.cpp` and `g_game.cpp`'s map reset, where those sends were.
- `sv_commands.cpp`: `SERVERCOMMANDS_SetLineTextureParts` sends the textures a mask names, and
  `SetSomeLineFlags` sends `kClientLineFlags`, which `cl_main.cpp`'s handler replaces whole.
