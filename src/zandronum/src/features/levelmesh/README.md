# features/levelmesh

A persistent, indexed world mesh that is **patched where the sim changed it** instead of rebuilt from
scratch every frame. The full design, phasing and rationale live in `docs/levelmesh-PLAN.md`; this
README covers what is actually in the tree and which engine hooks it touches.

## Why

Measured on Sunder MAP10 (RTX 5080, 640x400, spawn): the frame is 5.83 ms p50, of which **0.28 ms is
sim and 5.74 ms is render — but only 1.74 ms of that is GPU.** The remaining ~4 ms is CPU, spent
building **8251 `GLWall` objects per frame** for a scene totalling 16,888 vertices. The engine is
CPU-bound in the renderer by roughly 2.3x while the GPU idles.

## Status: P0 — measurement only

**Nothing renders from this.** P0 exists to answer one question with data rather than arithmetic:
if every sidedef gets a fixed vertex range, what does a real map cost?

| file | role |
|---|---|
| `computation/surfacebudget_compute.{h,cpp,_test.cpp}` | Pure worst-case vertex budgeting. Per piece, per side, per level, plus buffer sizing. 19 tests. |
| `computation/lightband_compute.{h,cpp,_test.cpp}` | **P2a**: which 3D-floor light band a fragment falls in, so banding can move from geometry into the shader. Planes packed like the existing glow planes. 14 tests. |
| `levelmesh.{h,cpp}` | Engine glue: walks `sides[]`/`subsectors[]` collecting the static budget inputs, and the `fua_levelmesh_stats` console command that reports them. |

### Measured budgets (Sunder, 2026-08-15)

| map | sides | wall verts | wall MB | + flats | mean/side | worst |
|---|---|---|---|---|---|---|
| MAP10 | 26,691 | 1,168,072 | 22.28 | 24.37 MB | 43.8 | 144 |
| MAP14 | 96,826 | 3,658,070 | 69.77 | 78.64 MB | 37.8 | 126 |
| MAP20 | 114,407 | 4,535,032 | 86.50 | 96.35 MB | 39.6 | 306 |

Fixed ranges are affordable: `FFlatVertexBuffer` already allocates 40 MB of per-frame scratch, so
MAP10's whole wall mesh costs less than a buffer the engine has today. The two-tier fixed/pooled
fallback is dropped.

### Using it

Load a map and run `fua_levelmesh_stats`. It prints side/sector/subsector counts, worst-case wall
vertices and their buffer size, the flat vertices `FFlatVertexBuffer` already reserves for
comparison, and the mean/worst per side — the tail is what decides fixed ranges versus pooling.

## In-engine hooks

**None yet.** P0 is a console command and a pure computation unit; it reads existing map structures
and writes nothing. The dirty-tracking hooks the later phases need are enumerated in
`docs/levelmesh-PLAN.md` under "Invalidation taxonomy" and are not wired up.

## Things that will bite

- **`side_t::numsegs` and `vertex_t::numsectors` come from `gl_PreprocessLevel`** (`PrepareSegs` and
  `InitVertexData` in `gl/data/gl_setup.cpp`). A dedicated server skips that, so both read zero and
  the budget understates. `CollectSideBudgets` detects this and `fua_levelmesh_stats` says so rather
  than printing a confident wrong number.
- **The budget assumes the equal-height dedupe in `gl_RecalcVertexHeights` is dropped** (plan P2b).
  With the dedupe in place the per-edge vertex count varies as sectors move, which is exactly what a
  fixed range forbids.
- **`side_t::V1()`/`V2()` are `inline` inside `po_man.cpp`** and cannot be reused. `SideVertices` in
  `levelmesh.cpp` re-derives the same thing, mirroring `GetSideVertices` in `gl/data/gl_setup.cpp`.
