# Map baking on a live level: what made it a 3.7x regression, and what it costs now

`fua_surface_mapbake_auto` builds the level mesh straight from the map instead of waiting for GL's
BSP walk to visit each seg. It is on by default and stays on. On rc4l's own content (`wdiex02`, the
Whodunit set) turning it on used to cost more than everything else in the frame put together.

## The measurement

Median present interval on `wdiex02`, Vulkan presenting, sim running, same vantage:

| | median | p99 | fps at median |
|---|---|---|---|
| before | 10.95 ms | 13.83 ms | 91 |
| after | 2.46 ms | 7.99 ms | 406 |

## The bug: squash-then-rebake

`InvalidateMovedSectors` re-derives a dirty sector's segs. It did that in this order:

```cpp
for (int i = 0; i < kMaxMapPieces; i++) MeshSquash(sc.pieces[i].range);
BakeSegFromMap(idx);
```

`MeshStore` compares new vertices against the stored ones and uploads nothing when they match --
there is a comment on that comparison saying, in as many words, that without it "the backend would
re-upload the entire level every frame forever."

**`MeshSquash` memsets the range to zero.** So squashing first guaranteed the comparison could never
match. Every re-bake saw zeros, wrote back the same bytes it had a moment earlier, and marked them
dirty. On `wdiex02` that was ~12,200 vertices and ~1,548 repaints per frame for a world standing
still.

The fix is the other order -- bake first, and squash only what the bake declined:

```cpp
const int made = BakeSegFromMap(idx);
if (made < 0)   // declined: this seg is GL's, hand it back
    for (int i = 0; i < kMaxMapPieces; i++) MeshSquash(sc.pieces[i].range);
```

Nothing is lost by not squashing an owned seg: the bake squashes the band and 3D-floor slots it does
not fill itself, including the case the pre-emptive squash was really written for -- a door's upper,
which exists when the door is shut and not when it is open.

`BakeSegFromMap` now returns **-1 for "declined"** and **0 for "took it, made nothing."** They used
to be the same answer, which meant a seg the map owns but that legitimately emits nothing got
squashed a second time -- re-zeroing ranges that had just been written.

## The thing that was assumed and was wrong

The working theory was that moving geometry drove this: a lift or a door invalidating its sector
every frame. Counting it says otherwise. Over one run on `wdiex02`:

```
sector invalidations since load: 95292 light-only, 28469 moved a plane
```

**Three quarters of invalidations are sectors whose planes never moved.** `sector_t` bumps
`fua_dirty` from `ChangeLightLevel` and `SetLightLevel` exactly as it does from `ChangePlaneTexZ`,
and Doom's light effects -- flicker, strobe, glow -- run every tic. A room with a flickering lamp was
re-deriving its walls from the map several times a second to express a light level.

That is what `fua_bake_dirty` reports, and why it exists.

## What it costs now

The invalidation loop is **0.294 ms/frame**, re-baking ~1,215 segs. Toggling the cvar inside one
instance puts the whole feature at ~1.25 ms/frame (2.46 vs 1.21 median), so roughly a millisecond is
downstream of the loop -- the backend applying repaints for surfaces whose shading genuinely did
change, because the lights genuinely are flickering.

**The next cut, if one is wanted**, is to stop re-deriving geometry for a light-only change: the band
structure of a wall is fixed by 3D-floor plane heights, which a light change does not move, so only
each band's `CaptureShading` result differs. It was not built here because 0.294 ms did not justify
it -- measure again before assuming it does.

## Verified by

- Vulkan picture before vs after: **0.1% on dbab02, 0.2% on dbab04**, which is those maps' own reload
  floor.
- GL-vs-Vulkan diff **bit-identical** before and after (46.7% / 82.3%), i.e. the change moved nothing.

