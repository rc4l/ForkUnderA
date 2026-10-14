# The Vulkan backend's frame cost — scope for later

Written 2026-10-13, after the GL-parity program, and re-measured 2026-10-14 (step 0, below). It says
what a frame costs, what was measured and when, and the order to take the work in. Numbers older than
the code they describe are marked as such.

## What a frame does now

**The walk (CPU).** `WalkView` (`features/visibility/viswalk.cpp`) cuts the view into angular slices,
one per pool lane, rebalanced from last frame's cost (`fua_vis_balance`), and clips them in parallel
(`ClipSlices`). Serial around that:

- up to three re-clips when a height-transfer guess or a leaking node box is wrong;
- `ForEachRank`, which scans every subsector once there is more than one slice;
- `ReplayRank` and `TakeRankCandidates` (sector order, particles, sky events, the flats pass, an
  insertion merge across slices);
- decal dedupe and sort, and the stacked sectors.

The portal views (stacked sectors, line mirrors, the sky box) are walked after the player's, each as
one slice, so they are single-threaded.

**The world (GPU).** The level is one vertex buffer, drawn with non-indexed `Draw` per batch; with
bindless and `fua_dg_mergedraws`, neighbouring batches merge (Sunder MAP16: 165 draws to 1). Nothing
culls on the GPU. The walk's answer is a bit per piece per walk slot, uploaded every frame, and the
vertex shader moves a piece the walk did not reach off screen (`fuaWalked`, `dgscene_glsl.h`). So
**every pass runs the vertex shader over the whole level**, visible or not.

**Every other view repeats the frame.** The sky portal, stacked portals (as many as the level has),
plane mirrors (one per reflective plane), the line-mirror tree (up to 12 levels) and each visible camera texture each
run `DrawWorld` again into their own target: light clustering, constants, the whole batch loop,
decals, and the translucent sort with its index upload. Line mirrors, plane mirrors and camera
textures draw with walk slot -1, which is the whole level unculled.

**Per pass on the CPU.** Light clusters are binned on the CPU every pass (`BuildLightClusters`,
`dglights.cpp`; the GPU port is noted there as not done). `DrawBlended` sorts the translucent list and
re-uploads its index buffer every pass. The walk bits, (subsectors + sides) × 10, go up every frame.

## What was measured, and when

| when | where | what |
|---|---|---|
| 2026-08 | Sunder MAP10, GL | 4.0 ms CPU against 1.74 ms GPU after the wall cache (`docs/levelmesh-PLAN.md`) |
| 2026-08 | Whodunit, Vulkan | frame time linear in piece records, ~0.3 µs each: wdiex01 114,646 pieces at 33.7 ms, wdi03 27,690 at 10.5 ms; not fill rate, lights, draw calls (153-199) or the sim (`docs/whodunit-map-audit.md`) |
| 2026-10 | Sunder MAP14, q43 | 1.52 ms median facing the heavy area; the walk ~54% of the main thread, of which ~13% waiting on the slowest slice and ~17% serial rank replay; the backend's draw ~23% |

The Whodunit row predates the walk bits and the threaded walk. Its per-piece cost has probably moved
from the CPU to the vertex stage, which is the next thing to find out.

## Step 0, re-measured (2026-10-14)

Vulkan only (`play --no-gl --lock`), 1024x640, sim running; frame p50 from `fuactl abperf` (round 3),
the backend's submit and GPU time from `fua_diligent_bench 120`, the world pass from `fua_dg_dynstats`,
the walk's clipping from `fua_vis_report`.

| map, view | frame p50 | GPU | world pass CPU | walk clip | batches |
|---|---|---|---|---|---|
| Sunder MAP14, q43 | 1.22 ms | 0.19 ms (1.1M verts) | 0.12 ms | 0.54 ms | 52 |
| wdiex01, start, before | 5.0 ms | 0.18 ms | 4.01 ms | 0.07 ms | 47,981 |
| wdiex01, start, after | 0.9 ms | 0.10 ms | 0.31 ms | 0.05 ms | 47,981 |
| wdi03, start | 1.05 ms | 0.13 ms | 0.19 ms | 0.07 ms | 835 |
| wdi07, start | 1.88 ms | 0.39 ms | 0.41 ms (4 passes) | 0.17 ms | 1,046 |
| wdiex02, start | 1.28 ms | 0.21 ms | 0.33 ms | 0.06 ms | 1,332 |
| wdi01, start | 0.63 ms | 0.16 ms | 0.20 ms | 0.09 ms | 982 |

