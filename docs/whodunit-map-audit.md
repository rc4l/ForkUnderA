# The Whodunit set, all 32 maps: where the frame time actually is

Measured 2026-08-22 on the build at `f5ad5b57`, Vulkan presenting, sim running, locked instance, a
ten-second sample at each map's spawn vantage. `wdi20` would not unfreeze and has no reading.

| map | med ms | fps | p99 ms | hitch% | light/moved sectors |
|---|---|---|---|---|---|
| wdi07 | 13.13 | 76 | 16.13 | 0.1 | 0 / 0 |
| wdi03 | 12.78 | 78 | 27.63 | 3.1 | 0 / 0 |
| wdi18 | 9.03 | 111 | 30.25 | 2.1 | 25849 / 0 |
| wdi23 | 8.02 | 125 | 9.52 | 0.0 | 0 / 0 |
| wdi02 | 7.16 | 140 | 9.25 | 0.0 | 42 / 0 |
| wdi01 | 6.55 | 153 | 8.61 | 0.0 | 0 / 0 |
| wdi22 | 3.69 | 271 | 4.88 | 0.0 | 0 / 0 |
| wdi06 | 3.47 | 288 | 5.44 | 0.0 | 1130 / 0 |
| wdi13 | 3.23 | 310 | 4.86 | 0.1 | 0 / 0 |
| wdi10 | 3.20 | 312 | 5.27 | 0.1 | 0 / 0 |
| wdiexsec | 2.86 | 349 | 3.89 | 0.0 | 0 / 0 |
| wdi04 | 2.83 | 354 | 4.77 | 0.3 | 192 / 0 |
| wdi25 | 2.76 | 362 | 3.98 | 0.1 | 3486 / 0 |
| wdiex02 | 2.39 | 419 | 6.17 | 9.7 | 80450 / 27790 |
| wdi21 | 2.38 | 420 | 3.38 | 0.0 | 8708 / 0 |
| wdi24 | 1.73 | 579 | 2.50 | 0.0 | 120 / 0 |
| wdi14 | 1.50 | 666 | 2.34 | 0.0 | 792 / 0 |
| wdi12 | 1.33 | 752 | 2.32 | 0.6 | - |
| wdi16 | 1.30 | 771 | 2.21 | 0.4 | 0 / 0 |
| wdiex01 | 1.27 | 785 | 38.11 | 39.4 | 73 / 13102 |
| wdi05 | 1.14 | 875 | 2.83 | 3.7 | 0 / 0 |
| wdiab02 | 1.03 | 968 | 2.06 | 1.0 | 4188 / 24644 |
| wdi19 | 0.75 | 1336 | 1.57 | 2.0 | 0 / 0 |
| wdi09 | 0.65 | 1529 | 2.37 | 9.5 | 3486 / 367 |
| wdi15 | 0.61 | 1635 | 1.71 | 8.6 | 0 / 0 |
| wdi08 | 0.46 | 2169 | 2.08 | 14.3 | 0 / 0 |
| wdi11 | 0.43 | 2346 | 1.99 | 20.1 | 0 / 0 |
| wdi17 | 0.42 | 2390 | 1.40 | 19.1 | 0 / 0 |
| wdiex03 | 0.35 | 2894 | 1.34 | 22.3 | 24428 / 0 |
| wdiexhub | 0.31 | 3183 | 1.29 | 23.4 | 0 / 0 |
| wdiexct | 0.26 | 3873 | 1.15 | 24.4 | 0 / 0 |
| wdi20 | - | - | - | - | SIM WOULD NOT UNFREEZE |

**Read hitch% against the median, not on its own.** The *fastest* maps carry the highest percentages
-- wdiexct at 24.4% -- because "twice the median" of 0.26 ms is a meaningless bar; their absolute tail
is about a millisecond. The number only means something where the median is already slow.

## Finding 1: the slow maps are not slow for any reason fixed this session

`wdi07` (76 fps), `wdi03` (78 fps) and `wdi23` (125 fps) invalidate **zero** sectors. No animated
lights, no moving geometry, an invalidation loop of 0.009-0.012 ms/frame. Every millisecond is raw
draw.

That is a different bottleneck from the tic-coupled stutter, and on this content it is the larger one:
`wdi03` is **28x slower than `wdi11`** on the same engine and the same build. Nobody has looked at it.

## Finding 2 -- CORRECTED

**What follows below was wrong in two ways, and the corrections matter more than the original.**

**It does not degrade.** `fua_frametimes` keeps a 1024-frame ring. The "3.25 ms then 33.9 ms" reading
was the ring still holding frames from the *previous* map right after the change, and then filling
with wdiex01's. A fresh launch straight into wdiex01 reads 33.7 ms from the first sample. The map is
simply a 30 fps map, flat, with no decay.

