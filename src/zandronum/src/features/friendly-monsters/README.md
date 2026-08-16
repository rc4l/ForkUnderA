# friendly-monsters

`sv_fua_friendlymonsters` — while set, nothing in the level fights the player.

## Why

Driving the engine to *look* at something (lighting, a texture, a sky) means warping somewhere and
standing still, usually somewhere the level would rather you did not. Every monster in earshot then
spends the visit shooting at the camera: muzzle flashes and projectiles land in frame, blood
recolours the floor, and eventually the player dies and the view is somewhere else entirely. Any
measurement taken from those frames is measuring the fight.

`notarget` looks like the answer and is not enough on its own:

- `CF_NOTARGET` is only consulted where a monster **acquires** a target (`P_LookForPlayers`, and the
  noise-alert path, both in `p_enemy.cpp`). Anything that locked on *before* the cheat went up keeps
  its target pointer, keeps chasing and keeps firing.
- It is a **toggle**, so a tool that runs twice against one instance turns it back off. That
  happened, and it is invisible: the second run measures an unprotected player.

`MF_FRIENDLY` is the flag the engine already uses for monsters on the player's side, so it is
honoured everywhere targeting decisions are made rather than at the one site a cheat patches. A cvar
is a **state**, not a toggle: setting it twice is setting it.

## How

`CUSTOM_CVAR sv_fua_friendlymonsters` applies to the whole level when it changes, and
`AActor::PostBeginPlay` applies it to monsters that arrive later — teleport ambushes, spawners,
`summon` — so the level does not stay peaceful only until the first closet opens.

Setting it also clears the target of anything already hunting a player and wipes sector
`SoundTarget`s, since the flag governs what a monster targets *next*, not what it is mid-swing on.

Clearing the cvar restores hostility, but only to monsters this feature made friendly — tracked with
`STFL_FUA_WASHOSTILE`. Without that mark, turning it off would also strip `MF_FRIENDLY` from allies a
mapper deliberately shipped (Strife, MBF-style friendly monsters).

`CVAR_SERVERINFO` because it changes what the simulation does, so a client must not be able to
disagree with the server about it. Not archived: it would be an unpleasant surprise to find it still
on in a real game days later.

## In-place engine edits

| File | Edit |
|---|---|
| `src/actor.h` | Added `STFL_FUA_WASHOSTILE = 0x40000000` to the `STFlags` enum. |
| `src/p_mobj.cpp` | Include, and one `zx::FriendlyMonsters_Spawned( this )` call at the end of `AActor::PostBeginPlay`. |
| `src/CMakeLists.txt` | `features/friendly-monsters/zx_friendlymonsters.cpp`, before `zzautozend.cpp`. |

## Driving it

`fuactl warp` sets it as part of putting the game into a measurable state, alongside `god` and
`fly`; `--no-notarget` opts out. See `tools/fuactl/src/undisturbed.mjs`.
