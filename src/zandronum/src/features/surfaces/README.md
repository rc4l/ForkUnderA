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

| map | geometry | planes | alignment | of which REAL |
|---|---|---|---|---|
| dbab01 | 99.7% | 100.0% | 89.1% | **0** |
| dbab02 | 99.5% | 100.0% | 75.9% | **0** |
| dbab04 | 100.0% | 99.1% | 86.6% | **0** |
| Sunder MAP10 | 100.0% | 100.0% | 91.8% | **0** |
| Sunder MAP16 | 100.0% | 100.0% | 90.9% | **0** |

**Read the last column, not the one before it.** Every remaining alignment disagreement is a wall
that REPEATS, drawn a whole copy of its texture up or down. That is the same picture -- the pixels
cannot tell and neither can the player. The ladder prints the two separately for exactly this reason:
a number that is right for a wrong-looking reason is still the number that catches the next real
fault, but "alignment is 86%" was never the blocker it read as.

Getting the real column to zero took three corrections, and two of them were the ladder's:

- **`CheckTexturePosition`**, which GL runs on every wall it builds and the derivation did not:
  subtract `floor(min(uplft.v, uprgt.v))` from all four v values so the wall starts inside the first
  copy of its texture. Invisible on a wall that repeats; on a wall that CLAMPS it is the whole
  picture, because outside [0,1] the clamp holds the edge texel and the wall becomes a smear of one
  row of pixels. 112 pieces across three maps were outside their texture. Now none are.
- **The ladder was scoring a second transcription of the derivation rather than the derivation.**
  Both halves of it -- `ExpectedSpan` worked the parts out again, and the alignment check recomputed
  `ComputeWallV` itself. So the shift landed and the number did not move, and GL's midtexture clip
  landed and the geometry number did not move. Both call `BuildDerivedWallSpan` now.
- **The delta classifier was asking what was wrong with an answer nobody gives**, measuring against
  the unshifted texture top. Corrected, it reports zero peg-shift disagreements where it had reported
  sixteen -- the same false lead the peg-condition hunt spent two rounds on.

**Rules the ladders found**, none of which was obvious from looking at a wall:

- An upper's bottom is cut by the **front floor** and a lower's top by the **front ceiling**, each
  only when BOTH ends of the wall ask for it. Clamping to the back planes, per end, is the same
  answer on every wall whose two ends and two floors agree -- which is most of a level, and why it
  passed -- and a different one the moment anything slopes.
- A wall is drawn when **either** end has area, and it comes out as a triangle when only one does.
- A two-sided middle **hangs by its own height** rather than filling the opening...
- ...and is **not clipped to the opening**. Which plane clips it turns on whether the sidedef carries
  an upper or a lower texture at all: with no upper it is clipped to the ceiling ABOVE the opening,
  not below it, and an intra-sky line with no upper is not clipped at all. Assuming the opening cut a
  128-unit grate on dbab02 down to 96, and it was the whole of Sunder MAP16's "fence" -- the one
  where GL drew 40 units of a 128-unit texture with every visible input saying 128. MAP16's geometry
  went from 93.9% to 100.0% when that rule went in.
- A sloped step with **no** sidedef texture is drawn with the SECTOR'S FLAT.
- The material's render height is not the texture's scaled height; `expand=true` adds a two-pixel
  border that reads as "two units too tall everywhere".
- Alignment references the plane's *texture Z*, not its live height, from a reference **pair** that
  differs per part.

## The pegging question, answered: it was never pegging, and it was never a condition

Two rounds inferred a peg CONDITION from which pieces failed, and produced rules that correlated
perfectly over the failures and halved the score when applied. The third round asked a different
question -- not *which* pieces are wrong but *by exactly how much*, in the units of the one line of
`DoTexture` that can differ:

```
if (peg) floatceilingref += mRenderHeight - (lh + v_offset)
```

Measured that way the answer arrived immediately: **not one piece anywhere is off by the peg term.**
Every miss is an integer number of whole textures.

