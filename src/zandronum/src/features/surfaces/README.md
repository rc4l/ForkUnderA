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

Three ways of deciding WHERE the shift applies have now been tried and measured, and all three land
on the same numbers -- 89.1% / 75.9% / 86.6% against 92.3% / 79.5% / 84.1% without it:

1. every part `DoTexture` makes (upper, lower, one-sided middle);
2. the shift computed from the whole part's derived span rather than the surviving fragment's
   own top, which is what a fragment inherits;
3. skipping `SplitWall` fragments entirely, which `GLWF_NOSPLITUPPER`/`LOWER` make identifiable.

Three different rules producing one identical score says the discriminator is not in this family
at all. The shift itself is not in doubt -- it is a function in `gl_walls.cpp` and
`ComputeVShift` transcribes it with tests -- so what is missing is the condition, and the
capture-time census is the thing to pull on: it is one command, and it says which walls arrive
already inside their first texture copy and which do not.

**Hypotheses killed by measurement**, each of which looked right: the piece belonging to the other
sidedef (0 pieces, on every map); a texture taller than its span pegging the other way (perfect
correlation over the failures, and applying it halves the score -- the correlation was selection
bias, since pegging can only differ where the texture does not fill the span); and the peg flag
being inverted on a subset, which the delta measurement retired outright.

The taller-texture rule had a `fua_surface_pegrule` switch so it could be A/B'd rather than argued
about. It has been **deleted**: a rule that halves the score is not a candidate any more, and a
switch nobody should ever turn on is worse than a paragraph. The measurement above is the record.

**Still open**, and better understood than it was: Sunder MAP16's fence. GL draws 40 units of a
128-unit texture (OFENCB01, pegged bottom, no scale) where every visible input says 128.

What the last look added: the seg carries **two** middle pieces, one `RENDERWALL_M2S` and one
`RENDERWALL_M2SNF`, at -244..-204 and -304..-244. That is a midtexture SPLIT across a 3D floor's
light boundary -- so part of this category may be the LADDER rather than the derivation: it compares
the union of pieces of one type, and a surface split across two types has no single union. The
capture's own piece 0 also carries `GLT_CLAMPY`, which is set on a wall occupying exactly one copy of
its texture, so GL believed it was drawing a whole texture into 40 units.

Whichever of those it is, it is the last thing between the derivation and being able to replace the
capture rather than correct it, and it is one texture on one map.

## Wired in, and on: the derivation builds the walls

`fua_surface_derive` is on. The bake takes a wall part's vertical SPAN and BOTH of its texture
coordinates from the map instead of from `GLWall`. What still comes from the capture is which
surfaces exist at all, the special kinds -- 3D floor faces, skies, horizons -- and the shading, which
is deliberately last.

| map | surfaces derived | from the capture | picture vs GL | control |
|---|---|---|---|---|
| Doom 2 MAP01 | 343 | **0** | 0.0% | 0.0% |
| dbab02 | 1733 | 519 | 0.0% | 0.0% |
| dbab04 | 3463 | 636 | 0.0% | 0.0% |
| Sunder MAP10 | 15350 | 5 | 0.0% | 0.0% |
| Sunder MAP16 | **71743** | **6** | 0.0% | 0.4% |

Pixel-identical everywhere. MAP16's own control -- two map reloads with nothing changed -- reads
0.4%, which is what a level that size does between reloads, and the derivation reads 0.0% against it.

**The horizontal coordinate turned out to be the easy half**, which is worth recording because it was
carried as the hard one for a long time. `GLWall::Process` draws an ordinary wall over the whole
LINEDEF with `fracleft` 0 and `fracright` 1 -- not per seg -- so the two edges are just the sidedef's
x offset and that plus the line's texel length. There is no seg-along-line bookkeeping to model.
Polyobjects are the exception, are drawn per seg with real fractions, and are left to the capture.
Deriving it took Sunder MAP16 from 2.3% to 0.0%.

## And the walk itself: a level's walls built without GL

