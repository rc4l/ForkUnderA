# AActor's field layout

An actor is 64-byte aligned (`dobject.cpp`), and `actor.h` orders AActor's fields by the passes that read them, measured with
`fua_dev_actor_access` (features/actor-access) on the Sunder MAP11 laps and on a hosted server and its client:
- line 1 holds what another actor's collision check reads;
- lines 2-3 hold the actor's own tick;
- lines 4-5 hold the render copy;
- lines 6-9 hold the links, chase and step extras;
- the cold fields follow.

Each group is 64 bytes. Adding a field to one moves every group after it, so a new field goes with the cold ones unless an
access trace says otherwise. On the three laps a monster now touches ~8 lines every tic instead of ~15.

- **Prefetch.** With the hot data packed, a tick no longer streams through the whole actor, so the hardware prefetcher stops
  pulling in the next actor's first line, and the thinker loop waited on it. `DThinker::TickThinkers` now fetches the next
  thinker's hot lines while one ticks (`TickBytes`), and the render copy fetches the lines
  `TakeActor` reads a few actors ahead (`CopyBytes`).
- **The second render word.** The render copy reads `renderflags2` every tic, so it holds line 4's place of `fillcolor`,
  which moved to the cold fields: the copy reads the stencil colour only for a style that draws with it
  (`STYLEF_ColorIsFixed`). On Sunder MAP11 (6,029 actors) a cold read of it took the actor take's median p95 from 0.307
  to 0.369 ms; in fillcolor's place it reads 0.253 against 0.252 without it, interleaved three rounds.
- **Water levels.** `UpdateWaterLevel` ran on every move and wrote `waterlevel` and `boomwaterlevel` each time, zeros on a
  dry map. It now measures into a local and stores each only when it changes (`ComputeWaterStore`), with both on line 2.
  They stay two fields: DECORATE reads `waterlevel` as a native int at its offset.
- **Collision walks.** `FBlockThingsIterator::FuaCompactFor` reads the mover's radius and missile flag once a walk, not per
  candidate.

`computation/waterlevel_compute.h`: the water stores; `actorprefetch.*`: the prefetch and the byte counts. Hooks: `actor.h` (the layout), `dthinker.cpp` (`TickThinkers`),
`features/gamecopy/gamecopy.cpp` (the actor take), `p_mobj.cpp` (`UpdateWaterLevel`), `p_local.h` and `p_maputl.cpp`
(the walk's mover values).

## Off the actor

Fields most actors never use live outside it, so every actor is smaller: 1,576 bytes became 1,080 (25 cache lines to 17).

- **Map-reset values** (`mapresetstate.h`, 136 bytes): what P_SpawnMapThing records for the map reset (special, args, tid,
  pitch, roll, scale, style, alpha, fill colour, gravity, score, health, conversation, bob phase, user variables) is
  `ZxMapResetState`, pointed to by `FuaMapReset`. `Saved()` reads a shared all-zero set for an actor that has none, as those
  fields read before; `SavedForWrite()` makes one.
- **Sent motion** (`netdelta.h`, 56 bytes): the tics and velocities a server last sent of an actor in a MoveThing are
  `ZxNetDelta`, pointed to by `FuaNetDelta` and made when the first MoveThing goes out. The snapshot carries every other
  actor's motion at the defaults, so only players and a server without snapshots make one. `lastX/Y/Z` stay on the actor:
  every spawn writes them.
- **Rare fields** (`ZxActorExt` in `actor.h`, 232 bytes): Strife's conversation, accuracy, stamina and tag; team, score
  and invasion fields; poison; ripper levels and ledger; the decal generator, start health, respawn count, fill colour,
  weapon special and fixed colormap; the dynamic lights and `fua_Material` name. `FuaExt` points at the class defaults'
  block until the actor writes one of them: `Ext()` reads, `ExtW()` copies the block for this actor on the first write,
  and `SetExt()` writes only a value the actor does not already read, so spawn-time writes of the default make nothing.
  Every class's defaults own their block (`OwnExt` after `dobjtype.cpp`'s copy), so a later decal or DeHackEd write to a
  parent stays the parent's. Ownership is the owner's address, so `PClass::Extend`, whose realloc for user variables can
  move a class's defaults, hands the block back to the moved defaults (`ReclaimExt`); a child class made at the old
  address would otherwise write its parent's block. `PClass::StaticFreeData` frees each class's block. DECORATE's natives among them carry an offset past any actor (`DEFINE_EXT_MEMBER_VARIABLE`,
  `computation/actorext_compute.*`), which an expression and `features/acs-fields` resolve in the block. A save writes
  each field where it was, so the format is unchanged.
- **Chase-trim's memo** (88 bytes): a jammed walker's blocked directions and blockers live in a pool in
  `features/chase-trim`, found by `FuaMemoIndex`; see that README.

A copied actor gets its own copy of each block (`computation/ownedblock_compute.h` for the two it owns alone). The cold
fields are ordered so no padding falls between them.

Hooks: `actor.h` (the blocks and their accessors), `p_mobj.cpp` (the destructor, the copy constructor and assignment,
`Serialize`, `MarkPooled`, `PointerSubstitution`, P_SpawnMapThing's writes), `dobjtype.cpp` (each class's defaults own
their block), `thingdef/thingdef.h` and `thingdef/thingdef_expression.cpp` (the natives in the block),
`features/acs-fields/acsfields.cpp`, `sv_commands.cpp` (the sent motion), `g_game.cpp` (the map reset's reads and copies),
`p_setup.cpp` (a map thing's user variables), `dobject.cpp` (`ResetUserVars`), `features/dynlights/a_dynlight.cpp`
(`AActor::PropagateMark` calls `MarkPooled`), and every reader and writer of a moved field through `Ext()`, `ExtW()` and
`SetExt()`.

Tests:
- `tools/fuactl/scenarios/resetsaved.mjs`: a killed statue and a changed one each restored by a map reset with every saved
  value, and the changed one's special running with its map argument.
- `tools/fuactl/scenarios/rarefields.mjs`: a script's change to one statue's score, accuracy and stamina stays that
  statue's; its sibling, a child class, an heir and a later spawn read their class values through ACS properties, a
  DECORATE expression and `fua_GetField`, before and after a savegame; and 64 parents whose user variables grew their
  defaults keep their accuracy beside a child made after each (fails every run without `ReclaimExt`).
- `computation/actorext_compute_test.cpp`, `computation/ownedblock_compute_test.cpp`.
