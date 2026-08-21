# Why an old game is costing milliseconds, and what to do about it

Measured at 1920x1200 on the Vulkan backend, with `fua_render_in_background 1` so the window can be
out of focus without the numbers being a lie.

**A correction, first, because it invalidates a set of numbers.** The first pass at this was measured
with `--file .../Sunder.wad`, and the file on disk is `sunder_2512.wad`. The launcher does not fail
on a missing pwad -- it loads the IWAD and carries on -- so every figure originally filed under
"Sunder MAP10" and "Sunder MAP16" was **base Doom 2**, on maps a fraction of the size. The tell was
sitting in the stats the whole time: 126 batches and 4 sprites, against a map whose own baseline note
records 8251 walls a frame. Sunder numbers below are the real ones.

## The frame, taken apart

At a light view on dbab01 -- 86 sprites, 74 world batches -- the frame is 2.11 ms, and **nothing in
the backend accounts for it**:

| switch off | p50 |
|---|---|
| baseline, everything on | 2.11 ms |
| dynamic lights | 2.13 ms |
| sky | 2.06 ms |
| 2D / HUD | 2.08 ms |
| light clusters | 2.04 ms |

Turning off whole subsystems changes the frame by less than the noise. So the cost is not in what the
backend draws. It is in what happens before it draws.

## The missing step: we pay for both renderers, every frame

`fua_dg_standalone 1` stops GL deriving the scene -- the BSP walk, a `GLWall` per visible seg, the
draw lists -- for a picture the Vulkan backend renders from geometry that is already resident.

On **Sunder MAP10 at spawn** -- 1008 sprites, 55 batches -- alternating runs, twice each:

| | p50 | fps | render |
|---|---|---|---|
| GL still deriving (default) | 9.99 ms | 100 | 9.66 ms |
| GL cut out | **3.36 ms** | **298** | 4.78 ms |

**Two thirds of the frame, 6.6 ms, is GL working out a picture nobody looks at.** It lands inside the
measured render time because GL's walk happens in `D_Display`, after the profiler's render mark.

The same test on dbab01 at a light view saves 1.3 ms of 2.11; on Doom 2 MAP10, 1.3 ms of 2.1. The
saving tracks how much GL has to walk, so it grows with exactly the maps that need it.

And the picture is already right: standalone against default at the same camera differs by **0.1%**
on Sunder MAP10 and 0.3% on dbab01, sprites present in both. The old note listing sprites as a
blocker is stale.

Sunder MAP10 is also not primarily fill bound, which is worth knowing because dbab01's sprite-heavy
view is: a ninth of the pixels takes it from 9.6 ms to 8.3, about 13%. The cost there is CPU.

## What actually blocks turning it on

One thing, and it is this session's own doing.

`fua_surface_mapbake_auto 1` + `fua_dg_standalone 1` renders **30.7% wrong**, drawing 49 batches
where the default draws 74. The cause is the ownership split: a seg whose sector has a 3D floor light
list cannot be lit from the map -- `SplitWall` cuts that wall into bands, each with its own light --
so it is deliberately left to the capture. Under standalone there is no capture, so those walls are
simply absent.

That is the whole of it. Standalone needs the map bake to carry 100% of the level, and the map bake
currently carries all of it except the sectors it hands back.

## The scope

1. **Light a 3D-floor sector's wall from the map.** The band boundaries are the sector's light list,
   which is map data -- the derivation declines it today because deriving ONE light level for the
   whole wall would be wrong, not because the bands are unknowable. Split the derived wall at each
   band and give each fragment that band's light and colormap, which is what `SplitWall` does. This
   is the only thing between standalone and being correct, and on Sunder MAP10 it is worth the 6.6 ms
   above -- 100 fps to 298.
2. **Close the map bake's own residual**, 0.4-1.1% depending on map, which is coplanar overlap:
   the map bake builds the whole level where the capture only built what GL walked, and the extra
   surfaces stipple against the ones already there. Counted: dbab02 goes from 74 duplicate pieces and
   1807 coplanar pairs to 376 and 1927.
3. **Then turn `fua_dg_standalone` on by default**, which is the point of the other two.

## What is NOT worth doing, measured

**Batching the sprite draws.** "1234 sprites, 1234 draw calls" reads like the whole problem. Reusing
the sorted pass's binding and collapsing adjacent draws took a 786-sprite frame from 786 submissions
and 786 descriptor binds to 95 and 21, and moved the frame time by nothing over four alternating
runs. The same scene at a ninth of the pixels went 30.98 ms to 7.98 ms. That pass is fill bound.

What is left there is overdraw -- sprites are drawn back-to-front with depth writes off, so every
overlapping layer is shaded in full -- and it is a smaller, more dangerous lever than the one above:
depth writes on the alpha-tested half is exactly the change that once punched black holes in plasma
bursts. The quads are already trimmed to their opaque borders by `FMaterial::TrimBorders`.

## The short version

The game is not slow because it is old. It is slow because it is being drawn twice: once by GL, to
work out a picture nobody looks at, and once by the backend, from geometry it already had. On Sunder
MAP10 that is two thirds of the frame.
