# ghostactor: the mirrors

Covers: src/zandronum/src/features/net-snapshot/ghostactor.h, src/zandronum/src/features/net-snapshot/ghostactor.cpp, src/zandronum/src/features/net-snapshot/computation/smooth_compute.h, src/zandronum/src/features/net-snapshot/computation/smooth_compute.cpp, src/zandronum/src/features/net-snapshot/computation/smooth_compute_test.cpp, src/zandronum/src/features/net-snapshot/computation/wall_compute.h, src/zandronum/src/features/net-snapshot/computation/wall_compute.cpp, src/zandronum/src/features/net-snapshot/computation/wall_compute_test.cpp, src/zandronum/src/features/net-snapshot/computation/anim_compute.h, src/zandronum/src/features/net-snapshot/computation/anim_compute.cpp, src/zandronum/src/features/net-snapshot/computation/anim_compute_test.cpp

## Responsibility

The engine side of the ghost table. Each present ghost gets a mirror: a client-side `AActor` of its
class that the renderer, sound and the local player's collision see. Once a frame, this file samples
every ghost (`SampleGhost`), writes the pose into its mirror, runs the mirror's state machine
between records, and destroys the mirrors whose ghosts left. It also hides the client's own copy of
each mirrored actor, makes the client's copies of cosmetic spawns (`SVC2_SNAPCOSMETIC`), moves a
snapshot point sound onto a mirror that appears late, applies psprite records and property sets to
remote entities, and prints the client's `[snap]` report. The ghost table and sampler are
`entities.cpp` ([entities.md](entities.md)).

The design: [Mirrors and the renderer](design.md#mirrors-and-the-renderer), [Mirror
collision](design.md#mirror-collision), [Error smoothing](design.md#error-smoothing), [Client-side
animation](design.md#55-client-side-animation) and [Cosmetic actors](design.md#63-cosmetic-actors).

## The mirror

`ghost_CreateMirror` spawns the class at the ghost's position, parks it in `STAT_INFO` so the
thinker loop never ticks it, unlinks it from the blockmap, and sets `MF_NOBLOCKMAP`, `MF_NOGRAVITY`,
`MF_NOCLIP` and `MF5_NOINTERACTION`. It carries `NETFL_MIRROR`, NetID 0 and `OF_Fixed`, which pins
it against the garbage collector, since the mirror table is not a root; the pin is cleared before a
deliberate `Destroy`. The engine's guards on `NETFL_MIRROR` keep gameplay off it: no damage, no
`P_RemoveThing`, no crushing or pushing by a sector, and its collision only as a blocker. Those
guards are flag tests in the stock files, not calls into this module.

Two players never get a mirror (`ghost_ExcludedPlayer`): the local player, whose prediction ticks
the real pawn, and whoever is being viewed through, whose real actor the camera already hides. A
player who becomes excluded loses his mirror, and his own actor is shown again. Every remote
player's own actor is still moved to its ghost's pose once a gametic (`CLIENT_MoveThing`), since
`players[].mo` is what everything else reads.

## Hiding the legacy copy

Where the client also holds the server's legacy actor for a mirrored id, that actor is hidden rather
than destroyed, since the server can still address it by NetID: `RF_INVISIBLE`,
`STFL_SNAPGHOSTHIDDEN` (its `Tick` returns at once), and `MF_NOSECTOR` and `MF_NOBLOCKMAP` where it
did not already have them, recorded so the reveal clears only those. `HideEnteredLegacy`, called on
the packet path right after `ApplyDelta`, sets the flags for the ids that just entered; the unlink
and relink wait for the frame loop. One relink brackets both bits, since `LinkToWorld` reads the
flags, and `P_DelSector_List` frees the `touching_sectorlist` the unlink saved: a relink that
`MF_NOSECTOR` makes skip the sector lists would otherwise leave the actor in every sector's touching
list, where a later sweep still finds it.

When a ghost leaves, its legacy copy stays hidden, since the entity is gone on the server and
showing it would bring it back; clearing every mirror reveals every hidden actor, whose own state is
authoritative again.

## A frame

`GhostFrame`, from `FillNow` in `features/gamecopy/gamecopy.cpp` -- the fill that takes the copy of
the world the renderer draws -- ahead of its walk, so the copy carries the mirrors this frame makes
rather than the previous frame's:
1. Installs the hooks once (`SetLawGravity`, `SetAnimatesLookup`, `SetPSpriteSink`, `SetPropsSink`)
   and the local player's reserved id every frame.
2. With `fua_csnap_ghosts` off, clears every mirror and returns.
3. Resets every ghost's smoothing when the stream epoch moves or a map reset window opens or closes.
4. For each present ghost: creates its mirror if it has none, and resolves the shooter as the
   mirror's `target` (the shooter's mirror first) until one succeeds.
