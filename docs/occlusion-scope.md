# Occlusion for sprites: built, measured, and why it does not pay

The standalone renderer draws the world from a resident mesh and never walks the BSP. That dropped
two jobs, not one: the walk built the geometry, and it also *found the actors worth looking at* --
it only ever descended into subsectors that survived angular clipping, so GL never saw an actor
behind a wall.

The sweep that replaced it has no equivalent. Sprites are 52-74% of what is left of the frame (see
`sprites-scope.md`) and every micro-optimisation of the per-sprite path came back a wash, so this
was the only lever left. It was built. It works. It is also a wash, and this is the record of why --
because the reason is the same one that sank the other three attempts, and it is worth stating once
in a form that stops it being tried again.

## What it is worth, in sprites

The BSP walk's own answer is a real occluder that ships, so its count is a reference. Running both
paths on the same camera and reading `Sprites:` out of `stat rendertimes`:

| map | the BSP walk sees | the sweep sees | occlusion could remove |
|---|---|---|---|
| Sunder MAP10 (open arena) | 1258 | 1406 | 11% |
| Sunder MAP16 | 752 | 3053 | 75% |
| Sunder MAP04 | 20 | 246 | 92% |

Worth almost nothing in an open arena, worth most of the sprite pipeline in a built-up map.

## The obvious answer, ruled out first

A **marking-only BSP traversal** -- keep the descent and the clipper, drop `GLWall::Process`, mark
the subsectors that survive -- reuses tested code and cannot change the picture.

It cannot pay for itself. `stat rendertimes` clocks the traversal separately from the wall building
and the parts add up (`All=8.386` with `Render=3.403, Setup=3.322, BSP=0.746`), so `BSP=` *is* the
marking-pass cost: 0.564 ms on MAP10 to save 0.03, 1.374 on MAP16 to save 0.54. It loses 19x and
2.5x. It only wins on MAP04, where the traversal is nearly free for the same reason its cull is
nearly total -- a BSP walk costs what it *fails* to cull.

## So: an angular buffer, per actor

`features/hwrender/occlusion.h`. A 1-D buffer over angle, 2048 buckets, filled from the biggest
blockers in the level and asked one question per actor. Four things had to be got right, and each
was got wrong first in a way worth keeping:

1. **Height cannot be ignored.** The first version was pure angle, like Doom's solidsegs. Doom got
   away with that because the walk only added a clip range for a wall it had actually reached, front
   to back, which made it both in view and full height. Choosing occluders off the map loses both
   guarantees: it occluded with the walls of 3D floor control sectors parked out in the void, and
   with basements the sight line passes clean over. It took the torches off the towers of MAP16.

2. **The vertical test is projective, not a world-height comparison.** Asking "is the actor between
   this wall's floor and its ceiling" answers no almost everywhere in a map built of terraces: it
   culled 5% where the honest answer was 85%. What decides the question is how much of the *view* a
   limit covers, so both reduce to slope from the eye -- height over distance.

3. **Two limits per bucket, not one wall.** Doom clipped each column against a floor limit and a
   ceiling limit, and that decomposition is right here for the same reason: a step hides everything
   below a line, a lintel everything above one. One band per bucket expresses neither. Restricting
   occluders to one-sided walls has the same flaw from the other end -- in Sunder the buildings are
   made of two-sided lines, and their steps are most of the occlusion there is.

4. **Keep the blocker that hides more, measured as slope.** Keeping the *tallest* is the obvious rule
   and it is wrong in a way that shows up as the curve running backwards: adding blockers reduced
   what was culled, because a tall far wall beats a low near step on height while hiding almost
   nothing, and once it owns the bucket the distance test spares everything nearer than it.

Two more things made it affordable enough to measure at all: the lines are ranked once at load by
length times blocking height (walking all 56,000 of MAP16's lines every frame costs 8.7 ms), and
painting is order-independent so there is nothing to sort -- the sort was 54 ms a frame on its own.

**It is exact.** Pixel-identical on Sunder MAP04/10/16/20 and dbab01-05, with the world frozen and
monsters live, while hiding up to 96% of the actors the sweep finds.

## And it is a wash

Sunder MAP16, minimum of 28 alternating samples in one instance:

| | Draw / RegisterSprite | the sweep | total |
|---|---|---|---|
| off | 0.314 ms | 0.310 ms | **0.630 ms** |
| on | 0.177 ms | 0.453 ms | **0.630 ms** |

The culling does exactly what it is supposed to: `Draw` and `RegisterSprite` fall by 44%, in line
with the actors removed. The sweep rises by the same amount -- the buffer's build, plus one test per
actor. MAP20 behaves the same way.

Earlier runs of the whole frame appeared to show 25-30% wins. They were noise: this machine's
frame-to-frame spread is larger than the effect, and only the engine's own clocks, alternated inside
one instance over dozens of samples, separate them.

## Why, and what that rules out

**A drawn sprite costs about 0.2 microseconds, all in.** A test that decides whether to skip one
cannot cost a comparable amount, and a square root, an angle and a bucket lookup do. The build is
only a third of the added cost, so choosing better occluders does not rescue it -- and neither does
choosing fewer, which was the recommendation this document used to carry.

That is the same wall the other three attempts hit, and it is worth naming: **the per-sprite pipeline
is already too cheap for a per-actor decision to be worth making.** Anything that costs tens of
nanoseconds per actor is competing with something that costs hundreds.

So the surviving design has to be one of:

- **A test of about five nanoseconds.** That means one array lookup and nothing else, which means
  visibility precomputed against something the actor already knows -- its sector.
  `visible[viewSector][actorSector]`, one bit, built when the level is baked. The mesh is already
  built once at load; this is the same move applied to visibility. The open questions are the build
  cost on a map of Sunder's size, and how much is given up by having to assume every door open.
- **A decision that is not per actor at all.** Reject whole groups: iterate `sector_t::thinglist`
  and skip an entire sector's actors on one test. Worth a count first -- on these maps sectors do not
  outnumber actors by nearly enough for that alone to be the answer.
- **Not culling on the CPU.** The backend already draws the world into a depth buffer; a pyramid over
  it, tested per actor on the GPU, moves the cost off the critical path entirely. Blocked behind the
  GL-free sprite port, and the strongest answer once that lands.

## What is left in the tree: nothing

The occluder was removed once it had answered the question. It was correct and it was measured, and
both of those live here rather than in code nobody runs -- a switch defaulted to off is still
something the next person has to read, decide about, and keep compiling.

It is recoverable in one command if the baked-visibility work wants something to check itself
against, which is the only use it had left:

    git show 10b5015a -- src/zandronum/src/features/hwrender/occlusion.cpp

`fua_sprite_sweep` stays, because the funnel it reports -- actors iterated, culled, reaching
`Process`, drawing -- is the measurement any future attempt starts from.
