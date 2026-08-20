# features/surfaces

**One surface type, derived from the map rather than transcribed from GL.**

A surface is geometry, a material, a light level and a colormap. A sidedef part is one; a sector
plane is one; the mesh has treated them identically for a long time — a `MeshPiece` does not know
which kind made it, it goes in one buffer, sorts by one key and takes its light from one path.

What is still doubled is the **derivation**. GL walks the BSP and produces a `GLWall` or a `GLFlat`,
and `features/levelmesh` keeps two transcriptions of that — `wallcache.cpp` and `flatmesh.cpp`, two
caches, two stamps, two slot tables, two piece-building tails. Every feature is therefore written
twice, and the halves drift. The bill, all of it paid in one week:

- batched walls lost their dynamic lights while flats kept theirs
- a stale light index could only ever affect walls
- the flat lookup was O(flats²) per frame for months while walls had an index
- flats had no cache at all while walls had one from the beginning
- projected decals had to be taught floors separately from walls, and each side broke the other twice

Doom's own data has no such split: a sidedef part and a sector plane are the same kind of thing, and
`GLWall` versus `GLFlat` is a 1993 renderer's distinction between drawing columns and drawing spans.
This directory is where the split stops being inherited.

## Why it is not under features/levelmesh

Deliberately, and it is the same point as the split itself: file layout is a claim about what belongs
with what. `levelmesh` owns *storage* — the mesh, the arena, the caches that hold what was captured.
`surfaces` owns *derivation* — working out what a piece of the map looks like, from the map. Putting
the derivation beside the caches would have it inherit their shape, which is GL's shape, which is the
thing being retired.

## What is here

- `computation/wallgeom_compute` — the vertical spans a sidedef contributes: upper, lower, middle.
  Engine-free and tested off-engine, because "what does this sidedef look like" is answerable without
  a camera, a level or a screenshot, and every answer that has to be checked by looking at a picture
  costs a day the first time it is wrong.

## Where it stands, measured

`fua_surface_verify` compares the derivation against the capture on a loaded level. Three ladders,
because three different things can be wrong and a single number cannot say which:

| map | geometry | alignment | planes |
|---|---|---|---|
| dbab01 | 99.7% | 92.3% | 100.0% |
| dbab02 | 97.9% | 79.4% | — |
| dbab04 | 100.0% | 84.1% | 99.1% |
| Sunder MAP10 | 99.9% | 95.6% | 99.4% |
| Sunder MAP16 | 93.9% | 82.8% | 99.9% |

**Rules the ladders found**, none of which was obvious from looking at a wall: an upper cannot start
below the back floor *or* the front floor; a two-sided middle hangs by its own height rather than
filling the opening; the material's render height is not the texture's scaled height; `expand=true`
adds a two-pixel border that reads as "two units too tall everywhere"; alignment references the
plane's *texture Z*, not its live height, from a reference **pair** that differs per part.

**Hypotheses killed by measurement**, each of which looked right: the piece belonging to the other
sidedef (0 pieces, on every map); a texture taller than its span pegging the other way (perfect
correlation over the failures, and applying it halves the score — the correlation was selection bias,
since pegging can only differ where the texture does not fill the span).

**Open**: the peg condition on a subset of uppers and lowers — 58 pieces on dbab01, 127 on dbab04,
2277 on Sunder MAP16 — all of which agree when the flag is flipped, none explained by sky or by the
sidedef. And Sunder MAP16's fence category, where GL draws 40 units of a 128-unit texture with every
visible input saying 128.

## What comes next, in order

1. UV derivation for those spans — pegging, offsets, scaling.
2. The plane equivalent, which is mostly `flatmesh_compute` moved here and renamed for what it is.
3. Slopes, which stop being a special case once a plane is a plane.
4. Wiring: the mesh builds from map data, and the capture path is what handles the cases the
   derivation has not learned yet, rather than the other way round.

`GLWall::Process` is a thousand lines of accumulated cases. It gets replaced one answerable question
at a time, and every question keeps its answer in a test — not by a rewrite that has to be right
everywhere before it can be run once.
