# chase-trim

Chasing costs less: far monsters move every other call, and walkers stuck in a crowd stop re-checking moves they know
are still blocked. A stuck monster also gets its missile check again.

## Far monsters (`sv_fua_farchase`, off by default)

A monster at least this many map units from every player's view point (camera, else body) skips movement on every
other chase call and walks a double step on the call between. It covers the same ground. A skipped call still
counts `movecount` down, and a call that would re-plan is never skipped, so it re-aims at its target and checks for
a missile attack on the same calls as before. Melee and missile checks
still run on every call. `P_Move` splits a step longer than the walker's radius into several moves, so a double step
cannot pass through anything. A far monster whose double step is refused walks single steps every call for
the next 8 calls, so a packed monster closet squeezes out as it always did. Nothing new is sent: clients draw the
server's positions. 0 turns it off.

## Blocked-direction memo

Most failed tries come from monsters standing in a crowd, blocked by other monsters. A chase call notes each
direction refused by one thing alone (`P_CheckPosition`'s "definitely blocking" exit, `tm.hardblocker`), or by a
wall or ledge (no thing, no special line). A walker that ends the call where it started keeps them.

Each later try is checked against what is remembered before the full check runs:

- A remembered actor still blocks if it exists, is still solid and its box still overlaps the step's first move.
  This is `PIT_CheckThing`'s own test: overlapping boxes, overlapping heights, too tall to step onto, and the step
  does not take the walker away from it. It holds while the blocker shuffles around, as long as it stays in the way.
- A remembered wall still blocks while the walker stands where it was refused and nothing in the world changed:
  any moving floor or ceiling (`P_ChangeSector`), line blocking change (`Line_SetBlocking`, ACS
  `SetLineBlocking`/`SetLineMonsterBlocking`, glass breaking) or polyobject move forgets every remembered wall, so a
  closet door opening or a monster-block line cleared is seen at once.
- Everything is forgotten when the walker moves, and after 35 tics.

Directions remembered from earlier calls apply only to a jammed walker: one whose last `sv_fua_jamafter` (default 3)
re-plans in a row failed every direction. Its jam ends the moment it moves. Within one call, a direction already
refused is never tried twice.

The memo is skipped where a failed check would do something:

- pushable, touchy, bump-special, bridge or pickup blockers,
- skull-flying, touchy or `THRUSPECIES` walkers,
- players,
- monster stairs,
- clients.

It is not exact: a blocker that changes flags, or a wall opened some other way, is noticed late, within 35 tics. `fua_dev_blockmemo 2` predicts without skipping and counts wrong predictions (`fua_dev_blockmemo_report`).

## Jammed walkers

When a jammed walker has every direction known blocked, the re-plan itself is skipped (`fua_dev_jamskip`). It
wakes as soon as any remembered blocker steps out of the way or a wall is forgotten. The walker ends exactly
as a re-plan that failed everywhere would: no direction, `strafecount` 0, and the stuck-attack count. Walkers that
strafe away from melee, or hang over a ledge, still re-plan.

`sv_fua_jamwait` (default 2) also makes a jammed walker skip movement for that many chase calls after each failed
re-plan. On a six-closet test map (imps, revenants, mancubi, arachnotrons, archviles and a mix, 12 seeds each) it
did not measurably slow how fast closets empty; the first half of an imp closet got out about 0.3-0.4 s later.

## Stuck monsters attack (`sv_fua_stuckattack`, default on)

Doom checks for a missile attack only when `movecount` is 0. When a re-plan finds no direction, `movecount` stays
below 0, so a fully stuck monster never fires. With the setting on, the count restarts from a random 0-15, like a
walk would, so the check comes round at a walker's pace. Its rolls come from a random stream of their own,
`FuaStuckWalk`.

## Hooks

- `p_enemy.cpp`:
  - `P_Move`: `StepScale`, `FailKnownBlocked` and `AfterMove`.
  - `P_DoNewChaseDir`: `OnReplanFailed`.
  - `A_DoChase`: `ChaseStep`, and a `ChaseMove` around movement.
- `p_map.cpp`: `P_CheckPosition` sets `tm.hardblocker`; `P_ChangeSector` calls `WorldChanged`.
- `p_lnspec.cpp`, `p_acs.cpp`, `po_man.cpp`: `WorldChanged` where a line's blocking changes or a polyobject moves.
- `p_local.h`: `FCheckPosition::hardblocker`.
- `actor.h`: `FuaChasePhase`, `FuaJamWait`, `FuaJamCount` (padding after `visdir`, the chase counters' line),
  `FuaBlockMemo`, `FuaBlockers` (the cold block).
- `CMakeLists.txt`: `chasetrim.cpp` in the `zdoom` sources.
- `p_mobj.cpp`: `FuaBlockers` in the pointer list, so the GC clears a destroyed blocker.

## Tests

- `computation/chasetrim_compute_test.cpp`: the memo, the far step and the jam rules.
- `tools/fuactl/scenarios/chaseclosets.mjs`: six packed closets (imps, revenants, mancubi, arachnotrons, archviles,
  a mix) woken by sound; each closet's monsters still inside 30 s later are held under a ceiling, so a change that
  stalls crowds fails (`--side closets --cvar sv_fua_jamafter=1,sv_fua_jamwait=200` fails the imps, revenants, arachnotrons and archviles).
