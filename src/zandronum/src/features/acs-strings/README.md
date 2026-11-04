# ACS string pool growth

ACS strings made at run time (`StrParam` and the like) live in one pool, `GlobalACSStrings`. Inserting into a
pool whose next free slot is at its room collects it first: every global, world and map variable and every
global array is walked for string ids, and the strings none holds are freed. The pool only grew when a
collection freed nothing, so a mod that keeps a full pool and makes a few dozen strings a tic was collected
again as soon as the few it freed were used. DnD on Sunder MAP11 collected every ~4 tics, ~119 strings freed
each time, ~1.8 ms a collection on the game thread.

- After a collection that freed under a quarter of the pool, the pool's room doubles
  (`computation/stringpool_compute.cpp`), so the next collection waits for as many new strings as the pool
  held.
- Strings get other ids than before, since fewer slots are reused: a fixture trace that hashes ACS
  variables holding string ids reads differently, and its baseline is recorded again.
- UZDoom collects at the same point. GZDoom 66d15dc2156cb5f1e7c8ac53c1850b4a07c8d964 (reserve
  `MIN_GC_SIZE` entries when growing) and its fix 5df5e2abe720ca81a391eea20695afdfa10fc1a0 change how many
  entries a growth adds, not when collections run, and are not needed here, since growing the room adds no
  entry.

## Literal ids cached per module

Every string literal a script pushes runs `PCD_TAGSTRING`, which turned the module's string into a pool id by
hashing it and searching a bucket each time; Complex Doom's effect actors run a script each that pushes a few
cvar and state names, ~460 starts a tic. A pool id only changes when strings are purged, cleared or loaded, so each
module now keeps its literals' ids (`TagCache`, `ComputeTagCacheFind`/`ComputeTagCacheStore`) for the pool's
generation, which `Clear` and `PurgeStrings` move on (`ReadStrings` clears first).

## Hooks

- `p_acs.cpp` `ACSStringPool::InsertString`: grows the pool's room by `ComputeStringPoolGrowth` after a
  collection; `P_CollectACSGlobalStrings` returns what it freed.
- Telemetry: `acs/string_collect` (time), `acs/string_collects`, `acs/strings_freed`.
- `p_acs.h`/`p_acs.cpp`: `ACSStringPool::Generation` (moved on in `Clear` and `PurgeStrings`), `FBehavior::TagIds`, and `PCD_TAGSTRING` reading through them.