5. Decides solidity against the local player's pawn, before the cull: a mirror whose class is solid
   becomes a blocker inside `fua_csnap_collide_dist`, stops being one past a quarter further out,
   and never while the wire says corpse.
6. Picks the work tier by distance from the camera, with a 256-unit hysteresis band: near and mid
   every frame, far on alternate tics, when the id's low bit matches the tic's. A far mirror off its
   phase keeps its last pose and frame.
7. Places the mirror. The pose of an extrapolated ghost stops at the first line that blocks its
   class along its path (`ghost_WallStop`, traced once per record); every pose then passes through
   the smoothing, which offsets extrapolated ghosts and the stepped ones `fua_csnap_smooth_entities`
   names (remote players by default); and an extrapolated one's descent is clamped to the floor and
   its rise to the ceiling. A ghost the smoothing leaves alone (held, outside the smoothed
   population) moves only on its first frame of each tic and the engine's tic interpolation slides
   it over the tic from where it stood, as stock slides an actor its tic moved; on every other ghost
   that interpolation is neutralised. The relink is skipped when nothing moved. Since a mirror never
   ticks, the ghost frame ends every mirror's slide at each tic's start, as the engine's tic does
   for an actor that ticks.

   The wall clamp's arithmetic and the smoothing are pure units. `wall_compute` gives the path
   (`ComputeWallPath`), where it first touches each line the trace returns (`ComputeWallContact`, on
   `ComputeSweepLine`), and whether that line's flags or opening stop a body with the class's
   defaults (`ComputeLineFlagsBlock`, `ComputeOpeningBlocks`); `ghost_WallStop` walks the lines and
   `ghost_LineBlocks` reads their flags, the opening and the railing hack. `smooth_compute` is the
   smoothing (`ComputeSmooth`); `ghost_SmoothCfg` fills its settings.
8. Writes velocity (for particle trails only), the appearance (below), render style, effects, the
   misc bits, a player mirror's crouch, the property set the wire last named, and the client's own
   rocket-trail choice.
9. Sweeps the mirrors whose ghost has left, keeping the legacy copy hidden, and moves pending point
   sounds onto mirrors that now exist.
10. Every `fua_snap_stats` seconds, prints the `[snap]` report and clears its counts; with it at 0
    (the default) the report still runs every 5 s to clear them, printing nothing, so one turned on
    later covers its own window.

`GhostFoldApplied` is the fold on the packet clock, called once for each payload applied: the state
it leaves is what the frame's own folds are taken from.

## Appearance and local animation

On a tier below `fua_csnap_localanim`, the mirror runs its own state machine on the tic clock,
advancing by the gametics since its last visit, at most 8, with action functions skipped. A record
resyncs it only when the wire's state index changed or a branch event arrived (`eventSeq`), and then
only if the mirror's frame disagrees or the two states are on chains that do not meet; an event that
agrees on the frame still sets the phase. The state the wire names is preferred over one a frame
search finds, its remaining tics come from the wire, and the record's age in tics is run forward.
Two exceptions hold the mirror: a state whose chain dead-ends in destruction is shown frame-only and
held (`ghost_SetStateSafe`), and a mirror resting in a final state the wire's state leads to is not
pulled back. When the wire has been quiet past 1 s and the mirror disagrees, the wire's frame is
taken once per wire frame.

At or above `fua_csnap_localanim`, the mirror takes the wire's sprite and frame verbatim, and its
state is resynced when it is promoted to an animating tier.

## Mirror spawns

