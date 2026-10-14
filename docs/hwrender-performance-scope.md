# The Vulkan backend's frame cost — scope for later

Written 2026-10-13, after the GL-parity program. Nothing here is built. It says what a frame costs
today, what was measured and when, and the order to take the work in. Numbers older than the code
they describe are marked as such; step 0 re-measures them before anything is changed.

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

**Every other view repeats the frame.** The sky portal, stacked portals (2 by default, up to 8),
plane mirrors (up to 16), the line-mirror tree (up to 12 levels) and each visible camera texture each
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
