# Sprites: what deriving them would cost, and why not to

In-game sprites -- the billboards Doom draws for actors: monsters, items, decorations, projectiles,
blood, puffs. Not the HUD; that is the 2D layer, captured separately and part of a different debt.

Sprites are the last thing GL still derives for the Vulkan backend. `GLSprite::Process` picks what a
sprite looks like and `GLSprite::Draw` emits it, and the mesh takes what they produce -- which is why
GL still runs at all once `fua_dg_standalone` has taken the world away from it.

## A correction that changes the size of the prize

`sv_nomonsters 1` does **not** remove sprites. On Sunder MAP10's arena it takes the frame from 1380
sprites to 1074 -- the items and decorations stay. Every "with monsters / without monsters" comparison
earlier in this work is therefore a comparison of 1380 against 1074, a 22% difference in count, and
not sprites against none.

That matters because "the remaining second of the frame is the GL sprite pipeline" was written on the
back of exactly that comparison, and it is wrong.

## What it actually costs

The engine's own clocks, `stat rendertimes`, on Sunder MAP10's arena:

| sprites | GLSprite render | GLSprite setup | total |
|---|---|---|---|
| 1380 | 0.167 ms | 0.126 ms | **0.29 ms** |
| 1074 | 0.149 ms | 0.337 ms | 0.49 ms |

Against a frame of 1.5-2.1 ms depending on what is alive, that is **15-25%** -- and it is the whole of
what a derivation would replace. The setup figure is noisy between runs; the render figure is not.

## What `GLSprite::Process` does, which is what would have to be rewritten

- Visibility: the camera actor, `IsVisibleToPlayer`, `RF_INVISIBLE`, `RenderStyle.IsVisible`, the map
  section bitmask, and the spy-icon / chasecam / mirror rules.
- `P_CheckPlayerSprite` for player sprites, which can rewrite the sprite number and both scales.
- Frame and angle selection: `R_PointToAngle` against the actor's own angle, then `gl_GetSpriteFrame`,
  which also reports whether the frame is MIRRORED -- and a mirrored frame flips its x offset.
- The sprite rectangle, already trimmed to the opaque border by `FMaterial::TrimBorders`.
- `PerformSpriteClipAdjustment` -- a hundred lines of clipping a sprite against the floor and ceiling
  it stands between.
- Wall sprites versus face sprites, which orient differently.
- Lighting: fullbright, fog level, `gl_CheckSpriteGlow`, the sector's colormap, a fixed colormap, the
  actor's `fillcolor` under `STYLEF_ColorIsFixed`, and `STYLEF_InvertSource`.
- `SplitSprite`, which cuts a sprite at a 3D floor's light bands the way `SplitWall` cuts a wall.
- Particles and voxels, each with their own path.

## The three options

1. **Narrow the sweep.** The standalone path iterates every actor in the level each frame and frustum
   culls them. That is already far cheaper than the BSP walk it replaced, but it is still O(all
   actors) where only the visible ones matter. A spatial index over visible sectors would cut it.
   *Unmeasured: how many actors exist against how many draw. Measure that before building anything.*
2. **Cache per-actor state between frames.** Limited by construction: the chosen frame depends on the
   angle between the actor and the camera, and the camera moves every frame.
3. **Derive sprites from the map.** Replaces the list above.

## The recommendation: don't do 3

15-25% of the frame, at 400+ fps, in exchange for a second implementation of every render style a mod
can set -- which is the specific thing that drifted before `CaptureShading` existed, and the specific
thing this port has refused to do since. The list above is not long because sprites are complicated;
it is long because Doom's sprites accumulated thirty years of special cases, and each one is a bug
that only shows up on somebody's favourite wad.

## Two better targets, found while scoping this

**`glFinish()` in the swap path.** `OpenGLFrameBuffer::Swap` calls it before presenting -- a full GPU
stall every frame, waiting on a GL queue that has almost nothing in it when the backend drew the
world. `NtWaitForSingleObject` is 9-13% of the profile.

**The instant-replay capture owns the frame-time TAIL.** `cl_fua_replay` is on by default and reads
the frame back through a PBO at 30 fps. On the median it costs nothing -- 1.515 ms against 1.505 --
but the 99th percentile goes from **4.14 ms to 2.69** and the worst frame from 5.39 to 2.71. At 400+
fps that is one frame in fourteen costing several times its neighbours, which is what a stutter is.
