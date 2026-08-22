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