What it said, and what was done:

- **The GPU is not the cost.** It takes 0.1-0.4 ms on every map, the whole level's vertex stage
  included, so step 1 (GPU culling and indirect draws) has nothing to win yet and was not built.
- **wdiex01's cost was the translucent pass.** 47,608 of its batches are translucent pieces, one batch
  each, and every pass layered, sorted and index-uploaded all of them for the ~700 the walk reached;
  the sampler put 62% of the game thread there. The pass now leaves out what the pass's walk did not
  reach before it sorts (`ComputePassDraws`, `fua_dg_blendwalk`), which the scene shader would have
  moved off screen anyway, so the picture is the same (`fuactl ab fua_dg_blendwalk 0 1`: no change).
- **Animated textures were re-resolved per batch,** 0.06 ms a frame on wdiex01; they are now asked once
  per base texture (`ComputeGroupByKey`), and the A/B no longer tells the loop from no loop.
- **Views without walks are cheap here.** A line mirror made on wdiex01 (`fua_make_mirror`) costs
  0.19 ms of CPU for its pass; step 2 is not worth building until a map shows otherwise.
- **What is left is the walk and the sprites.** At q43 `WalkView` is 63% of the game thread: the
  clip 18%, the serial rank replay and candidates 12%, the wait on the slowest slice 10%. On wdi07 the
  sprite build is 0.48 ms for 1,354 sprites. Steps 4 and 5 are where the next measured win is, and
  frame pacing is not (`fua_dg_frames_ahead` 0, 2 and 3 measure the same).

## The work, in order

**0. Re-measure.** Sunder MAP10, MAP14 and MAP16, wdiex01 and wdi03, and a mirror-heavy and a
stack-heavy template, each with the CPU phases (`fua_dg_dynstats`, `fua_vis_report`, the framephase
clocks), GPU time (`fua_diligent_bench`'s timestamps) and frames shown (`fuactl screen`). Attribute
each suspect with `fuactl abperf` inside one session, not by which clock looks large.

**1. GPU culling with indirect draws.** A compute pass per view tests each piece, or each sidedef and
subsector group of pieces, against the walk's bit and the view's frustum. It writes a compacted list
and the indirect arguments, and the world draws with `DrawIndirect` and a count buffer. The walk stays
the authority on what is visible: the GPU only drops what the walk did not reach or what lies outside
the frustum, so the picture cannot change. That moves the vertex stage from the level's size to what
is seen, the most likely remainder of the Whodunit cost. The draws need per-piece vertex ranges (or an
index buffer) instead of one `Draw` per batch range.

**2. Views without walks.** Line mirrors, plane mirrors and camera textures draw the whole level. With
step 1, give each its own frustum on the GPU. That draws a superset of what GL's own walk through the
mirror draws, so it stays exact. Then walk the portal views on the pool in parallel, since each is
independent of the others.

**3. Per-pass CPU work.** Bin the light clusters on the GPU. Sort the translucent list once per view
per frame, and only when its set changed. Upload the walk bits as dirty ranges.

**4. The walk's serial phases.** Keep per-slice rank lists and merge them, rather than scanning every
subsector in `ForEachRank`, and fold `TakeRankCandidates` into that merge. This is the 17% from the q43
profile, and the 13% slice wait is what `fua_vis_balance` already chips at.

**5. Sprites on the GPU.** `docs/sprites-gpu-plan.md` stages 2 and 3, still blocked where that plan
says.

## Measured already, so not worth redoing

- Occlusion for sprites: built, a wash on every map (`docs/occlusion-scope.md`).
- CPU-side geometry storage: exhausted (`docs/levelmesh-PLAN.md`, P2d).
- Fewer draw calls: not the cost; merged draws already took them to one or a few.
- Another graphics API: D3D12 changes none of the above (`features/hwrender/diligent/README.md`).

## How each step is judged

- **The picture:** the scenario suite, GL against Vulkan, unchanged; `fua_vis_check` bit for bit for
  anything touching the walk.
- **The time:** `fuactl abperf` with the step's switch, on the maps from step 0, in one session.
- **What reaches the screen:** `fuactl screen`'s repeated refreshes per second at q43, as in the
  judder plan.