A client-side spawn (SXF_CLIENTSIDE, a +CLIENTSIDEONLY class) is never made by the server, so a
mirror runs the spawner actions of the states it enters, on the per-frame advance and on a record's
jump. `AActor::SetState` asks `MirrorSpawnAction`, which allows a spawner state whose owner runs no
special expression. For a spawner class each mirror keeps a ring of the states it entered
(`ComputeMsPush`, in `anim_compute`), which classifies a record's state change as a seed, a resync,
behind, undecidable or a jump; of these only a jump runs the new state's spawners, owed the record's
age up to 8 tics, with the mirror placed where it was then.

## Cosmetic events

`CosmeticEvents` reads one `SVC2_SNAPCOSMETIC` whole and, on the joined map, makes each event: a
copy of a routed spawn with its position, velocity and extras, flagged `NETFL_CLIENTSIDEONLY` unless
plain, and set to its state with or without the action; or blood and splatter through `P_SpawnBlood`
and the splatter functions, from the originator's real actor or mirror. Each copy is owed its age:
`CosmeticFirstTick`, before its first `Tick`, runs it forward that many tics with `g_bSnapCatchUp`
set, and what it spawns meanwhile is owed the rest (`CosmeticSpawn`). In a catch-up an actor starts
each `CHAN_AUTO` sound once (`CatchUpHoldsSound`, declared in `capture.h`). `CosmeticForget` drops a
destroyed actor's debt.

## Property sets

The property sink takes a `SNAPEV_PROPS` payload and keeps it by NetID, so a set survives the frames
between its arrival and the mirror's creation; the frame then writes scale, roll, the material and
its uniforms onto the mirror. `PropsKept` is how many sets this client holds and `PropsStats` where
they went -- onto a mirror, or held for one that does not exist yet. `GhostClearAll` clears them
with the mirrors, since the next map reissues the ids.

## Point sounds

A snapshot point sound for an id with no mirror yet is kept by NetID and point (`PointSoundPending`)
when it started, at most 1024. After each frame's mirrors exist, `S_AttachPointSound` moves it onto
the mirror; it is dropped when it ends, left at its point when the mirror's channel is busy, and
cleared at a level change. Whether `S_Sound` started the sound is read from `S_SoundStarts`' count
across the call, since a refusal can leave an older copy at the same point and a replacement can
reuse a stopped copy's channel, so neither a search nor a pointer can tell.

## Psprites

`ghost_ApplyPSprite` writes a remote player's weapon and flash layers from the psprite record
directly, without `P_SetPsprite`, so a remote weapon draws and never fires. It never touches the
local player's, resolves the state against the weapon's class, blanks both layers for weapon class
0, and leaves a layer alone when its state cannot be resolved.

## Reading the report

Each `[snap]` line names its interval: a population (the tiers, solid mirrors) is a per-frame mean,
an event a window total.
- The animation census prints each tier's percentage beside its count, since raising
  `fua_csnap_localanim` grows the sample set; a count of 0 means the tier is not animating. Its
  by-state twin compares state identity rather than the drawn frame, which monsters share across
  states, so the gap between the two is the measure.
- The control split puts the ids the server never suppresses (`format.h`) in a column of their own.
  With suppression off both columns are one population and must read equal, which checks the
  instrument; with it on, a gap means suppression's drops are selection, a match that they are
  damage.
- `bystate by gap` buckets the mirror against the wire's state by the server tics since the entity's
  previous appearance record. Read it with suppression off: the long buckets are the entities the
  scheduler starved, which suppression would make typical.
- The loop census (`fua_csnap_loopcensus`) runs in server tics, so delivery delay does not enter it.
  Read it by gap bucket: the aggregate, printed last, is dominated by well-served entities a loop
  hint would leave alone.
- The jump log tells starvation from delivery: a jump after a 60 ms gap is not starvation, one after
  2 s is nothing else, and `recs` above 1 with a large jump is a burst drawn at once. `pos` counts
  the records carrying position, so a stationary entity has `recs` and no `pos`.
- If the frame rate slides while the near and mid counts climb, the render set is the cost; if they
  are flat, it is elsewhere, and the frame phases (sim, render, the ghost frame) say where.
- `fua_csnap_ghostinfo`'s `age` is milliseconds since the newest state arrived: small means any
  staleness is this client's, thousands that the entity is not reaching it, and small while a
  monster looks idle that it is not awake on the server. Its wire and shown frames differ when local
  animation ran ahead or a resync picked the wrong sequence.

