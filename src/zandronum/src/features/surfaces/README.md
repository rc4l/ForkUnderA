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

Engine-free and tested off-engine, all of it, because "what does this sidedef look like" is
answerable without a camera, a level or a screenshot — and every answer that has to be checked by
looking at a picture costs a day the first time it is wrong.

- `computation/wallgeom_compute` — the vertical spans a sidedef contributes: upper, lower, middle.
- `computation/walluv_compute` — where the picture sits on them: the coordinate along the line, the
  coordinate down from the pegging reference, and the reference itself.
- `computation/planegeom_compute` — the same questions for a floor or a ceiling: height at a point,
  sloped or not, which side it is seen from, and the normal that follows the viewed side rather than
  the plane.
- `surfaceverify.cpp` — `fua_surface_verify`, which compares all of it against what the capture
  recorded, on a real level.

## Where it stands, measured

`fua_surface_verify` compares the derivation against the capture on a loaded level. Three ladders,
because three different things can be wrong and a single number cannot say which. Run them all with
`node scenarios/surfaceladder.mjs <maps...>`, which also carries the A/B for each candidate rule.

| map | geometry | alignment | planes | alignment misses that are a whole texture |
|---|---|---|---|---|
| dbab01 | 99.7% | 92.3% | 100.0% | 91 of 91 |
| dbab02 | 97.9% | 79.5% | 100.0% | 228 of 228 |
| dbab04 | 100.0% | 84.1% | 99.1% | 159 of 159 |
| Sunder MAP10 | 99.9% | 95.6% | 100.0% | 566 of 566 |
| Sunder MAP16 | 93.9% | 82.8% | 100.0% | 10181 of 10265 |

**Planes are exact now, on every map.** The last percent was not a derivation fault at all: the flat
cache was not being cleared on a level change, so a fresh level was compared against the previous
one's flats through pointers it had already reused. That is fixed in `AllocForLevel`, and it was
taking the process down as well as the score -- see [features/levelmesh](../levelmesh) for the note.

**Rules the ladders found**, none of which was obvious from looking at a wall: an upper cannot start
below the back floor *or* the front floor; a two-sided middle hangs by its own height rather than
filling the opening; the material's render height is not the texture's scaled height; `expand=true`
adds a two-pixel border that reads as "two units too tall everywhere"; alignment references the
plane's *texture Z*, not its live height, from a reference **pair** that differs per part.

## The pegging question, answered: it was never pegging

Two rounds of inferring a peg CONDITION from which pieces failed produced rules that correlated
perfectly and halved the score when applied. The third round asked a different question -- not
*which* pieces are wrong but *by exactly how much*, in the units of the one line of `DoTexture` that
can differ:

```
if (peg) floatceilingref += mRenderHeight - (lh + v_offset)
```

Measured that way the answer arrived immediately and it is the same on every map: **every alignment
miss is an integer number of whole textures**, and the peg shift is never the difference. Not one
piece anywhere is off by the peg term. The 84 exceptions are all on Sunder MAP16 and are the fence
category below.

A whole texture off is the same picture on a wall that wraps, and **none** of the ones counted are on
a wall GL clamps -- so what is left is not visible. It is still not right: the number decides whether
`GLT_CLAMPY` gets set, and a clamped wall shows the difference at once.

**Where it comes from, and what is still open.** GL's `CheckTexturePosition` slides a finished wall
back into the first copy of its texture by subtracting `floor(min(v at the two top corners))`. That
is modelled in `walluv_compute` (`ComputeVShift`) and it is real -- turning it on fixes 39 pieces on
dbab01. It also breaks 77, so it is behind `fua_surface_vshift`, default off, and the score above is
the honest one without it.

What the breakage says is worth more than the rule: `fua_surface_verify` counts, at the moment of
capture, how many walls arrive with their top v already inside `[0,1)`. On dbab01, 129 of 1180 cached
pieces do not -- and `CheckTexturePosition` guarantees exactly that range for everything `DoTexture`
makes. So those pieces did not go through it, or something moved them afterwards. About a quarter of
them are fragments `SplitWall` produced, whose v is interpolated from a parent that was already
normalised; the rest are not yet accounted for.

Computing the shift from the whole part's derived span instead of the surviving fragment's own
top -- which is what the fragment inherited -- was tried and measured: dbab04 improves (84.1% to
86.6%) and dbab01 and dbab02 get worse. So the fragment story is part of it and not all of it.
That census is the thing to pull on next, and it is one command.

**Hypotheses killed by measurement**, each of which looked right: the piece belonging to the other
sidedef (0 pieces, on every map); a texture taller than its span pegging the other way (perfect
correlation over the failures, and applying it halves the score -- the correlation was selection
bias, since pegging can only differ where the texture does not fill the span); and the peg flag
being inverted on a subset, which the delta measurement retired outright.

The taller-texture rule had a `fua_surface_pegrule` switch so it could be A/B'd rather than argued
about. It has been **deleted**: a rule that halves the score is not a candidate any more, and a
switch nobody should ever turn on is worse than a paragraph. The measurement above is the record.

**Still open**: the 84 pieces on Sunder MAP16 where GL draws 40 units of a 128-unit texture with
every visible input saying 128, and the capture-time census above.

## What comes next, in order

1. Close the peg-condition subset and the MAP16 fence category — both listed above with their
   numbers, both reproducible with one command.
2. Wiring: the mesh builds from map data, and the capture path handles only the cases the derivation
   has not learned yet, rather than the other way round. The ladders are what say when a case is
   ready to move across.
3. Shading — light level, colormap, fog — which the capture currently takes from GL through
   `CaptureShading` and which a derivation would have to answer for itself. That one is deliberately
   last: it is the part where a second implementation drifted before, and it is why `CaptureShading`
   exists at all.

`GLWall::Process` is a thousand lines of accumulated cases. It gets replaced one answerable question
at a time, and every question keeps its answer in a test — not by a rewrite that has to be right
everywhere before it can be run once.