Which pointed at `CheckTexturePosition` -- GL slides a finished wall back into the first copy of its
texture by subtracting `floor(min(v at the two top corners))` -- and there the hunt stalled for
another round, because three different rules for WHERE the shift applies all landed on the same
score, and three different rules producing one identical number says the discriminator is not in that
family at all.

It was not. **The ladder was scoring its own copy of the derivation.** The alignment check recomputed
`ComputeWallV` from scratch instead of asking `BuildDerivedWallSpan`, so the shift could be put into
the shipping code and the number would not move -- which is exactly what happened, twice, before
anyone noticed the number was about something else. And the delta classifier measured against the
unshifted texture top, so it kept reporting "sixteen walls have the peg flag inverted" about an answer
the derivation had stopped giving.

With the ladder asking the derivation and the classifier asking about the answer that ships: the
shift goes in unconditionally, every clamping fault across five maps goes to zero, and the peg class
goes to zero on every map. There was never a peg rule to find.

**Hypotheses killed by measurement**, each of which looked right: the piece belonging to the other
sidedef (0 pieces, on every map); a texture taller than its span pegging the other way (perfect
correlation over the failures, and applying it halves the score -- the correlation was selection
bias, since pegging can only differ where the texture does not fill the span); the peg flag being
inverted on a subset, which the delta measurement retired outright; and Sunder MAP16's "fence", which
turned out not to be about pegging at all but about which plane clips a two-sided middle.

Two switches that existed so those rules could be A/B'd rather than argued about are gone.
`fua_surface_pegrule` was deleted -- a rule that halves the score is not a candidate any more, and a
switch nobody should ever turn on is worse than a paragraph. `fua_surface_vshift` survives with its
meaning changed: it is now the real `CheckTexturePosition`, default **on**, kept switchable because
it moves pieces in both directions and one run should be able to say so.

The lesson worth keeping is not about pegging. It is that a ladder which transcribes the code it
measures will agree with itself forever, and the two places this one did were the two places the
answer had been sitting.

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

`fua_surface_mapbake_auto` does it at level load, and the frame is what GL's own bake produces --
0.0% on Doom 2 MAP01 and dbab04. Off by default while the traversal is still what fills in the kinds
the derivation does not do.

`fua_surface_mapcover` is what said this was possible before it was written, and it is the number to
watch: on dbab04 the map accounts for 1332 of the 1336 parts GL draws; on Sunder MAP16, 59,477 of
59,483 across 52,052 segs. Two categories had to be found to get there, and both were in
`GLWall::Process` rather than in the map:

- **a sloped wall is drawn if EITHER end has area**, not both. `if (topleft<=bottomleft &&
  topright<=bottomright) return;` is the whole of GL's test, and requiring both ends left every wall
  that pinches out at one end unaccounted for.
- **a sloped step with no texture on the sidedef is drawn with the SECTOR'S FLAT** -- "with a
  background sky there are ugly holes otherwise". That one category was 131 parts on dbab04.

MAP16's residual is gone: it was the two-sided middle clip, and MAP16's geometry ladder now reads
100.0% where it read 93.9%. Coverage on dbab01, dbab02 and dbab04 is exact in both directions -- no
part GL draws that the map does not claim, and none the map claims that GL does not draw.

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

## Driving the bake from the map, and what still stands in the way

`fua_surface_mapbake_auto` builds every wall in the level from the map with no `GLWall` involved. It
is **off** by default, and the reason is a number: the frame it produces differs from the GL-driven
one by 0.5% on dbab01, 1.1% on dbab02 and 0.4% on dbab04 -- small, localised panels rather than
anything systemic, but not the 0.0% the derivation itself reads when the capture is driving.

Two things it took to get there, both of which were holes rather than inaccuracies:

**Ownership is per SEG, not per level.** `BuildDerivedWallLight` declines one sector kind -- a sector
with a 3D floor light list has no single light level, because `SplitWall` cuts the wall at every band
and gives each fragment that band's own light. Declining is right. Reading the refusal as "there is
no wall" was not: `BakeSegFromMap` squashed all three parts of every seg in such a sector, and on
dbab01 an entire brick wall went missing and the lava room behind it came through -- 10.5% of the
frame. A seg the map cannot light stays with the capture, and the two paths divide the level between
them instead of one leaving holes in it.

**`fua_surface_mapcover` was only asking half its question.** It skipped segs GL never drew, which
makes "the map accounts for everything GL drew" true by construction and says nothing about what the
map draws that GL does not -- the half that was putting a wall through another wall. It counts both
now.

## What comes next, in order

1. **The map-driven bake's last percent, which is now identified: coplanar overlap.** Not a
   derivation fault. `fua_surface_bakediff` compares the whole quad -- material, all four corners,
   light, colormap -- for every captured piece against what the derivation builds for the same seg
   and part, and every input agrees except 13 pieces across three maps. What differs is that the map
   bake builds the WHOLE LEVEL where the capture only ever built what GL walked, and the extra
   surfaces overlap the ones that were already there:

   | dbab02 | pieces duplicating another piece's geometry | coplanar overlapping pairs |
   |---|---|---|
   | GL-driven | 74 of 3454 (2.1%) | 1807 |
   | map-driven | 376 of 3489 (10.8%) | 1927 |

   Coplanar quads with different vertices do not agree on depth to the last bit, so the rasteriser
   stipples between them -- which is what `fua_dg_cull` was added for, and its note names a rock face
   on dbab02 as the symptom. That is the same rock face still showing the difference. Backface
   culling removes the pairs whose facings differ; these 120 are the remainder. The fix is the one
   that note already names: normalise winding at bake time and stop building a second quad where one
   already covers the wall.

   **Ruled out by measurement, each of which looked plausible:** material identity (animation frames
   read as faults until the probe learned better -- 171 of them on dbab02, all nukage flowing);
   colormap; light level and fake contrast; the horizontal and vertical coordinates and the height on
   every compared surface; viewer-substituted sectors; segs the map bake does not own; sector
   movement between load and capture; dynamic lights; `gl_seamless`; incomplete capture coverage
   (`fua_levelmesh_bakeall` first, no change); and reload noise, whose floor is 0.0%.

2. **Special surface kinds** -- 3D floor faces, skies, horizons. These never reach the mesh from
   either path (`RecordPiece` is only called for the plain wall lists), so this is about drawing them
   at all, not about the two bakes disagreeing.

3. **Retiring `GLWall`/`GLFlat` and collapsing `wallcache.cpp` and `flatmesh.cpp` into one cache**,
   which is the point of all of it: one surface type, derived once, instead of two transcriptions of
   what GL does that drift apart every time a feature is added to one of them.

`GLWall::Process` is a thousand lines of accumulated cases. It gets replaced one answerable question
at a time, and every question keeps its answer in a test -- not by a rewrite that has to be right
everywhere before it can be run once.

## Where the map-driven bake stands now

`fua_dg_standalone` and `fua_surface_mapbake_auto` are **on by default**. The two things holding them
off were the residual against the GL-driven picture and the surface kinds only the BSP walk built;
what closed them:

- **3D floor wall faces** (`ffblocks_compute`). A slab hanging in the sector on the other side of a
  line cuts a block out of the wall behind it, and `GLWall::DoFFloorBlocks` builds those during the
  traversal -- so when the traversal stops they simply vanish. That was the whole of dbab02's 4.0%.
  The walk is nine tests' worth of rules: clip each slab to what the last one left, skip one entirely
  above the wall, skip inverted ones (they belong to the front sector's pass), stop at the wall's
  bottom.
- **The span they are cut from is the opening**, not the middle texture's. Asking
  `BuildDerivedWallSpan` for the middle read tidily and gated the whole thing on the sidedef having a
  midtexture, which most two-sided lines with a slab behind them do not: 32 sectors of 3D floors
  produced three faces. `DoFFloorBlocks` takes the gap between the planes -- the lower of the two
  ceilings, the higher of the two floors, each decided at both ends together.
- **No upper between two sky ceilings.** GL wraps its entire upper-texture branch in
  `if (front != sky || back != sky)`. Deriving one anyway laid a lump of the sidedef's top texture
  across dbab02's horizon: geometry and lighting both correct, and simply not a surface that exists.

### The picture, each map against its own reload noise floor

Settled world, monsters off, console cleared. A map only counts as differing when it beats the floor
the GL-driven path sets for itself on the same shot.

| map | GL-driven vs map-driven | GL-driven vs itself | verdict |
|---|---|---|---|
| dbab01 | 0.1% | 0.1% | no difference |
| dbab02 | **0.0%** | 0.0% | was 4.0% |
| dbab03 | 0.0% | 0.0% | no difference |
| dbab04 | 0.6% | 0.1% | **one wall band, open** |
| dbab05 | 0.0% | 0.0% | no difference |
| Sunder MAP10 | 0.0% | -- | no difference |
| Sunder MAP16 | 0.8% | 0.0-0.1% | **real, open** -- see below |
| Sunder MAP04 | 0.2% | -- | at the floor |

### Sunder MAP16, 0.8%

Recorded first as "no difference", on a floor reading that was itself unstable. Measured again with
three loads per config it is consistent: both paths repeat within 0.0-0.1% of themselves and differ
from each other by 0.8% every time. It is a patch of animated lava and a glowing object, which points
at an animation frame resolving differently between the capture and the bake rather than at geometry.
Not chased yet.

### The other open difference: dbab04's tinted band

One horizontal band on one wall comes out grey where GL tints it blue. Its geometry, its texture and
its extents are all already right; only the colour is wrong, which is the class of fault that
survives every check that compares shapes -- `fua_surface_bakediff` reports **0 material, 0 light and
0 colormap** differences across 1336 compared pieces, because whatever draws the band is not among
the pieces being compared.

Ruled out by measurement, in order:

- the light bands the front sector's 3D floor light list casts (implemented for the faces, band
  looked up per block the way `SplitWall` cuts one -- no change to the picture);
- a fog slab, which `BuildFFBlock` turns into an untextured translucent panel of the light's fade
  colour laid over the wall behind it. The bake used to skip these outright, on the reasoning that an
  untextured face is nothing for the mesh to hold -- which is false, the capture path registers
  exactly such a piece with a null material. They are emitted now. It did not move the band, so no
  map measured here appears to contain one, and that path is correspondingly unexercised;
- reload noise, whose floor on this map is 0.1%;
- the player's weapon sprite, which is absent from the GL-driven capture on this map at any time and
  present in the standalone one -- a pre-existing gap in the older path, in the direction of the new
  one being right, and worth 2.6% of the raw figure until it was excluded.

`ClipFFloors` -- the clip GL applies when a slab is `FF_SWIMMABLE|FF_TRANSLUCENT`, against the *front*
sector's slabs of the same kind -- is the next thing to try, and is not implemented.

### `fua_dg_cullbatches`, removed

It was worth nothing and it is gone. Measured twice, most recently on Sunder MAP10 with the level
baked: submit-only 0.20-0.24 ms with it either way, four alternating runs, no separation. The reason
is the one recorded when it was written -- a batch is a run of pieces sharing a material, those are
scattered the length of the level, and every batch's box is most of the map. It becomes worth having
again only when batches are spatially coherent, which is what per-piece indirect draws would give,
and at that point the test is three lines.

### What the switches are worth

Sunder MAP10 at spawn, monsters live, the engine's own `stat rendertimes`:

| | walls built per frame | `All=` |
|---|---|---|
| GL-driven | 8354 | 5.65-8.39 ms |
| standalone + map bake | 0 | 0.66-0.75 ms |