## Data structures

Per id, for the whole id space and allocated on first use: the mirror pointer (`g_mirror`) with the
compact list of ids that have one (`g_mirrorIds`), the tier, the last synced state, the hard-resync
frame, the event sequence seen, the animation tic, the smoothing state (`SnapSmooth`), the cached
wall trace (`GhostWallTrace`), the hidden legacy copy and the flags added to it, the property set
held for the id (`g_ghostProps`), and the loop census's shadow (`GhostShadow`). Beside them: the
mirror-spawn rings (`MsRing`, by id, for spawner classes only), the pending point sounds
(`GhostPointSound`), and the actors owed a catch-up.

## Public functions

The frame and the lifecycle:
- `GhostFrame`: a frame, above. `GhostFoldApplied`: the fold on the packet clock.
- `RegisterMirrorEffects`: every mirror carrying effects into the tic's effects registry, since
  a mirror never ticks and so never registers itself.
- `GhostsFromNetwork`: turns `fua_csnap_ghosts` on when snapshot traffic arrives, and off with every
  mirror cleared when it stops.
- `GhostClearAll`: destroys every mirror, reveals every hidden actor and drops the property sets.
  Needs a live level.
- `LevelUnloaded`: forgets every mirror reference without touching an actor, resets the recording,
  every per-id table and the ghost table (`ForgetWorld`). Every level teardown must call it, or a
  mirror pointer outlives its actor.
- `HideEnteredLegacy`: above.

Lookups:
- `MirrorForNetID`: the mirror for an id, as a sound source or a blood originator; `bCount` false
  keeps a per-tic retry out of the lookup line.
- `ResolveThroughMirror`: the client's actor lookup for a command that may address a mirrored
  entity, counted by command name, so a command arriving for a hidden actor is answered with its
  mirror rather than dropped.
- `MirrorPlayerNum`: the player a mirror stands in for, or -1, since a mirror's NetID is 0. The
  game copy's sprite path reads it for a player mirror's skin, a mirror carrying no `player_t`.
- `SpeedForClass`: a class's default speed in units per tic, for the replay's glide census.

Sounds, spawns and respawns:
- `PointSoundPending`, `CountPointSoundOnMirror`.
- `CosmeticEvents`, `CosmeticCountsReset`, `CosmeticSpawn`, `CosmeticFirstTick`, `CosmeticForget`.
- `MirrorSpawnAction`, with `g_bSnapMirrorSpawns` up while a mirror's spawners run.
- `RespawnBehindMirror`: an item respawn for a copy behind a mirror. The record draws the item, so
  the copy stays hidden and only its sound and fog are made, at the ghost; true when it answered.

Property sets: `PropsKept`, `PropsStats`, above.

The report:
- `GhostReportNow`: the report at once with its counts cleared, for the fuzzer.
- `CensusScope` (`ESnapCensusPhase`): times one call into the `[snap] ghostframe` and predict
  lines' window totals, a line at each site. The frame's sim and render come from
  `features/framephase` instead, which already times them, and the tics run from the gametic.

The smoothing (`computation/smooth_compute.h`, also used by `csnap_replay.cpp`):
- `SnapSmooth`: one ghost's smoothing state: the decaying offset, the part retained inside the
  rounding cell, the model's own motion between records, and the current segment's box.
- `SnapSmoothIn`: what one frame of smoothing reads of the sampler's pose, with its kind
  (`SNAP_SMOOTH_BODY`, `SNAP_SMOOTH_BALLISTIC` or `SNAP_SMOOTH_TURNING`, `SNAP_SMOOTH_KINDS` of
  them) and, for the census only, whether its ages were dated by the clock model. The kind is
  behavioural, not only a census label: a ballistic missile has the ballistic bit (gravity missiles
  included), a turning one the steering bit alone, and a body neither, so any `MF_MISSILE` or
  `MF_SKULLFLY` actor is a missile of one kind or the other, a charging Lost Soul included.
  "Turning" is wider than homing: the server sets the steering bit alone on a missile that turns,
  one whose straight run it has not yet proven (a fresh straight shot spends its first tics there),
  and one whose velocity rounds to zero on the wire. It is the definition every measurement behind
  the missile threshold used; only ballistic missiles take that threshold, since the turning cell's
  mark failed (frames drawn more than 15 degrees off course rose at 0 on the recordings run at 90
  frames a second and up). `ComputeSmoothIn` builds it from a pose, for the mirror and the replay
  alike.
