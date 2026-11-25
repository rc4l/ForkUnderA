# AActor's field layout

An actor is 64-byte aligned (`dobject.cpp`), and `actor.h` orders AActor's fields by the passes that read them, measured with
`fua_dev_actor_access` (features/actor-access) on the Sunder MAP11 laps and on a hosted server and its client:
- line 1 holds what another actor's collision check reads;
- lines 2-3 hold the actor's own tick;
- lines 4-5 hold the render copy;
- lines 6-9 hold the links, chase and step extras;
- lines 10-11 hold a server's position-update bookkeeping;
- the cold fields follow.

Each group is 64 bytes. Adding a field to one moves every group after it, so a new field goes with the cold ones unless an
access trace says otherwise. On the three laps a monster now touches ~8 lines every tic instead of ~15.

- **Prefetch.** With the hot data packed, a tick no longer streams through the whole actor, so the hardware prefetcher stops
  pulling in the next actor's first line, and the thinker loop waited on it. `DThinker::TickThinkers` now fetches the next
  thinker's hot lines while one ticks (`TickBytes`, two more lines on a server), and the render copy fetches the lines
  `TakeActor` reads a few actors ahead (`CopyBytes`).
- **Water levels.** `UpdateWaterLevel` ran on every move and wrote `waterlevel` and `boomwaterlevel` each time, zeros on a
  dry map. It now measures into a local and stores each only when it changes (`ComputeWaterStore`), with both on line 2.
  They stay two fields: DECORATE reads `waterlevel` as a native int at its offset.
- **Collision walks.** `FBlockThingsIterator::FuaCompactFor` reads the mover's radius and missile flag once a walk, not per
  candidate.

`computation/waterlevel_compute.h`: the water stores; `computation/actorprefetch_compute.*`: how much a tick fetches;
`actorprefetch.*`: the prefetch and the byte counts. Hooks: `actor.h` (the layout), `dthinker.cpp` (`TickThinkers`),
`features/gamecopy/gamecopy.cpp` (the actor take), `p_mobj.cpp` (`UpdateWaterLevel`), `p_local.h` and `p_maputl.cpp`
(the walk's mover values).