**The leak was real but was in the wrong layer, and was not the cost.** `MeshStore`'s size-change
path -- named below as the mechanism -- has `g_reallocs == 0` on wdiex01; it never runs there. The
actual leak is in the backend: `RetireSlot` empties a `VBSlot` when a surface goes away but leaves it
allocated, and a surface that came *back* was treated as new and **appended**, abandoning the space it
had. A door's upper exists when the door is shut and not when it is open, several times a second.
100% of wdiex01's appends were this case, none a genuine size change.

`ReviveSlot` now puts a returning piece back into the slot it left, refusing when it no longer fits or
when its material has moved it to another batch. Appends went from climbing ~1,060 every twelve
seconds to **flat at 24 (0% of slack)** with 8,294 revivals, and the picture is unchanged (0.1% on
dbab02/dbab04, and wdiex01 renders correctly after thousands of revive cycles).

**Fixing it changed the frame time not at all** -- still 33.7 ms. It was an unbounded resource leak,
almost certainly the reason the engine dies after eleven map loads, and not the reason the map is
slow.

## Finding 2b: what actually makes these maps slow

Frame time is **linear in the total number of piece records**, whatever is on screen:

| map | piece records | median | us per piece |
|---|---|---|---|
| wdiex01 | 114,646 | 33.67 ms | 0.29 |
| wdi03 | 27,690 | 10.51 ms | 0.38 |
| wdiexct | 159 | 0.25 ms | - |

It is not fill rate (320x200 measures 33.77 ms against 1280x800's 33.78), not dynamic lights (33.65
with them, 33.71 without), not draw calls (153-199 for all three), and not the sim (`ticprof` max
3.77 ms). Something walks **every piece record every frame** at roughly 0.3 us each, and nothing culls
it. That is the same bottleneck as Finding 1 -- `wdi03` and `wdi07` are slow because they are big, not
because they do anything.

**That is the next thing to fix, and it is worth more than everything else in this document.**

## The original Finding 2, kept for the record (WRONG MECHANISM)

`wdiex01` reads 3.25 ms (308 fps) on the first sample after load and settles to a **flat 33.9 ms
(30 fps)** within a minute. The raw dump is uniform -- `33.5 33.9 33.8 36.4 32.6 33.2 ...` -- not
spiky. Worst single frame in the whole audit: **2,421 ms**.

It is not the bake (invalidation loop 0.45 ms) and not the sim (`ticprof`: median 1.06 ms, max 3.77,
**zero** tics over 10 ms). The backend counters say what it is:

```
t1  med 33.88    9060 appends ( 54360 verts, 37% of slack)
t2  med 33.93   10141 appends ( 60846 verts, 42% of slack)
t3  med 33.99   11205 appends ( 67230 verts, 46% of slack)
t4  med 33.99   12206 appends ( 73236 verts, 51% of slack)
```

Monotonic: about 6,400 vertices every twelve seconds, never reclaimed. The mechanism is `MeshStore`:

```cpp
if (range.count != 0) { g_reallocs++; RetireRange(range); }   // size changed: retire the old one
...
g_used += count;                                             // and bump-allocate fresh space
```

`RetireRange` forgets the offset. **There is no free list**, so a piece whose vertex COUNT changes
leaks its old space permanently. `wdiex01` has 13,102 moved-plane invalidations against 73 light-only
-- doors and lifts changing which wall parts exist, and how many band splits each carries, every tic
-- so this runs constantly.

`MeshSquash` already carries a comment warning about exactly this failure in the squash path:
"catastrophic when it happens every frame... grew the arena from 5.3k vertices to 82k... left running
it is the 47 GB leak that killed an earlier build." The squash path was fixed. **The size-change path
was not.**

The reshade added this session cannot help here: it applies only when the planes did not move, and
`wdiex01` is almost entirely plane movement.

## Finding 3: the engine dies after about eleven consecutive map loads

The first audit pass ran the maps back to back. It died at `wdi12` -- sim frozen, then the process
gone, and **no crash dump**, so it exited rather than faulted. Every map after it reported "FAILED TO
LOAD" because the audit was talking to a corpse.

`wdi12` in isolation loads and runs fine at **752 fps**, so this is cumulative across level changes,
not a bad map. Given Finding 2, arena exhaustion is the obvious suspect, and it is also the best lead
on the `wdiex02` crash originally reported. The second pass relaunched every four maps and completed
all twenty remaining.

## What to do next, in order

1. **Give the vertex arena a free list, or compact it.** One bug behind a 10x degradation, a probable
   crash, and unbounded memory growth. Everything else here is smaller.
2. **Find out why `wdi03`, `wdi07` and `wdi23` cost 8-13 ms to draw** with no dynamic work at all.
   That is the real frame time on a third of this content.
3. `wdi20` would not unfreeze. Probably map-specific scripting; worth one look.
