# Occlusion for sprites: what it is worth, and what it rules out

The standalone renderer draws the world from a resident mesh and never walks the BSP. That dropped
two jobs, not one: the walk built the geometry, and it also *found the actors worth looking at* --
it only ever descended into subsectors that survived angular clipping, so GL only ever saw actors
that were not behind a wall.

The sweep that replaced it has no equivalent. An actor in the camera's map section and inside the
frustum is processed whether or not there is a wall in front of it. Sprites are 52-74% of what is
left of the frame (see `sprites-scope.md`), and every micro-optimisation of the per-sprite path came
back a wash, so this is the only lever left.

## What it is worth

The BSP walk's own answer is the target, because it is a real occluder that ships and is known
correct. Running both paths on the same camera and reading `Sprites:` out of `stat rendertimes`:

| map | the BSP walk sees | the sweep sees | occlusion could remove |
|---|---|---|---|
| Sunder MAP10 (open arena) | 1258 | 1406 | **11%** |
| Sunder MAP16 | 752 | 3053 | **75%** |
| Sunder MAP04 | 20 | 246 | **92%** |

It is worth almost nothing in an open arena, where everything really is visible, and it is worth
most of the sprite pipeline in a built-up map. MAP10 is the benchmark this port has been tuned
against, and it is the map occlusion helps least.

## The measurement that rules out the obvious answer

The obvious answer is a **marking-only BSP traversal**: keep the descent and the clipper, drop
`GLWall::Process`, and mark the subsectors that survive. It reuses tested code, it gives the numbers
above by construction, and it cannot change the picture because it culls exactly what GL culls.

It also cannot pay for itself. `stat rendertimes` clocks the traversal separately from the wall
building -- `All=` on Sunder MAP10 is 8.386 with `Render=3.403, Setup=3.322, BSP=0.746`, and those
add up, so `BSP=` is the descent and the clipping and nothing else. That is precisely what a marking
pass would cost:

| map | a marking pass costs (`BSP=`) | the sprites it saves | verdict |
|---|---|---|---|
| MAP10 | 0.564 ms | 0.250 x 11% = 0.03 ms | **loses, 19x** |
| MAP16 | 1.374 ms | 0.726 x 75% = 0.54 ms | **loses, 2.5x** |
| MAP04 | 0.007 ms | 0.060 x 92% = 0.055 ms | wins, by 0.05 ms |

MAP04's traversal is nearly free for the same reason its cull is nearly total: a BSP walk costs what
it *fails* to cull. That is a pleasant property and it is not enough. On the two maps where sprites
actually cost something, re-adding the walk costs more than the sprites it removes.

## The constraint that follows

The sweep is fast because it is O(actors). The walk is slow because it is O(visible geometry), and on
these maps geometry outnumbers actors by a lot -- Sunder MAP10 builds 8354 walls a frame against 3834
actors iterated and 1406 sprites drawn.

So: **any scheme that pays per-geometry per-frame has already lost.** What can win is a per-actor
test against a structure that was either built once, or built by the GPU as a side effect of drawing
the world it was going to draw anyway.

That rules out, without further measurement: the marking-only traversal above, a per-frame portal
flood fill over the sector graph, and rebuilding a solidseg buffer from every seg each frame.

## The three candidates that satisfy it

### 1. Visibility baked with the mesh

The level mesh is already built once at load and is static. Sector-to-sector visibility could be
built at the same time, and the per-frame cost becomes a bit test: `visible[viewSector][actorSector]`.

- **For it:** zero per-frame geometry cost, which is the whole constraint. Fits the architecture --
  this port's central move has been to compute at bake time what the engine recomputed per frame.
- **Against it:** doors, lifts and moving sectors change what is visible, so a static answer has to
  assume every door open and is therefore conservative -- and the maps where occlusion is worth most
  are the built-up ones full of doors. Build cost on a map of Sunder's size needs measuring before
  anything else; a wrong-shaped algorithm here is minutes at load, not milliseconds.
- **Not to be confused with** Doom's REJECT lump, which is sector line-of-sight for monster AI, is
  frequently unbuilt or all zeroes, and is conservative in the wrong direction for this.

### 2. Hi-Z from the depth buffer we already draw

The backend draws the world into a depth buffer before sprites. A depth pyramid over it, tested per
actor bounding box, is exactly the per-actor test the constraint asks for, and the occluder is the
real geometry rather than an approximation of it.

- **For it:** no CPU rasterisation, no bake, exact occluders, and it costs one downsample of a buffer
  that already exists.
- **Against it:** reading it back on the CPU is a stall, so the test wants to be on the GPU -- which
  means the sprite list has to be GPU-side, which is the `GLSprite::Process` port that has not
  happened. Using the *previous* frame's pyramid avoids the stall at the price of one frame of
  latency, which shows as a sprite popping in when the player steps around a corner. Padding the test
  box hides most of that.
- **Sequencing:** this is the strongest long-term answer and it is blocked behind the GL-free sprite
  port, so it is not the next thing.

### 3. A few big occluders, not all of them

Doom's original visibility was a 1D angular buffer, and it worked because Doom's occlusion is largely
a 2D problem. The walk is expensive because it feeds that buffer from *every* seg. Feeding it from
only the largest occluders near the camera -- a few dozen, chosen from the resident mesh -- makes the
build O(chosen) instead of O(visible), and the test stays O(actors).

- **For it:** self-contained, no latency, no bake, and the cost is a knob rather than a property of
  the map.
- **Against it:** it is an approximation, so it must be conservative -- cull only when an actor is
  definitely hidden -- and a conservative angular test in a map with 3D floors and slopes will give
  back some of the 75%. How much is unknown and is the thing to measure.
- **Choosing the occluders** is the real design question: biggest solid one-sided walls within some
  radius, re-chosen when the camera moves far enough.

## Recommendation, and the next measurement

Candidate 3 is the one to prototype, because it is the only one that is neither blocked on other work
nor at risk of a load-time surprise, and because its own failure mode is measurable early: build the
angular buffer from the N largest nearby occluders and count how many of MAP16's 3053 sprites it
rejects. If N=32 gets most of the way to 752, it is worth building properly. If it needs N in the
hundreds to beat half, it has become O(geometry) again and it has lost for the same reason the
marking pass lost.

That count is cheap to get and needs no rendering changes at all -- `fua_sprite_sweep` already
reports the funnel, and a rejection counter next to it answers the question before any of the culling
is wired to anything.

Two things worth holding onto while doing it:

- **The prize is map-shaped.** Anything measured on MAP10 will look worthless and anything measured on
  MAP04 will look free. MAP16 is the case that decides it.
- **Over-culling is visible and cheap to catch.** This only removes sprites, so a mistake shows up
  directly in a picture diff against the GL-driven render, and the sprite count has an exact target
  to hit: 752 on MAP16, 1258 on MAP10, 20 on MAP04.
