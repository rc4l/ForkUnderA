---
name: moved-sectors
description: How to test that a moved floor, ceiling, door or platform is drawn where the game holds it after a client reconnects or joins late and after a single player reloads the same map, with the netonetime and singlereload scenarios, which judge by state and by pictures and fail first on a build whose level mesh keeps the last load's sector looks. Use for any change to features/levelmesh (wallcache.cpp, flatmesh.cpp, the map bake, InvalidateMovedSectors), features/gamecopy's sector fill and change counters (rcShape, rcPlacement, rcLook), dsectoreffect.cpp MovePlane and the floor, ceiling, door and plat movers, cl_main.cpp's SetSectorFloorPlane/SetSectorCeilingPlane, sv_main.cpp's full update, level setup (PreprocessLevel, AllocForLevel, dgframe.cpp's level bake), save loading, and for a report of a door, lift or floor drawn where it was (closed, raised) while players walk through it.
---

# Testing moved sectors after a reload or a join

Our backend draws the level from a mesh baked once a level loads and re-baked a sector at a time when the render copy
says it changed. A plane that moved before the bake, or that reaches the copy after it (a join's full update, a save),
is drawn right only if that chain holds. Stock Zandronum's GL works its flats out every frame, so a fault here is ours.

The game's state can be right while the picture is wrong: collision reads the game's plane, the screen reads the mesh.
**Judge by pictures as well as state.** `netonetime` first passed on state alone while every reconnecting client drew
the map's start (fixed in `wallcache.cpp`, `KeepBakedLooks`, 2026-11-23).

## The scenarios

```bash
# from tools/fuactl; FUACTL_NO_FOCUS=1, each run on a port of its own
node src/cli.mjs scenarios netonetime singlereload --engine ../../build-win/Release/forkundera.exe
```

- `netonetime` (hosted, `compareNetplay`): a player uses a one-time switch (lowers a floor) and crosses one-time lines
  (open a door, raise a platform). The server's world is held to the change first. Then the same client reconnects
  (console `reconnect`, or `disconnect`, 15 s away with the server empty, and `connect`), and a fresh client joins late.
  Each is judged by state (`world.sectorstate`) and by pictures from one spot and three angles against the player's own
  before it left. Configs: with an observer, at 100 ms, alone, alone with a gap, survival alone (the map is reset at the
  round's FIGHT once everyone left, which is stock survival; the scenario names what server and client hold before and after).
- `singlereload` (single player): the same actions, a save loaded over a restart of the map, and the map restarted with
  the actions finished out of view, each judged by pictures against the first look.

## Keeping the pictures comparable

- Notices off (`show_messages 0`): each capture prints its file name.
- One camera: `warp` to `LOOK_FROM` and turn with `input.look` to `LOOK_ANGLES`. A second client warping to the same
  spot lands on the first one's head and looks from 56 higher; the player steps aside first.
- A joiner draws its teammate's name, health and weapon top left; `worstBlock` judges from row `PICTURE_FROM_Y` down.

## Making it fail first

Keep the build from before your change (`build-win/Release` copied with its `.pk3` and `.dll` files) and run both
scenarios on it. For the reload fault, a build that refills `g_lastLook` only when the sector count changes fails both
(netonetime's pictures 47-72, singlereload's out-of-view restart). `--side lan --cvar fua_debug_netdrop=118,119` (the
floor and ceiling plane commands) fails netonetime's state checks after the reconnect and the late join.
