# netids

Network ids found in a bitmap. `IDList::getNewID` used to look for the next free id by walking the
table one entry at a time. When the table is full, every spawn walks all 65,535 entries and finds
none. That happens on nuts.wad with Complex Doom, which reaches 70,000 actors, and there the walk was
44% of the game thread.

`IDList` now keeps which ids are free in a bitmap, its only record of that, in place of a flag on each
entry. The search reads a 64-bit word at a time and returns the same id the walk did, in the same order.

## Files (this folder)

- `computation/netidsearch_compute.{h,cpp}`: `ComputeMarkId`, `ComputeIdFree` and `ComputeNextFreeId`.
- `computation/netidsearch_compute_test.cpp`: their tests, including a check against the old walk on
  random tables.

## In-place engine edits

- `actor.h`: `IDList` holds the bitmap `_freeBits` in place of each entry's `bFree`, sized by `kIdCount`;
  `freeID` marks the id free and `findPointerByID` reads it.
- `p_mobj.cpp`: `IDList::useID` reads and marks the id, and `IDList::getNewID` takes the next free id from
  `ComputeNextFreeId`.