- `SnapSmoothCfg`: the thresholds (`fua_csnap_smooth_step`; `fua_csnap_smooth_nonballistic` for an
  extrapolated body or turning missile; `fua_csnap_smooth_ballistic` for an extrapolated ballistic
  missile), whether this ghost is in the smoothed stepped population, a map reset in progress, and
  the tic rate.
- `SnapSmoothStats`: the smoothing census, window totals, stepped and extrapolated apart, with the
  extrapolated census again by kind (ballistic, turning, body) and by what dated the ages (the clock
  model or arrival). Each cell is a `SnapSmoothSplit`, which also counts the snaps that jumped back
  against the wire velocity, the snaps by correction size (under 0.5 u, under 2 u, 2 u and over),
  and moving frames, snapped or not, drawn off course: past 15 and past 45 degrees from the wire
  velocity with over 0.05 u of the step off its line (the sideways part of a step forward, the whole
  of a step back), so a step back is past both. The angles depend on the frame rate, so they compare
  settings within a recording, not recordings with each other. `ComputeSmoothCellText` gives one
  cell as a `[snap] smoothing by kind` line's text, an empty cell none.
- `ComputeSmooth`: one frame for one ghost. It folds a target change into the offset only when a
  record or event arrives, decays the offset at the actor's speed over the newest record's tic gap,
  retains the part inside the rounding cell for extrapolated ghosts, and snaps above the threshold.
  It is not the engine's tic interpolation: a record lands at any point in a tic, since packets are
  read on every pass of the main loop, while `r_TicFrac` assumes the move happened at the tic's
  start. On a stepped ghost the decay is interpolation between its two newest records, one record
  gap behind (a tic when records come every tic, 2 to 4 for a walker), where stock slides over one
  tic. It takes the target after the wall clamp; the floor and ceiling clamps see its result. Angle
  is not smoothed. An extrapolated ghost takes the missile threshold if it is a ballistic missile,
  else the body's. At a threshold of 0 an extrapolated ghost is drawn on its target and every
  correction is still counted, as a snap, so the census compares smoothing off with it on. A ghost
  still owing an offset when its threshold becomes 0, whether by a change of setting or of kind (a
  body still settling that becomes ballistic), is drawn on its target and its offset cleared. The
  clear is counted as a snap in the cell of the kind it now is, sized by the offset dropped, with
  its back and off-course counts, so the jumps that mixing two thresholds makes are in the census (a
  stepped ghost's clear, outside it, is not counted, nor is one inside a map reset).
- `ComputeSmoothStepPop`: whether a stepped ghost is smoothed, by `fua_csnap_smooth_entities`.
- `ComputeAnchorStart`: the start a mid-tic refill sets so the frame draws the fold's own output,
  called by the mirrors here and by the replay's `anchor` mode ([csnap_replay.md](csnap_replay.md)).
- `ComputeSmoothSlides`: whether the renderer slides a ghost this frame, read after `ComputeSmooth`:
  held, outside the stepped smoothing (its population, or a threshold of 0), not smoothed as
  extrapolated, and not a walking step to draw raw. The mirror asks that of a monster
  (`MF3_ISMONSTER`) under `nomonsterinterpolation`, and it applies only with no wire velocity:
  stock's switch is in `P_Move`, so it draws a monster's walking steps raw while a lunge, knockback
  or thrust still slides. A monster knocked back while walking slides here where stock would draw
  that tic raw, an approximation. No other ghost is affected by that setting.
- `ComputeSlideFrame`: one frame of a mirror's position and `PrevX`, `PrevY`, `PrevZ`. A ghost that
  does not slide is put on its pose with the previous position equal; one that slides moves only on
  its first frame of a new tic, with the previous position where it stood, and holds between. A step
  past `SNAP_SLIDE_SNAP_UNITS` (32) units a tic of its record gap (1 to 4), a teleport or respawn,
  is drawn as it lands.

The animation decisions (`computation/anim_compute.h`), templates on the state key, the engine's a
state pointer:
- `SnapMsEntry`, `SnapMsRing`: the ring of states a mirror entered, `SNAP_MS_RING` deep.
- `ComputeMsPush`, `ComputeMsResync`, `ComputeMsOldest`: record a state entered, test a record
  against the ring (entered within the state's longest duration and `SNAP_MS_SLACK` tics of the
  record), and read the oldest entry. `ms_Classify` keeps the behind and undecidable walks over the
  real states.
- `ComputeShadowVerdict`: the loop census's verdict on a shadow against the wire's state, exact,
  behind, ahead or off-chain, each chain walked `SNAP_SHADOW_WALK` steps by the caller's successor
  function. `ComputeShadowBucket`: the census's gap bucket. `ghost_ShadowScore` advances the shadow
  and counts.

The wall clamp (`computation/wall_compute.h`):
- `ComputeSweepLine`: `PIT_CheckLine`'s contact test swept along a path: the box overlaps the
  segment's bounding box and straddles its line. The second test alone is the infinite line.
  `ComputeSweepSlab`: the slab narrowing it is built from.
- `ComputeWallPath` (`SnapWallPath`): a pose's path, reach and trace length.
  `ComputeWallProjectile`: whether the lines treat the body as a projectile.
- `ComputeWallContact` (`SnapWallContact`): where the path first touches one line, the engine's
  opening point there and the body's height, or nothing nearer than the line already found.
- `ComputeLineFlagsBlock` (`SnapLineFlags`, `SnapBodyFlags`): whether a two-sided line's flags stop
  the body, and whether it is a railing. `ComputeOpeningBlocks`: whether the opening does.

## Settings

All client only. Archived: `fua_csnap_collide` (on), `fua_csnap_collide_dist` (512 units),
`fua_csnap_rendercull` (on), `fua_csnap_cull_near` (3000 units), `fua_csnap_cull_far` (6000 units).
Unarchived: `fua_csnap_ghosts` (the master switch, set by the network path), `fua_csnap_localanim`
(3: the tiers below it animate, 0 none), `fua_csnap_smooth_step` (32 units a tic; 0 off),
`fua_csnap_smooth_nonballistic` (1 unit, for turning or unproven missiles, bodies being held; 0
off), `fua_csnap_smooth_ballistic` (0, for ballistic missiles; the census still counts at 0),
`fua_csnap_smooth_entities` (1: remote players; 0 none, 2 all; a held ghost outside it slides over
its tic, as stock), `fua_csnap_recmirror` (record one drawn-state change in N to a client recording;
0 none), `fua_csnap_loopcensus` (off; the loop-hint census's shadow state machines). Of these only
`fua_csnap_recmirror` and `fua_csnap_loopcensus` are dev-mode tools; the rest set how the game
looks, which is a player's choice on any server. Stock's
`nomonsterinterpolation` draws a walking monster's steps as they land, and `cl_capfps` draws every
actor at its tic's end; the recorder logs both.

`fua_csnap_ghostinfo [n | class]` prints what the client holds for the nearest ghosts, or for every
ghost of one class.

## Rules that must hold

- **A mirror never plays.** No action function runs on it except the spawners `MirrorSpawnAction`
  allows; it takes no damage, is never removed by gameplay, and blocks only.
- **The ghost frame runs before the copy is taken.** `FillNow` calls it ahead of its walk, so the
  copy the renderer draws holds this frame's mirrors; called after, every mirror would be a frame
  stale.
- **Mirror pointers are not dereferenced after teardown.** `LevelUnloaded` drops them blind, which
  is why the id list is kept rather than derived.
- **Pinned until destroyed on purpose.** A mirror holds `OF_Fixed` from creation, cleared only right
  before its `Destroy`.
- **Unlink before changing the blockmap flags.** `UnlinkFromWorld` is gated on `MF_NOBLOCKMAP`, so
  setting the flag first strands the actor in the blockmap.
- **A relink that skips the sector lists still has to free them.** `MF_NOSECTOR` makes `LinkToWorld`
  skip `touching_sectorlist`, so the list the unlink saved is freed explicitly or the actor stays in
  every sector's touching list.
- **Clear only the flags this file added** to a hidden actor; clearing one the class owns relinks it
  into a list nobody removes it from.
- **List surgery happens in the frame loop**, never on the packet path.
- **Animation runs on tics, per mirror.** A frame delta would animate a far mirror at half its
  speed.
- **Keyed by id, lives one map.** Every per-id table is cleared at a level change, since the next
  map reissues the ids -- the property sets with them.

## Engine hooks

The stock call sites, each a `[Kaori-Chani]` line. Seven files call this header:
- `features/gamecopy/gamecopy.cpp`: `GhostFrame` in `FillNow`, and `MirrorPlayerNum` in the sprite
  path for a player mirror's skin.
- `cl_main.cpp`: `LevelUnloaded` at the connection's map load (`SVCC_MAPLOAD`), on quitting a
  network game and at a map change; `GhostsFromNetwork` when snapshot traffic starts and stops;
  `CosmeticEvents` for `SVC2_SNAPCOSMETIC`; `ResolveThroughMirror` in the client's actor lookup; and
  `RespawnBehindMirror` for an item respawn behind a mirror.
- `p_setup.cpp`: `LevelUnloaded` in `P_FreeLevelData`.
- `p_mobj.cpp`: `MirrorSpawnAction` in `AActor::SetState`, beside the hidden actor's early return in
  `AActor::Tick`.
- `cl_pred.cpp`: `CensusScope` around the prediction replay's `P_PlayerThink` and the pawn's `Tick`
  in `client_predict_DoPrediction`.
- `p_tick.cpp`: `CensusScope` around `DThinker::RunThinkers` and `P_RunEffects`.
- `features/gameloop/loop.cpp`: `CensusScope` around `G_TickerPlaysim` and the per-tic `NetUpdate`.

The mirror guards elsewhere -- no damage in `p_interaction.cpp`, no removal in `p_things.cpp`, no
crushing in `p_map.cpp`, the collision in `PIT_CheckThing` -- are tests of `NETFL_MIRROR` and
`STFL_SNAPGHOSTHIDDEN`, not calls here.

Inside the module: `apply.cpp` calls `HideEnteredLegacy` and the fold; `capture.cpp` forwards the
cosmetic spawn, first tick and destroy, and declares `CatchUpHoldsSound`, which is defined here;
`movers.cpp` resolves a point pusher's source with `MirrorForNetID`.

## Tests

No self-test block for the mirrors: they need a level. The three units are GoogleTest files under
the 100% line gate:
- `computation/wall_compute_test.cpp`: the wall sweep (a long wall, a jamb passed, a segment's edge
  and miss, a reversed, slanted or zero-length segment), the path and contact, and the line and
  opening tests. One branch is never taken: a projectile reaches the line's projectile test only
  when that flag is set.
- `computation/smooth_compute_test.cpp`: the population, a stepped step, a lost record, a teleport,
  an extrapolated cap and correction; the paths (tracking unsmoothed, a reset, the census's buckets,
  past and reversed counts, each axis of the retained part); smoothing at 0 (drawn on the target,
  each correction a snap in its cell, the same inputs folding at 1, an owed offset cleared as one
  counted snap, nothing counted for an unsmoothed stepped ghost or a reset); the input fields read
  from a pose with each kind and a cell's text; by kind (a body and a turning missile at 1 and a
  ballistic missile at 0 given one correction, a kind change clearing an owed fold as a snap in the
  new kind's cell, the size buckets, and the off-course counts either side of 15 and 45 degrees);
  and the slide (who slides at each population, a frame held between tics and moved at a new one,
  the snap distance at and past its edge, and a walker at four frames a tic updating every tic and
  every two).
- `computation/anim_compute_test.cpp`, keyed by int: the mirror spawn ring (an empty ring, re-entry,
  the wrap, the window and an endless state) and the shadow verdict (each bucket's edges, and each
  verdict over a loop, a chain that ends, a state with no successor and the walk's last step).

`fua_csnap_analyze` reproduces the sampler and smoothing against a recording, without mirrors
([csnap_replay.md](csnap_replay.md)); live, the `[snap] ghostframe`, `[snap] localanim`, `[snap]
mirror spawns`, `[snap] cosmetic events`, `[snap] point sounds`, `[snap] extrap clamps`, `[snap]
properties`, `[snap] smoothing` and `[snap] smoothing by kind` lines report what the mirrors did,
and `fua_csnap_ghostinfo` separates a ghost the server is not updating from one this client is not
drawing.
