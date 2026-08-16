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

**Friendly is not the same as quiet.** A friendly monster still hunts, it just hunts the *other*
monsters: it wakes, plays its see-sound and walks off to find a fight. Watching a level come alive
around you is not what this is for, so each one is also `Deactivate()`d — the engine's own dormancy,
which sets `MF2_DORMANT` and the `Inactive` state, or `tics = -1` when the class has none. That stops
the state machine outright, so `A_Look` never runs again: no see-sound, no wandering.

Monsters that were **already** friendly are skipped entirely rather than deactivated. They were never
hostile to the player, and rewriting them would mean turning the cvar off makes a level's own allies
hostile. Skipping them is also what keeps one mark bit exact: `STFL_FUA_WASHOSTILE` means "this
feature changed both its side and its dormancy", so clearing the cvar restores both with nothing to
guess.

`CVAR_SERVERINFO` because it changes what the simulation does, so a client must not be able to
disagree with the server about it. Not archived: it would be an unpleasant surprise to find it still
on in a real game days later.

## Savegames

`MF_FRIENDLY`, `MF2_DORMANT` and `STFlags` are **all serialised** by `AActor::Serialize`, so a save
taken while the cvar was on carries every monster back friendly and asleep. `CUSTOM_CVAR` only fires
when the value *changes*, so loading that save with the cvar already off would restore nothing and
hand back a permanently pacified level with no way out of it.

`FriendlyMonsters_Loaded`, called on the loading side of `Serialize`, reconciles what the save
carried against what the cvar currently says. The effect cannot outlive the session that asked for
it. No new field is written, so **the savegame format is unchanged** and older saves load normally.

## Backporting

Kept deliberately small: three call sites and one enum value. `mcp_rpc.cpp` is bridge-only
(`FUA_MCP_BRIDGE`) and carries no risk at all.

- `STFL_FUA_WASHOSTILE` takes `0x40000000` out of Zandronum's `STFlags` word. `0x80000000` is still
  free. If an upstream sync ever lands a flag on that bit, this is the collision to move, and the
  `fua_` name makes it greppable.
- `AActor::Serialize` and `AActor::PostBeginPlay` each gain one guarded call. The `PostBeginPlay` one
  costs a single bool read per actor spawn when the cvar is off.
- Everything is server-side: `ApplyTo` returns early under `NETWORK_InClientMode()`, because this
  rewrites AI state (side, target, dormancy) and a client must not predict it. Single player is not
  client mode, so the dev use this exists for is unaffected.

## In-place engine edits

| File | Edit |
|---|---|
| `src/actor.h` | Added `STFL_FUA_WASHOSTILE = 0x40000000` to the `STFlags` enum. |
| `src/p_mobj.cpp` | Include, one `zx::FriendlyMonsters_Spawned( this )` at the end of `AActor::PostBeginPlay`, and one `zx::FriendlyMonsters_Loaded( this )` on the loading side of `AActor::Serialize`. |
| `src/CMakeLists.txt` | `features/friendly-monsters/zx_friendlymonsters.cpp`, before `zzautozend.cpp`. |

## Driving it

`fuactl warp` sets it as part of putting the game into a measurable state, alongside `god` and
`fly`; `--no-notarget` opts out. See `tools/fuactl/src/undisturbed.mjs`.