`fua_surface_mapbake` rebuilds every wall in the level from the seg array, with no `GLWall` involved
at any point -- geometry, texture coordinates, material and shading all derived. It was the last
dependency in the phase: everything about a wall was derived except WHICH walls there are, and that
came from GL walking the BSP and reporting what it drew.

| map | parts built from the map | segs | vs GL's own bake | control |
|---|---|---|---|---|
| Doom 2 MAP01 | 393 | 373 | 0.0% | — |
| dbab04 | 1799 | 1543 | 0.1% | — |
| Sunder MAP16 | **81153** | **69568** | 0.4% | 0.0% |

`fua_surface_mapcover` is what said this was possible before it was written, and it is the number to
watch: on dbab04 the map accounts for 1332 of the 1336 parts GL draws; on Sunder MAP16, 59,477 of
59,483 across 52,052 segs. Two categories had to be found to get there, and both were in
`GLWall::Process` rather than in the map:

- **a sloped wall is drawn if EITHER end has area**, not both. `if (topleft<=bottomleft &&
  topright<=bottomright) return;` is the whole of GL's test, and requiring both ends left every wall
  that pinches out at one end unaccounted for.
- **a sloped step with no texture on the sidedef is drawn with the SECTOR'S FLAT** -- "with a
  background sky there are ugly holes otherwise". That one category was 131 parts on dbab04.

MAP16's 0.4% against a 0.0% control is the honest residual and the next thing to look at. It is six
parts GL draws that the map does not claim, on a level with eighty-one thousand.

## What the wiring proved about the ladders

With the derivation live, the alignment ladder still reads 92.3% / 79.5% / 84.1% -- and the rendered
frame is **0.0% different from GL's**. Both numbers are correct and the gap between them is the
useful part.

Every one of those misses is a whole-texture offset, and none is on a wall that clamps. A texture
slid by an exact multiple of its own height, on a wall that wraps, is the same picture. The ladder
compares a NUMBER and reports a difference; the eye compares the picture and there is none.

So the alignment ladder was more pessimistic than reality by exactly the amount that was already
measured and written down. It stays as it is -- a number that is right for the wrong-looking reason
is still the number that will catch the next real fault -- but "alignment is only 84%" was never the
blocker it read as.

## ...and its light, which was the last of it

`fua_surface_derive_light` is on too. A wall's light level, its fake-contrast term and its colormap
come from the sector and the sidedef now, not from a `GLWall`.

**This is not a second lighting implementation, and that distinction is the whole reason it was safe
to do.** `CaptureShading` calls the engine's own `gl_SetColor` and `gl_SetFog` and reads the answer
back out of `FRenderState` -- that is what has kept the two renderers agreeing, and it is unchanged.
What moved is where its three *inputs* come from.

Pixel-identical: 0.0% on Doom 2 MAP01 and dbab04, 0.1% on Sunder MAP16 against that map's own reload
noise floor of 0.4%.

A sector with a 3D floor light list keeps the capture: `SplitWall` cuts the wall into bands and gives
each its own light, so there is no single light level to derive, and deriving one would be wrong
exactly in the rooms people build 3D floors for.

The fallback column is broken down by reason in `fua_dg_dynstats`, because "1073 fell back" does not
say what to build next and "467 special walls, 52 no span" does.

## What comes next, in order

1. Sunder MAP16's remaining disagreement, which is the last thing between the derivation and being
   able to replace the capture rather than correct it. The fence category is named below.
2. The horizontal coordinate, which needs the seg-along-linedef bookkeeping the derivation does not
   model yet -- and a ladder of its own before any of it goes in.
3. Shading -- light level, colormap, fog -- which the capture takes from GL through `CaptureShading`.
   Deliberately last: it is the part where a second implementation drifted before, and it is why
   `CaptureShading` exists at all.

`GLWall::Process` is a thousand lines of accumulated cases. It gets replaced one answerable question
at a time, and every question keeps its answer in a test -- not by a rewrite that has to be right
everywhere before it can be run once.
