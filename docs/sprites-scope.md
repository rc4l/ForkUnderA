# Sprites: what they actually cost, and what does not help

In-game sprites -- the billboards Doom draws for actors: monsters, items, decorations, projectiles,
blood, puffs. Not the HUD; that is the 2D layer, captured separately.

With `fua_dg_standalone` and `fua_surface_mapbake_auto` on, the world comes from a resident mesh and
GL builds nothing per frame. Sprites are what is left. `GLSprite::Process` picks what a sprite looks
like and the mesh takes what it produces, which is why GL still runs at all.

## A correction that changes the size of the prize

`sv_nomonsters 1` does **not** remove sprites. On Sunder MAP10's arena it takes the frame from 1380
sprites to 1074 -- the items and decorations stay. Every "with monsters / without monsters" comparison
earlier in this work is therefore 1380 against 1074, and not sprites against none.

## What it actually costs

This document previously said 15-25%, measured against a frame that still had GL building walls in
it. Measured properly -- by capping the sweep with `fua_sprite_max 1`, which leaves the frame with no
sprites in it and therefore prices the whole pipeline -- sprites are most of what is left:

| | `All=` with sprites | `All=` without | sprites are |
|---|---|---|---|
| Sunder MAP10 | 0.830 ms | 0.403 ms | **52%** |
| Sunder MAP16 | 2.391 ms | 0.610 ms | **74%** |

Minimum of many samples, because the median swings by 2x on this machine and comparing across engine
restarts is worthless. Everything below is a within-one-instance A/B, alternating.

## The funnel, and where it leaks

`fua_sprite_sweep` reports it. The standalone path walks the thinker list rather than the BSP tree,
so it starts from every actor in the level:

| | actors iterated | culled before `Process` | reach `Process` | actually draw |
|---|---|---|---|---|
| MAP10 | 3834 | 2428 | 1406 | **1406** |
| MAP16 | 6428 | 1822 | 4606 | 3053 |

MAP10 runs at a perfect hit rate. MAP16 loses 1553 a frame inside `Process`, and `fua_sprite_sweep`
names the reason: **all of them fail the map section bitmask**.

That test is worth reading carefully, because its comment in `GLSprite::Process` reads as though the
BSP walk fills the mask in -- which would make it meaningless without a walk. It does not.
`FGLRenderer::ProcessScene` clears the mask and sets the VIEWER's section every frame before drawing
anything, and portal traversal is the only thing that ever adds to it. So the question is asked and
answered identically with a walk or without one.

## Three things that were tried and did not help

Each was built, measured with an alternating A/B in one instance, and then judged.

1. **Narrow the sweep** -- hoist the cheap half of `Process`'s rejection ahead of the projection, so
   an actor that can never draw is not projected and not passed on. This is what the previous version
   of this document recommended. It **does not separate from noise** on either map. The cost is not
   in the actors that get rejected; it is per-actor-that-draws. The extraction was kept anyway
   (`GLSprite::CanPossiblyDraw`) because it is the same code `Process` runs rather than a copy of it,
   and it is the first piece of the sprite path that is not tangled in the renderer.

2. **Memoise `CaptureShading`.** It goes through `gl_SetColor` and `gl_SetFog` -- which write the
   whole render state -- and then reads eight values back, once per sprite per frame. Keyed on its
   six inputs it hits **98%**: 3053 sprites on MAP16 come from 31 distinct lighting situations. It
   still buys nothing. What a hit skips is two calls and eight getters, and writing the eight answers
   into the piece costs about the same. Removed.

   It is worth recording how that was nearly missed. The first version cached a whole `MeshPiece` and
   assigned it over the caller's, which clobbered every field the caller had already set and that
   `CaptureShading` does not touch -- chief among them `mp.material`, so every sprite sharing a cache
   slot wore the texture of whichever sprite reached that slot first. That version measured as a 10%
   win. It was only ever the bug being cheap, and it showed up against the picture at 1.7% on MAP16
   and nowhere else.

3. **`DynAppend` pushed six vertices one at a time** and took its `MeshPiece` by value -- eighteen
   thousand bounds tests and six thousand struct copies a frame on MAP16, to append blocks that are
   always six long. One grow and one `memcpy` now, and the proto passed by reference. Strictly less
   work with identical semantics, and it did not separate from noise either.

## Where the time actually is

Splitting MAP16's 0.86 ms of sprite work by knob, minimum of many samples:

| piece | cost | share |
|---|---|---|
| `RegisterSprite` -- building the quad and feeding the mesh | ~0.28 ms | 36% |
| `gl_SetDynSpriteLight` -- the per-actor dynamic light | ~0.16 ms | 21% |
| the rest of `Process` -- frame choice, clipping, projection | ~0.33 ms | 43% |

There is no hot spot. It is roughly a quarter of a microsecond per drawn sprite, spread thinly across
a lot of small work, which is why every micro-optimisation above came back a wash.

## So the only lever left is drawing fewer

The BSP walk was doing two jobs and only one of them was dropped: it derived the geometry, and it
found the actors worth looking at. It only ever visited subsectors that were *visible*, so GL only
ever saw actors that were not behind a wall. The sweep has no equivalent -- an actor in the camera's
map section and inside the frustum is processed whether or not there is a wall in front of it, and on
a map like MAP16 that is most of the 3053.

The candidates, none of them small:

- **A marking-only BSP traversal.** The walk's expense was building a `GLWall` per seg, not the
  descent; a traversal that only marks visible subsectors would give exact sprite visibility. It
  costs whatever `BSP=` costs, which was 0.6-0.75 ms on MAP10 -- possibly more than it saves, and it
  brings back the thing that was just removed. Measure the marking pass on its own before believing
  either way.
- **A software occlusion buffer** over the resident mesh. The general answer, and much the largest.
- **Screen-space size culling.** Cheap, effective, and it changes the picture -- which this port has
  not been willing to do.

## The other half: a GL-free build

`GLSprite::Process` touches no GL state -- upstream reached the same conclusion and its
`hw_sprites.cpp` is API-agnostic -- so the port of sprites is not a rewrite of the hard part, it is
severing the easy one. `CanPossiblyDraw` is the first piece over that line. The rest still reaches
through `GLRenderer->mViewActor`, `GLRenderer->mCurrentPortal` and the GL draw lists, and that is the
work item -- not a second implementation of every render style a mod can set.
