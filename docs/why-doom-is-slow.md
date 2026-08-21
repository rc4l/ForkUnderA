# Why an old game is costing milliseconds, and what to do about it

Measured on dbab01 and Sunder MAP16, 1920x1200, Vulkan backend, `fua_render_in_background 1` so the
window can be out of focus without the numbers being a lie.

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

| | p50 | render |
|---|---|---|
| GL still deriving (default) | 2.11 ms | 2.31 ms |
| GL cut out | **0.835 ms** | 0.93 ms |

**1.3 ms a frame, at a view that draws almost nothing.** It lands inside the measured render time
because GL's walk happens in `D_Display`, after the profiler's render mark. The saving scales with
how much GL has to walk, not with what the backend draws: the same test at Sunder MAP16's spawn --
a small room -- saves 0.13 ms of 0.69, and the switch's own note records ~7 ms on MAP16 from a
vantage that sees the map.

And the picture is already right: standalone against default at the same camera differs by **0.3%**,
with sprites present (93 of them). The old note listing sprites as a blocker is stale.

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
   is the only thing between standalone and being correct, and it is worth roughly the 1.3 ms above.
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
work out a picture nobody looks at, and once by the backend, from geometry it already had.
