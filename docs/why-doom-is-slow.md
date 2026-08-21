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

Two things, not one. The first is done; the second was missed when this was written.

### Walls -- done

DONE. `wallbands_compute` cuts a wall at its light bands the way `SplitWall` does, so the map bake
can carry a 3D floor sector instead of handing it back. dbab01's map bake goes from 2139 parts on
1968 segs to 3019 on 2570, every part GL draws has geometry in the mesh, and under standalone the
walls come out complete -- 3D floor steps included.

### Flats -- next

`flatmesh.cpp` captures `GLFlat`s from the same walk that standalone stops, so with GL cut out the
floors and ceilings are simply not there: the frame renders every wall correctly over a bare sky.
That is the whole of the 20.6% still between standalone and default on dbab01.

It is the same shape of job as the walls and the inputs are already map data -- a subsector's
vertices, the sector's plane, its offsets, scale, rotation and light -- so the work is to split
`RegisterFlatSubsector` into "what a flat is made of" and "store it", and then fill the first half
from the map instead of from a `GLFlat`. Sector floors and ceilings are the bulk of it; 3D floor
faces are enumerated the way `GLFlat::ProcessSector` enumerates them.

### The original, for the record

`fua_surface_mapbake_auto 1` + `fua_dg_standalone 1` renders **30.7% wrong**, drawing 49 batches
where the default draws 74. The cause is the ownership split: a seg whose sector has a 3D floor light
list cannot be lit from the map -- `SplitWall` cuts that wall into bands, each with its own light --
so it is deliberately left to the capture. Under standalone there is no capture, so those walls are
simply absent.

That is the whole of it. Standalone needs the map bake to carry 100% of the level, and the map bake
currently carries all of it except the sectors it hands back.

## Done, and what it came to

Sunder MAP10 at spawn, 1920x1200, alternating runs:

| | p50 | fps |
|---|---|---|
| default | 8.99 ms | 111 |
| GL cut out | **2.11 ms** | **475** |

    fua_surface_mapbake_auto 1
    fua_dg_cullbatches 1
    fua_dg_standalone 1

Three things it took:

1. **Walls in 3D floor sectors, lit from the map.** `wallbands_compute` cuts a wall at its light
   bands the way `SplitWall` does, so those sectors stop being handed back to a capture that is not
   running. A seg owns `kMaxMapPieces` mesh ranges rather than `kMaxCachedPieces` -- different
   questions, and conflating them at four is why 3D floor segs were ineligible at all.
2. **Flats from the map.** `RegisterFlatSubsector` is split into what a flat is made of and what
   turns that into geometry; `BakeFlatsFromMap` fills the first half from subsector vertices, the
   sector's plane, its offsets and its light. `ProcessSector` picks faces by where the viewer is, so
   a bake emits both sides of everything and lets back-face culling drop the half turned away. A
   sector that moves re-bakes its planes, because the flat cache's stamp is only consulted by a walk
   that no longer happens.
3. **One quad per SIDEDEF.** The derivation draws the whole linedef, so a line the BSP split into
   four segs was contributing four coplanar copies -- and the map bake walks every seg. Sunder MAP20
   went from 376 pieces duplicating another's geometry to **0**, and MAP16's parity from 1.7% to 0.1%.

**Parity against the default renderer**, same camera: Doom 2 MAP01 with a door open 0.0%, Sunder
MAP16 0.1%, MAP04 0.2%, MAP10 0.5%, dbab01 with its 138 3D-floor sectors 0.7%, Sunder MAP20 3.9%.

MAP20 is the outlier and it is not missing geometry -- the difference is distant detail on a map with
49,641 coplanar overlapping pairs, where two surfaces in the same plane disagree about depth in the
last bit. That is why these stay **off by default**: 475 fps is one line of config away, and shipping
a visible difference on somebody's favourite map is not a default to set on their behalf.

**A door caught MID-TRAVEL reads 13.3% and is not a fault** -- two runs catching it at different
heights. It settles to 0.0%. Worth writing down because it looks exactly like a broken lift.

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