That 46.7%/82.3% GL-vs-Vulkan gap is itself unexplained and **pre-existing** -- it reproduces on the
pre-change binary. `png --align` reports `dx=0 dy=0`, so it is not framing; it is a broad, low
amplitude (`mean|d|` 11.6) brightness difference across the whole image, plus the console overlay in
the top fifteen rows. It contradicts `occlusion-scope.md`'s "pixel-identical on dbab01-05" and
deserves its own investigation.

## Two traps this cost time to find

- **`fuactl launch` does not set `fua_vulkan`.** An instance launched without
  `--cvar fua_vulkan=1` runs pure GL, `fua_frametimes` reports "not enough frames", and every
  backend counter sits frozen at zero while the engine looks perfectly healthy.
- **`sim.pause {"on":false}` used to pause.** `sim.pause` ignored its argument and always set
  `paused = 1`, answering `{"paused":1}` -- and `GetInt` cannot parse `false` anyway. A frozen sim is
  exactly the state in which moving sectors and animated lights read as "no difference". Both halves
  are fixed: `sim.pause` honours `on`, and `GetBool` reads JSON booleans, with tests pinning that
  `GetInt` cannot.

---

# The choppiness: a spike once per tic, and three things that made it

Fixing the median left a separate complaint standing -- "146 fps and the mouse movement is very
choppy... it does sometimes report 480 fps but the choppiness is a big one." That is not something a
median can show, and `fua_frametimes`' raw dump is what showed it:

```
4.0 3.2 2.9 2.9 2.7 2.7 [7.8] 3.6 2.8 4.3 3.6 2.8
2.5 2.5 [7.8] 3.6 3.4 3.0 2.7 4.3 2.9 [7.9] 3.6 2.4
```

Every cycle sums to ~28.5 ms -- **one Doom tic at 35 Hz**. One frame in eight cost 3.5x the rest.
The sim tic itself is only ~1.04 ms median (`fuactl ticprof`), so the tic was not the spike; the tic
was the *trigger*. Everything a tic dirties is answered on the very next frame, and the other seven
frames do nothing.

## 1. One UpdateBuffer per repaint

The backend applied each repainted surface with its own 64-byte `ctx->UpdateBuffer`. On the frame
after a tic that was ~1,548 driver calls. `g_scenePieceData` is a full CPU mirror of that buffer, so
any *span* of it is safe to upload whether or not every entry changed -- which makes near-misses
mergeable. Sorting the slot indices and bridging gaps of up to 32 entries turned **1,492,083 repaints
into 15,993 uploads, 93:1**.

## 2. Re-deriving a wall to express a flicker

`ReshadeSegFromMap` re-lights a seg without re-deriving it. If a sector's planes did not move, the
band structure cannot have changed -- band boundaries are 3D-floor plane heights -- so only each
band's light differs, and everything else (material, base texture, normal, alpha, blend mode) is read
back off the piece the last bake registered. The one input a reshade cannot re-derive is *which*
light list entry a band used, so the bake now records it in `SegCache::bandLight`.

It declines rather than guesses: a seg it never baked, one whose stored piece has gone, one that does
not own its side, or one carrying a **fog boundary** -- the single piece whose light comes off an
`F3DFloor` rather than the band list. Ordinary 3D-floor blocks pick their light out of the same list
a band does, so they reshade too. A decline falls back to the full bake, which is always right.

## 3. Doing a tic's worth of work on one frame

Even coalesced, ~1,500 surfaces changing appearance at once is a lump landing 35 times a second. A
repaint is a *shading* change, not a position change: a surface carrying last frame's light for
another two milliseconds is invisible, whereas a surface in last frame's *place* would not be. So
repaints -- and only repaints, never the geometry patch -- are queued and drained at 512 a frame,
with a flush-all escape if the backlog ever runs away.

## Measured, four fixed vantages on wdiex02

Medians are unchanged, which is the point -- this is all tail:

| spot | median | p95 | p99 | worst frame |
|---|---|---|---|---|
| 1008,480 | 0.63 -> 0.60 | 3.43 -> **1.45** | 5.32 -> **3.43** | 7.04 -> **4.66** |
| 280,1085 | 0.80 -> 0.76 | 3.54 -> **2.12** | 6.23 -> **3.76** | 7.68 -> **6.21** |
| -5160,3328 | 1.24 -> 1.21 | 5.87 -> **3.91** | 7.27 -> **4.60** | 7.83 -> **5.78** |
| -5616,1728 | 2.50 -> 2.54 | 7.50 -> **5.54** | 8.68 -> **7.04** | 9.57 -> **8.17** |

The spike is ~5 ms above baseline at *every* vantage regardless of what is on screen, which is what
identifies it as tic-coupled rather than view-dependent.

## How the reshade was proved correct

Percentile numbers cannot show a lighting bug. So: run the level for twenty seconds with every light
effect animating through the reshade path, freeze it, screenshot, then run `fua_surface_mapbake` --
which re-derives all 45,849 parts from the map -- and screenshot again.

**0.0% over the world region.** A complete re-derivation produces the same pixels the reshade had
already arrived at. (The 0.5% full-frame figure is the console text the re-bake printed between the
two shots.) Also 0.1% Vulkan before-vs-after on dbab02 and dbab04, both maps' own reload floor.

## What is left, and the honest ceiling

Skipping light-only work *entirely* -- correct for nothing, but it bounds the prize -- gives p99 4.40
and 0.8% of frames over twice the median. The tail is now within reach of that without freezing a
single light. What remains is genuine per-tic work: the sim tic, and the surfaces that really did
change. Threading the sim is the only thing left that would remove it, and that is a different
project.

One measurement trap worth keeping: `fua_surface_mapbake` leaves the mesh heavier than it found it,
so an instance that has run it is no longer a valid timing baseline. A spot that read 0.63 ms on a
clean load read 3.97 ms in an instance where the verifier had been run.

## Open: a derived wall samples the wrong part of its texture (2026-08-24)

Found from rc4l's vantage on `wdiex02` at `-4523,-468,-490` facing `270,-9`, saved as the spot
`sky-and-ship`. Looking west across the water, the far backdrop wall -- linedef 30450, one-sided,
mid texture `SRISE_BK`, a 1024x512 night skyline -- renders in Vulkan as a flat, dark, red,
detail-free band where GL renders a lit city.

It is NOT lighting, NOT the models work, NOT culling, and the masked cut-out in front of it (the
tanker, `XCTANKER`, a paletted PNG with tRNS) is drawn correctly. Measured in the same band:

| | max | mean | r/g/b |
|---|---|---|---|
| GL, final | 217 | 8.54 | 10.8 / 8.2 / 6.7 |
| Vulkan, final | 8.3 | 1.59 | 2.9 / 0.9 / 1.0 |
| Vulkan, raw texel (`fua_dg_lightmode 10`) | 29 | 6.59 | 12.1 / 3.8 / 3.9 |
| Vulkan, vertex light (`fua_dg_lightmode 4`) | 102 | 67.8 | flat grey, = the recorded 0.27 |

The vertex light is right and the texture SAMPLE is wrong -- red-dominant with no highlights at all,
which is what a texture minified to its top mip looks like. The v range is being stretched far past
what the wall should carry, so the sampler is asked for a level that has averaged the skyline away.

**Localised precisely, and this is the useful part:** `fua_surface_mapbake_auto 0` -- which leaves
every wall to GL's capture instead of deriving it from the map -- makes the band read max 183.3 /
mean 7.86 / r 10.4, against GL's 215 / 8.47 / 10.7. So the derived path is wrong and the captured
path is right, for this wall, on this map. The bug is in `BuildDerivedWallSpan`'s one-sided-middle
case (`features/surfaces/surfacebuild.cpp`) or in the `mRenderHeight` it takes its v scale from --
not in the backend at all.

Not yet fixed. The workaround costs the bake, which is the whole point of the derivation, so it is a
diagnostic rather than a setting to ship.
