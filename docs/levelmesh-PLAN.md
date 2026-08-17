# features/levelmesh — a persistent world mesh, and the GPU-driven renderer it unlocks

**Status: proposed, not started.** Divergence branch. This document is the scope, not a commitment.

## Goal

Replace *deriving world geometry every frame on the CPU* with *patching a persistent mesh when the
world actually changes*. Concretely: stop constructing a `GLWall` per visible wall per frame in
`GLWall::Process` (`gl/scene/gl_walls.cpp:1424`) and streaming its vertices into a mapped buffer, and
instead keep the whole level as one indexed triangle mesh that only changes where the sim changed it.

That single change is the prerequisite for everything else worth having — GPU culling, indirect
draws, bindless materials, baked or realtime GI, and eventually ray tracing. None of those are
reachable while the mesh is rebuilt from scratch 200 times a second.

**The measuring stick is Ironwail** (`andrei-drexler/ironwail`), a QuakeSpasm fork that does exactly
this. Worth stating plainly up front, because it corrects an easy assumption: **Ironwail is OpenGL
4.3, not Vulkan.** Its GPU culling, compute-generated index buffer, indirect multi-draw and clustered
lighting are all GL. The performance is architectural, not API. So this plan is an OpenGL plan.

## Baseline — measured, not assumed

**Sunder MAP10 "The Hag's Finger", RTX 5080 (GL 4.5), 640x400, standing at the spawn.**
Captured with `fuactl capture` + `fuactl gl-timers`, 2026-08-15.

| metric | value |
|---|---|
| frame p50 / p99 / max | 5.83 / 8.24 / 8.70 ms (166 fps avg, 117 fps 1% low) |
| sim / render split | **0.28 ms sim / 5.74 ms render** — render is 95% of the frame |
| GPU total | **1.74 ms** (scene 1.52, translucent 0.21, hud2d 0.00) |
| **CPU in the render path** | **~4.0 ms** — the GPU idles ~70% of the render window |
| per-frame draw counters | **8251 walls, 394 flats, 1258 sprites, 16888 vertices** |

16,888 vertices is nothing for that GPU; even the 1.52 ms of scene time is mostly draw-call and
state-change overhead rather than rasterization. The ~4 ms is 8251 `GLWall` objects built,
texture-mapped, split and streamed **every frame**, plus the BSP walk, the clipper and the drawlist
sort. **The engine is CPU-bound in the renderer by roughly 2.3x on this map.**

That is the number this plan exists to move, and the number every phase gets measured against.

Caveats on the baseline, so it is not over-read: 640x400 understates the GPU share (CPU cost is
resolution-independent, so at 4K the split narrows but the 4 ms does not shrink); it is one viewpoint
at the spawn, not the map's worst vista; and it was captured on a `browser-join-server` worktree build
rather than `main` (same renderer code, different branch). Re-measure on `main` before quoting it as
a regression gate.

**Method for every later measurement** — same map, same spawn, same resolution, `capture` then
`gl-timers`, per `.claude/skills/zandronum-session`. Prefer `fuactl bench` / `perf-ab` over a single
`capture` for anything comparative.

## Why this is not a third renderer

Two prior efforts are relevant, and this plan has to justify itself against both.

**The renderer staircase** (`docs/renderer-staircase.md`) landed at flight 23 — core profile on all
platforms, ~GZDoom 2.0.05-plus. Further progress is gated on the base-engine backport (#41). This
plan is **orthogonal to that gate**: a level mesh is our own data structure over our own map
structures, and needs nothing from ZDoom's coordinate refactor. The staircase can resume
independently, and should.

**The hwrender port** (`docs/hwrender-branch-status.md`, `docs/hwrender-PLAN.md`) is the cautionary
one. The staircase doc's verdict on the archived hybrid was that *"every seam between our renderer and
an adopted one is a defect factory — fifteen of them are cataloged in its history."* That is correct
and this plan must not repeat it.

The distinction that makes it not a repeat: **the level mesh is a data seam, not a renderer seam.**
There is one renderer throughout. It reads its geometry from a new place. There is no second
pipeline, no capture layer, no adapter translating between two renderers' conventions, and no period
where two things draw the same frame. Phase 2 below is specifically designed so the existing GL
renderer draws from the mesh and the output is expected to be *pixel-identical* — which is a testable
claim, unlike "the hybrid looks about right."

If at any point this plan grows a second draw path, it has become hwrender and should be stopped.

## What the tree already tells us

Findings from reading the current code. These set the design.

### 1. Level topology is immutable at runtime

Lines, sides, sectors, subsectors and segs are allocated in `P_SetupLevel` (`p_setup.cpp:3932`) and
never added to or removed from afterwards. Only **positions, materials and transforms** change. This
is the property that makes a persistent mesh tractable at all — every surface can be assigned a
stable ID and a fixed buffer range at load time.

### 2. Plane movement has a clean chokepoint, and it is *not* the plane equation

`dsectoreffect.cpp` writes `floorplane.d` **raw and speculatively**: it sets `d = dest`, calls
`P_ChangeSector`, and restores `d = lastpos` if the move crushed something. Hooking the plane
equation therefore fires on moves that never happened.

`sector_t::ChangePlaneTexZ` (`r_defs.h:695`) is called only on **committed** moves, and already calls
`SetAllVerticesDirty()`. Together with the three dirtifying `SetPlaneTexZ` callers in
`r_data/r_interpolate.cpp:454`, `:459`, `:490`, that is the complete and correct "a plane really
moved" signal.

### 3. Moving planes are dirty every *frame*, not every *tic*

`R_SetupFrame` calls `interpolator.DoInterpolations(r_TicFrac)` (`r_utility.cpp:895`), which mutates
`floorplane.d` and `TexZ` to the sub-tic position; `RestoreInterpolations` puts them back after the
scene is built (`gl/scene/gl_scene.cpp:917`). So any sector with a live interpolation must be patched
per frame. This is fine — it is a Z-only patch over a known buffer range, which is exactly what
`FFlatVertexBuffer::UpdatePlaneVertices` already does — but it rules out any design that assumes
tic-rate invalidation.

### 4. Slopes are effectively static

`plane.a`/`plane.b` are written at load (`p_udmf.cpp:1544-1577`, `p_slopes.cpp`) and in one netcode
client path (`cl_main.cpp:6890`). Treat a slope change as a rare full-sector rebuild; do not design
for it.

### 5. Polyobjects mutate their vertices in place — and should not

`FPolyObj::DoMovePolyobj` (`po_man.cpp:1354`) and `RotatePolyobj` write `Vertices[i]->x/y` directly,
and `gl_bsp.cpp:267` rebuilds a per-frame mini-BSP for them. A polyobject is a **rigid body**. In the
mesh it should be its own sub-mesh with a transform, which removes the per-frame rebuild entirely and
is also the only representation a TLAS can use later.

### 6. We already have half a level mesh

`FFlatVertexBuffer` (`gl/data/gl_vertexbuffer.cpp`) *is* a level mesh for flats: baked once in
`CreateFlatVBO` (`:307`), stable per-sector ranges (`sector_t::vboindex[4]`, `vbocount[2]`), in-place
Z patching (`UpdatePlaneVertices`, `:343`), lazy invalidation by comparing `vboheight` against
`GetPlaneTexZ` (`CheckUpdate`, `:413`).

That design is correct and proven in shipping code. **The work is giving `side_t` what `sector_t`
already has.**

### 7. The seamless-wall machinery is runtime T-junction repair, and can be baked

`gl_vertex.cpp`'s `SplitLeftEdge`/`SplitRightEdge` insert a vertex at every height in the shared
`vertex_t::heightlist`; `SplitUpperEdge`/`SplitLowerEdge` insert one at every other seg's `sidefrac`
on the same sidedef. This exists because **the mesh is not watertight** — those are genuine
T-junctions, patched every frame.

The topology of that repair is static: `vertex_t::sectors[]` is built once in `InitVertexData`
(`gl/data/gl_setup.cpp:412`) and never changes, and the per-sidedef seg list is built once in
`PrepareSegs` (`:517`). Only the *heights* move. The one thing making the vertex count dynamic is
`gl_RecalcVertexHeights` (`gl/data/gl_data.cpp:466`), which dedupes equal heights and zeroes
`numheights` when there are ≤2.

**Drop that dedupe and the vertex count per edge becomes constant**, at which point walls patch in
place exactly like flats do today, and the mesh is watertight by construction. That is a hard
requirement later — ray tracing does not tolerate T-junctions — so it is worth taking early.

## Invalidation taxonomy

| source | hook | frequency | mesh action |
|---|---|---|---|
| plane height | `sector_t::ChangePlaneTexZ` (`r_defs.h:695`) + 3 interp callers | per frame while moving | patch Z over the sector's ranges |
| slope | `p_slopes.cpp`, `cl_main.cpp:6890` | ~never | full sector rebuild |
| sector/side texture | `sector_t::SetTexture` (`r_defs.h:659`), `side_t::SetTexture` (`:975`) | rare | material index only — **no geometry touch** |
| tex offset / scale / angle | scrollers, ACS panning (`side_t::SetTextureXOffset` etc, `r_defs.h:980+`) | per tic | **per-surface uniform, never baked into vertex UVs** |
| 3D floors | `P_Recalculate3DFloors` (`p_3dfloors.cpp:427`), `P_RecalculateAttached3DFloors` (`:670`) | per frame w/ moving model | rebuild affected sides |
| polyobject | `DoMovePolyobj`/`RotatePolyobj` (`po_man.cpp:1354`) | per tic | transform only |
| line/side add/remove | — | never | — |

The two rows worth calling out as design constraints rather than mechanics:

- **Texture panning must not bake into vertex UVs.** Scrolling sky and conveyor floors are common;
  baking would dirty those sectors every tic forever. Keep offset/scale/angle as a per-surface record
  in a side buffer and apply it in the shader — which is what `gl_SetPlaneTextureRotation`
  (`gl/scene/gl_flats.cpp:77`) already does via the texture matrix.
- **Material changes must not touch geometry.** Switch textures, animated flats and ACS retexturing
  are frequent; they should be a single index write.

## Design

### The data model

```
LevelMeshSurface            // stable ID, assigned at build, never reordered
    kind                    // wall_top | wall_mid | wall_bottom | wall_ffloor | flat_floor | flat_ceil
    owner                   // side_t* + part, or sector_t* + plane + subsector
    vertexRange             // fixed [first, count) into the mesh vertex buffer
    indexRange              // fixed [first, count) into the mesh index buffer
    plane                   // secplane_t at build time, patched on move
    materialIndex           // into the material table — the only thing a texture change writes
    uvParams                // offset/scale/angle — per-surface, shader-applied
    lightmapRect            // atlas rectangle, filled in P4
    flags                   // sky | masked | translucent | static

LevelMesh
    vertices[]              // welded, watertight
    indices[]
    surfaces[]
    dirtySurfaces           // set, drained once per frame before draw
    polyMeshes[]            // one per polyobject, each with its own transform
```

Key invariants:

- **Ranges are assigned once and never move.** A surface's vertex count is fixed at build time
  (worst case over its possible split configurations); its *contents* change, never its extent. This
  is the same trick Ironwail uses for its index buffer: static layout, variable counts.
- **Stable IDs are load-bearing.** They index the material table, the lightmap atlas, and later a
  ray-traced hit lookup. Nothing may reorder them after build. For 3D-floor surfaces the key is
  `(side_t*, master line_t*, part)` — **never `F3DFloor*`**, which is deleted and recreated every
  frame for moving sectors (Resolved §4).
- **The mesh is renderer-agnostic.** It knows nothing about GL. That is what lets one renderer read
  it during the transition instead of two renderers existing.

### Where it hooks

Build in the slot `gl_PreprocessLevel` already occupies (`gl/data/gl_setup.cpp:613`) — after
`PrepareSegs`, `PrepareSectorData` and `InitVertexData`, all of which it consumes. Teardown alongside
`gl_CleanLevelData` (`:684`).

Dirty marking goes in the seven hooks above. Every one of them is a one-line call into the mesh; per
`features/README.md`'s "unavoidable in-place hooks stay put", they get listed in the feature README.

### Module layout

Per `AGENTS.md` and `features/README.md` — this is mostly new files, so it is a feature folder:

```
src/zandronum/src/features/levelmesh/
    README.md                        # what it is, and every in-engine hook with file:line
    levelmesh.h / .cpp               # the mesh object, build, dirty drain, teardown
    levelmesh_build.cpp              # walls + flats -> surfaces (engine-facing glue)
    computation/
        weld_compute.{h,cpp,_test.cpp}       # ComputeWeldedEdge: the static T-junction vertex set
        surfacerange_compute.{h,cpp,_test}   # ComputeSurfaceVertexBudget: worst-case per side
        dirtyprop_compute.{h,cpp,_test}      # ComputeDirtyPropagation: sector -> affected surfaces
        chartpack_compute.{h,cpp,_test}      # ComputeChartPacking: lightmap atlas (P4)
```

The pure parts are genuinely pure — welding, budgeting, dirty propagation and rectangle packing are
all functions over plain arrays with no engine dependency, so the 100%-coverage rule costs little
here. `IMPLEMENT_CLASS` is not used by any of this, but the `.cpp` files still go in the
`add_executable( zdoom … )` list before `zzautozend.cpp` (`CMakeLists.txt:1479`) to match convention.

## Phases

Each phase ships independently and leaves a running game. That is the whole point.

### P-early — Wall draw batching **← DONE, and it ships a real win**

Not in the original phasing. Taken early because the baseline showed the cost is per-draw overhead,
not geometry — and batching is reachable without the mesh existing.

`GLDrawList::DrawWalls` now merges runs of identically-stated walls into one `GL_TRIANGLES` draw.
`diwcmp` was widened from (texture, clamp) to the full draw state so equal-state walls actually
arrive adjacent, and the wall sorts now run whenever `gl_batch_walls` is on rather than only under
`gl_sort_textures`. Fans are expanded to independent triangle lists, since a `GL_TRIANGLE_FAN`
restarts at its own first vertex and this GL floor has no primitive restart.

Conservative by construction: glowing walls and untextured walls never batch (their state is
per-wall), and only the opaque passes batch — `GLPASS_LIGHTSONLY` and `GLPASS_TRANSLUCENT` keep the
old path. Behind `gl_batch_walls` (default on).

**Measured, Sunder MAP10 spawn, same instance, alternating, 300 frames each:**

| `gl_batch_walls` | p50 | p99 | render | vertices |
|---|---|---|---|---|
| 0 | 5.653 / 5.643 ms | 8.14 / 7.65 ms | 5.58 / 5.54 ms | 16,888 |
| 1 | 4.543 / 4.466 ms | 6.67 / 7.26 ms | 4.54 / 4.46 ms | 25,332 |

**~1.15 ms off the frame, ~20% faster (177 → 222 fps), and the p99 tail improves too.** Vertices rise
1.5x from the fan expansion and it is still a clear net win, which is the whole thesis: at these
counts the draw call costs more than the geometry. Screenshots with the flag on and off are
identical.

### P2d — state-grouped index buffer **← BUILT, OFF. CPU-side geometry storage is now exhausted.**

The fix P2c pointed at: keep the vertices where they are, and use an **index buffer** to make each
draw-state run one contiguous span, so a run draws with one `glDrawElements`. Two passes (indices
must upload before anything draws from them), `(firstIndex, count)` per run -- literally a
`DrawElementsIndirectCommand` minus its instance fields. Renders correctly. Behind `gl_wallmesh`.

**Measured 5.00 ms against 4.08 ms for streaming -- clearly worse.**

Why, and this is the conclusion the whole P2 line was after:

> The existing streaming path writes vertices into a **persistently mapped** buffer
> (`ARB_buffer_storage`) and draws each batched run with one contiguous `glDrawArrays`. That is a
> memcpy-speed write with **zero API calls per frame**. Writing 4-byte indices instead of 20-byte
> vertices moves less data, but it reintroduces a per-frame `glBufferSubData` and a second pass over
> the draw list, and those cost more than the bytes saved.

**So CPU-side geometry storage is a dead end for frametime.** Persistent-mapped streaming plus
state-batched contiguous draws is already close to optimal for a CPU-driven renderer on hardware
that has `ARB_buffer_storage`. Three successive designs -- per-seg ranges, `glMultiDrawArrays`, and a
state-grouped index buffer -- all lost to it.

**What that means for the swap.** The remaining CPU is not in producing or storing geometry; it is in
*deciding what to draw* (Clip 0.451 ms + BSP 0.392 ms) and in per-draw state. Neither is reachable by
any buffer layout -- both need the visibility decision to move to the GPU. That is **P3** (GL 4.3
compute culling + indirect draws), and it is the only remaining step with a real win in it.

The P2c/P2d code stays in tree, off, because it is not wasted: a GPU culling pass needs exactly this
-- baked geometry with stable ranges and an index buffer it can write. What P2d proves is that
filling that index buffer **on the CPU** is pointless; the GPU has to fill it.

### P2c — persistent GPU geometry buffer **← BUILT, OFF, and it points at the real design**

`features/levelmesh/staticmesh.{h,cpp}`: a GL-resident vertex buffer, a bump allocator, a stable
`MeshRange {offset, count}` per cached wall piece, and `glMultiDrawArrays` over the ranges in a
batch. Behind `gl_wallmesh`, **default 0 because it measured slower**: 4.14 ms against 4.00 ms for
streaming. Renders correctly (screenshot-verified); it is simply not faster.

**Why, and this is the useful part:** wall batching already gives streaming **one contiguous
`glDrawArrays` per state run**. The persistent buffer hands that same run **N scattered ranges**
through `glMultiDrawArrays`. Persistence cost contiguity, and contiguity was worth more than the
per-frame memcpy it saved.

The fix is not to abandon persistent geometry. It is to **lay the buffer out by draw state**, so a
run is one contiguous range again — which is exactly what Ironwail's per-texture index buffer does,
and it is the precondition for indirect draws (an indirect command *is* an offset+count into a
state-sorted buffer). So P2c is the last step that can be reached by tweaking the existing draw path;
everything after it requires the buffer layout itself to be organised by material.

**That is the fork in the road toward the backend swap**, and it is now a concrete one:
`buffer laid out by state -> ranges become indirect commands -> a culling pass writes them -> the
backend stops needing the scene layer at all`.

### P2b attempt 3 — per-seg wall cache with counter invalidation **← LANDED**

`gl_wallcache`, default 1. Sunder MAP10 spawn, alternating, 300 frames each:

| `gl_wallcache` | p50 | p99 |
|---|---|---|
| 0 | 4.828 / 4.474 / 4.509 ms | 7.10 / 6.23 / 6.66 |
| 1 | **4.016 / 4.019 / 3.964 ms** | 6.79 / 6.08 / 5.81 |

**~0.60 ms, ~13% faster (217 → 250 fps), 98.6% hit rate, memory flat at 446 MB.** `stat rendertimes`
attributes it exactly: wall **Setup 0.979 -> 0.228 ms**, total Setup 2.417 -> 1.214, All 4.702 ->
3.708. Screenshots identical with the flag on and off.

Combined with wall batching, the original 5.83 ms baseline is now **4.00 ms — 31% faster overall,
166 -> 250 fps.**

What made attempt 3 work where 1 and 2 failed:
- **Fixed per-seg storage** (`kMaxCachedPieces = 4`) instead of a growing array, so re-capture
  overwrites and leaking is structurally impossible.
- **Three integer compares** against `sector_t::fua_dirty` / `side_t::fua_dirty` instead of a content
  hash. The engine bumps those counters at the few places that mutate anything a wall is generated
  from (`ChangePlaneTexZ`, `SetPlaneTexZ`, `SetTexture`, light setters, offset setters).
- Ineligible segs are **sticky**, so they are never re-captured.

### P2b attempts 1 and 2 — what went wrong **(kept: the failures cost real time)**

Cached each seg's generated `GLWall`s with a stamp of their generation inputs, and replayed them into
the draw lists when nothing changed. Both attempts measured SLOWER, and **both measurements were
invalid** -- worth recording, because the wrong conclusions were nearly shipped.

| `gl_wallcache` | p50 | render |
|---|---|---|
| 0 (off) | 5.122 ms | 5.06 ms |
| 1 (cache) | 5.828 ms | 5.75 ms |
| 2 (replay, validation skipped — unsafe diagnostic) | 5.835 ms | 5.77 ms |

**Both numbers were measuring a leak, not a design.** Storage grew without bound: a capture that
turned out uncacheable stranded its walls, and because `inArea` disqualified nearly every seg on
nearly every map, virtually all of them were re-captured *every frame*. It reached **47 GB** and
killed the process in the user's hands. The "isolation" diagnostic was also wrong -- mode 2 skipped
the stamp *comparison* but still built the stamp, so it never isolated what it claimed to.

Three lessons, all of which cost time:
1. **Verify the control actually took effect.** A later decomposition attempt toggled
   `gl_render_walls/flats/things`, which are `CVAR_DEBUGONLY` and *do not exist in a release build*
   -- four measurements of nothing, reported by `ui exec` as `{"executed": true}` either way.
2. **Check the hit rate before trusting a cache A/B.** At 98.6% the test is fair; near zero it is
   measuring nothing.
3. **Use `stat rendertimes`.** It attributes the frame directly and would have pointed at wall Setup
   (0.979 ms) on the first day instead of the third attempt.

### P0 — Budget measurement **← DONE**

`features/levelmesh/` with a pure `surfacebudget_compute` unit (19 tests) and a `fua_levelmesh_stats`
console command that walks `sides[]`/`subsectors[]` and reports the worst-case fixed-range cost.
Nothing renders from it; no engine hooks are wired.

*Shipped:* the measurement that killed the fixed-vs-pooled question (see Risks). Zero risk.

Still outstanding from P0's original scope, deferred into P2b where they actually matter: building
the mesh itself in `gl_PreprocessLevel`, and validating watertightness / absence of degenerate
triangles.

### P1 — Flats read from the mesh

Repoint `FFlatVertexBuffer` at the mesh's flat surfaces instead of building its own. This is
deliberately the easy half — flats already work this way, so it is mostly proving the range and
dirty-drain plumbing on known-good ground.

*Ships:* identical rendering, one fewer buffer.

### P2 — Walls read from the mesh **← the load-bearing phase**

**P2a — move 3D-floor light banding to the fragment shader. DEMOTED: no longer a prerequisite.**

Measured with `fua_levelmesh_stats` (2026-08-16) across Sunder MAP03/10/14/20: **zero sectors with 3D
floors, zero light-banded sides, deepest lightlist 0.** `SplitWall` is never called on any of them,
because Sunder is Doom-format and 3D floors need Hexen/UDMF. So on precisely the maps where the
framerate hurts, the per-side vertex count is *already* static and the fixed-range design needs
nothing from P2a.

Revised approach: P2b reserves a bounded band count per side, and any side whose front sector has
`lightlist.Size() > 1` stays on the existing `SplitWall` path. That keeps the risky shader change --
the one step the pixel-identity gate cannot cover -- off the critical path entirely.

P2a becomes a later optimization aimed at 3D-floor-heavy ZDoom/Zandronum mods (the catalogue's own
content is the case to measure), not a gate on the mesh. The band-selection math
(`computation/lightband_compute`, 14 tests) is already written and stays valid for when it is taken.

**Caveat worth stating**: Sunder is one wad family and a vanilla-format one. Before assuming light
banding is rare in general, re-run the stats on a catalogue mod that actually uses 3D floors.

**P2b — weld and bake.** Drop the `gl_RecalcVertexHeights` dedupe, bake the split vertex set at build
time. `GLWall::Process` stops emitting vertices and instead resolves to a surface ID + a Z patch.
`gl_vertex.cpp`'s four split functions become a build-time step and are deleted.

**Acceptance is pixel-identity**, not "looks right" — the same map, same position, same frame, byte-
compared. Where it differs, the mesh is wrong.

*Ships:* most of Ironwail's CPU win, still on the current GL context, still on macOS. If the plan
stopped here it would already have been worth doing.

### P3 — GPU-driven, GL 4.3

Add `4.3` to the top of the core chain in `ComputeGLContextRequests`
(`features/hwrender/computation/glcontext_compute.h`); macOS keeps 4.1 and the P2 path as its
fallback. Then, one dispatch at a time:

0. precompute the render-hack candidate set at build time, so the hack system survives losing the
   CPU BSP walk (see Resolved §1)
1. compute cull → visible surface list (PVS analogue: `mapsection` + frustum + backface)
2. compute → index buffer + indirect draw commands
3. `glMultiDrawElementsIndirect` for the world
4. bindless materials (`ARB_bindless_texture`, optional with a bound fallback — Ironwail's pattern)

Each is separately revertable behind a cvar. Note P3 does **not** reach zero per-frame CPU geometry
work: the stencil flood path is view-projected and stays on the CPU by nature (Resolved §1). It is a
small residual on a small candidate list, but it does not go away.

*Ships:* draw calls collapse from thousands to a handful on 4.3 hardware.

### P4 — Lightmap atlas

Chart-pack the mesh into a lightmap atlas (`ComputeChartPacking`, texel density by surface area).
This is a prerequisite for **any** GI, baked or realtime, and is worth landing before choosing a
lighting technique.

### P5 — Lighting

Deferred deliberately. The candidate is **radiance cascades** rather than hardware RT, because it
needs only compute and therefore runs identically under MoltenVK later, whereas
`VK_KHR_ray_query` does not. Doom's near-2.5D geometry is an unusually good fit. Decide after P4.

## The backend swap — END TO END ON SUNDER, and benchmarked

`features/hwrender/diligent/` — DiligentCore vendored (Apache-2.0, `bb821b7`), Vulkan only, behind
`-DFUA_DILIGENT=ON` (default off).

**Sunder MAP10 renders through Diligent**, from geometry produced by `features/levelmesh` —
24,558 vertices handed over as plain interleaved position+uv, with the backend knowing nothing about
`GLWall`, draw lists, subsectors or the BSP. Visually confirmed against the GL window side by side.

### The decisive benchmark

`fua_gl_meshbench` and `fua_diligent_bench` submit **the same vertices, the same number of times,
each GPU-synced before the clock stops** — the only fair comparison available, since nothing else
about a Diligent frame and an engine frame is alike.

| backend | submit-only, 8186 tris |
|---|---|
| OpenGL | 0.0080 / 0.0040 ms |
| Diligent (Vulkan) | 0.0080 / 0.0060 ms |

**Identical, to the limit of the timer.** Both issue one draw call to the same GPU; the API is not
what costs. Diligent's presented figure (~0.19 ms/frame) is swapchain present, not drawing.

**This closes the question the whole P2/P3 line was circling:** the backend swap is not a performance
change. It buys portability (Metal via MoltenVK, where GL is capped at 4.1 with no compute) and ray
tracing. Every millisecond still on the table is CPU visibility work — BSP walk and clipper — which
no backend API touches and only GPU culling removes.

## The backend swap — milestone 1 detail

`features/hwrender/diligent/` — DiligentCore vendored at `deps/DiligentCore` (Apache-2.0, `bb821b7`),
Vulkan only, behind `-DFUA_DILIGENT=ON` (default off). `fua_diligent_probe` creates a **Vulkan 1.4
device on the RTX 5080 inside the running engine**, and the GL renderer keeps rendering afterwards.

Three integration problems found and fixed: C++17 needed for Diligent's aligned-new (confined to the
one TU), static-vs-dynamic CRT mismatch (`LNK2038`), and `CMAKE_MSVC_RUNTIME_LIBRARY` not applying
because Diligent's `cmake_minimum_required` resets `CMP0091`.

**Stated plainly: the swap is not a frametime win.** The remaining CPU is the visibility decision
(BSP + clipper, ~0.85 ms) and per-draw state; a different backend API touches neither. The swap buys
portability (Metal via MoltenVK, where GL is capped at 4.1 with no compute) and a path to ray
tracing. The frametime win is GPU culling + indirect draws, reachable in GL 4.3 without swapping.

Milestones: 2 swapchain, 3 triangle, 4 the 2D backend seam, 5 scene geometry consuming the baked
ranges `features/levelmesh` already produces.

## If and when Vulkan

`DiligentGraphics/DiligentEngine` (Apache-2.0, active) is the on-ramp — Vulkan/D3D12/Metal behind one
API, with bindless and ray tracing, at roughly D3D11-level difficulty rather than raw-Vulkan
difficulty. Apache-2.0 is GPL-3 compatible, so it does not endanger the fork's licensing position.

**Nothing before P5 depends on this decision**, and it should not be made early. A Vulkan backend
bolted onto the *current* architecture would be slower than the GL renderer, because Vulkan punishes
per-draw state churn harder than GL does. The mesh is what makes any backend fast; the backend choice
is a consequence, not a cause.

## Risks

- **P2 is the whole plan.** If welded wall geometry cannot reach pixel-identity, everything after it
  is blocked. Budget accordingly and treat any pixel difference as a defect, not a tolerance.
- **P2a is a behaviour change, not a refactor.** Moving 3D-floor light banding from geometry to the
  fragment shader (Resolved §3) changes per-pixel results at band boundaries — geometric splitting is
  exact at the seam, shader evaluation is per-fragment. Expect small differences on 3D-floor maps and
  hold P2a to its own visual review; it is the one place the P2 pixel-identity gate cannot apply.
- ~~**Vertex budgeting still needs measurement.**~~ **RESOLVED — fixed ranges are affordable.**
  Measured with `fua_levelmesh_stats` on three Sunder maps (2026-08-15):

  | map | sides | sectors | subsectors | wall verts | wall MB | +flats | mean/side | worst side |
  |---|---|---|---|---|---|---|---|---|
  | MAP10 | 26,691 | 1,420 | 10,958 | 1,168,072 | 22.28 | 24.37 MB | 43.8 | 144 |
  | MAP14 | 96,826 | 5,643 | 47,176 | 3,658,070 | 69.77 | 78.64 MB | 37.8 | 126 |
  | MAP20 | 114,407 | 13,467 | 48,580 | 4,535,032 | 86.50 | 96.35 MB | 39.6 | 306 |

  Three things settle it. **The totals are small**: ~96 MB of worst-case reservation on Sunder MAP20,
  one of the largest Doom maps in existence — and for scale, `FFlatVertexBuffer` *already* allocates
  `BUFFER_SIZE` = 2,000,000 vertices = **40 MB** as a per-frame scratch buffer, so MAP10's entire wall
  mesh is smaller than a buffer the engine allocates today. **The mean is stable** across a 4x range
  of map size (37.8–43.8 vertices/side), so cost scales linearly with side count, not superlinearly.
  **The tail is tame**: the worst side is 3–8x the mean, not 100x, so the pathological 3D-floor
  stacking the fixed-range design feared does not materialize on real maps.

  The two-tier fixed/pooled fallback is therefore **dropped**. Plain fixed ranges everywhere.
- **Render hacks are understood but not free** (Resolved §1). Sector hacks fold into the mesh as
  annotations; the stencil flood does not and never will, so P3 keeps a CPU residual. The schedule
  risk here is now bounded, but the flood path must be kept working through every phase — it is the
  thing most likely to break silently, since it only shows up on specific maps at specific view
  heights.
- **macOS diverges further.** After P3, Mac runs a materially different path from Windows/Linux.
  That is already true today at the context level, but it widens, and it doubles the manual E2E
  verification surface.
- **Two-renderer drift.** Explicitly guarded against by the P2 pixel-identity gate. If that gate is
  ever waived, this plan has become hwrender.

## Resolved

The four questions this plan opened with, answered from the tree.

### 1. Render hacks — split decision: most reuse existing surfaces, one cannot

`gl_renderhacks.cpp` manufactures two different things and they get opposite answers.

**Sector hacks reuse existing geometry.** `AddOtherFloorPlane`/`AddOtherCeilingPlane` (`:111`, `:124`)
attach a `gl_subsectorrendernode` to a sector; the draw path is `DrawSubsector(node->sub)`
(`gl/scene/gl_flats.cpp:283`) — that is *the subsector's own flat vertices*, drawn with a substituted
plane and sector. Deep water (`HandleHackedSubsectors`, `:949`), fake bridges, and sector stacks
(`ProcessSectorStacks`, `:1143`) all work this way. **No new geometry exists.** → these become a
per-frame draw annotation `(surfaceID, substitute plane, substitute sector)`, not new surfaces.

**Flood gaps cannot be surfaces.** `DrawFloodedPlane` (`gl/scene/gl_drawinfo.cpp:1082`) builds its quad
by projecting the wall segment onto the plane *from the camera*:

```
prj_fac1 = (planez - fviewz) / (ws->z1 - fviewz);
px1 = fviewx + prj_fac1 * (ws->x1 - fviewx);
```

The quad is different every frame even when nothing in the world moved, and it is drawn inside a
stencil mask. It has no world-space existence. → **stays a per-frame side channel, permanently.**

**The real risk this exposed is upstream of both.** The hack system's *inputs* are collected during
the CPU BSP walk — `AddUpperMissingTexture` is called from `GLWall::Process` — and its decisions are
keyed on `viewz` (`:673`, `:704`). Under P3 there is no CPU BSP walk, so the whole system loses its
host.

Resolution: **the candidate set is static.** "This side has no upper texture over a two-sided gap" is
a map property, not a frame property. Precompute the `(side, subsector)` candidate list at build time;
per frame, filter it by mapsection visibility and `viewz`. Invalidate on `side_t::SetTexture`. That is
a small pass over a small list and needs no BSP walk. Only the flood draw itself stays per-frame.

### 2. Dirty set — push, and the choice is forced

Today it is polled *lazily*: `mVBO->CheckUpdate(sector)` is called from `DoSubsector`
(`gl/scene/gl_bsp.cpp:403`) only for sectors the walk reaches, guarded by `validcount`.

Under P3 the CPU does not know what is visible, and the mesh must be correct **everywhere** before the
cull dispatch runs — so lazy-on-visit is not merely suboptimal, it is impossible. Push wins by
default, and cheaply: the hooks already exist, so cost is O(sectors that actually moved) rather than
O(all sectors).

Drain point: immediately after `R_SetupFrame` (which is where `DoInterpolations` runs,
`r_utility.cpp:895`) and before the draw. Note the interpolate→render→restore cycle re-dirties every
mover each frame; that is correct and expected, not a bug to design around.

### 3. Vertex budget — fixed-per-side is viable, but only after light banding moves to the shader

Piece count per side is 3 (top/mid/bottom) plus 2 per 3D-floor block. That is nearly static, and
pieces that are absent this frame (a closed door has no upper) collapse to zero-area triangles, which
cost essentially nothing on the GPU. Per-piece vertex count is likewise static once the
`gl_RecalcVertexHeights` dedupe is dropped: 4 + the two edge heightlist counts (from
`vertex_t::numsectors`, fixed at load) + the two seg counts (`sidedef->numsegs`, fixed at load).

The one thing that breaks it is `SplitWall` (`gl/scene/gl_walls.cpp:264`), which multiplies every
piece by `frontsector->e->XFloor.lightlist.Size()` — and `lightlist` is **rebuilt every frame** by
`P_Recalculate3DFloors` for sectors with moving planes. That alone makes the budget unbounded.

Resolution: **move 3D-floor light banding from geometry into the fragment shader.** `lightlist_t`
(`p_3dfloors.h`) is just `{plane, p_lightlevel, extra_colormap, blend, flags}` — a small array the
shader can walk by pixel Z, which is structurally identical to the glow-plane evaluation the renderer
already does in `main.fp`. That deletes `SplitWall` and `Put3DWall`, removes the dominant per-frame
wall-splitting cost, and makes the per-side budget static. It is a prerequisite for P2, not an
optimization.

### 4. 3D-floor IDs — key on `master`, never on `F3DFloor*`

`P_Recalculate3DFloors` (`p_3dfloors.cpp:427`) `delete`s and re-`new`s every `FF_DYNAMIC` rover each
time it runs, and it runs **every frame** for sectors with moving attached planes (via
`P_RecalculateAttached3DFloors` from the interpolation path). `F3DFloor*` is therefore not stable
across frames and must not be an ID.

But a dynamic rover is a *copy* of an original (`*dyn = *pick`, `:520`), and originals are created
once in `P_Spawn3DFloors` through `P_Add3DFloor(sec, sec2, master, flags, alpha)` (`:112`) where
`master` is a `line_t*` — stable for the level's lifetime.

Resolution: key 3D-floor surface IDs on `(side_t*, master line_t*, part)`. Runtime splits become
sub-ranges within the original rover's reserved budget, never new surfaces.

---

## Measured: what the GPU actually costs (Diligent/Vulkan backend, RTX 5080, 624x361)

This is the section that changes the plan, so it leads with the numbers.

`fua_diligent_scale` redraws the uploaded scene N times per frame and times it with a Vulkan
timestamp query. Sunder MAP10, full-level static bake, 39,957 triangles in 55 material batches:

```
    1x =    39957 tris,   55 draws -> GPU  0.0129 ms
    2x =    79914 tris,  110 draws -> GPU  0.0198 ms
    5x =   199785 tris,  275 draws -> GPU  0.0393 ms
   10x =   399570 tris,  550 draws -> GPU  0.0736 ms
   25x =   998925 tris, 1375 draws -> GPU  0.1785 ms
   50x =  1997850 tris, 2750 draws -> GPU  0.3520 ms
  100x =  3995700 tris, 5500 draws -> GPU  0.6971 ms
  => ~5.7M triangles per GPU millisecond
```

Perfectly linear, no knee. **The entire visible world of one of the heaviest maps ever built for
Doom costs 0.013 ms of GPU time.** The frame budget at 60 fps is 16.7 ms.

Three conclusions follow, and they were not what the port set out to find.

**1. The GPU was never the bottleneck, by two orders of magnitude.** The renderer measured at ~4 ms
CPU against 1.74 ms GPU at the start of this work, and that 1.74 ms was almost entirely swapchain
present, not shading — the actual draw work is 0.013 ms. Every millisecond worth having is on the
CPU side.

**2. The backend swap does not pay for itself.** Matched benchmark, same geometry, same viewpoint:

| | submit cost |
|---|---|
| GL (`fua_gl_meshbench`) | 0.0033 – 0.0133 ms/frame |
| Diligent/Vulkan (submit-only) | 0.0167 – 0.0300 ms/frame |

Diligent is **3–9x slower to submit** the same draws. That is not a Diligent defect — it is a
general-purpose abstraction over five APIs with per-draw state validation, against a GL path
specialised to this engine. Vulkan buys explicit control that would matter if the GPU were saturated,
and the GPU is at 0.08% utilisation.

**3. The win is the level mesh, and it is backend-independent.** The BSP walk, the clipper and the
per-frame geometry generation exist to avoid sending the GPU work it does not need saving from. A
whole-level static bake drawn unconditionally costs ~0.013 ms GPU and ~55 draws. Deleting the culling
that avoids it is worth ~0.85 ms of CPU *on the existing GL renderer*.

### Status of the Diligent port

Working end to end on Sunder MAP10, live from the player's camera (`fua_diligent_live 1`):

- walls, flats, full-level static bake (`fua_levelmesh_bakeall`)
- real materials via `FMaterial::CreateTexBuffer`, mipmapped, trilinear min / point mag
- the engine's real lighting: `R_DoomLightingEquation` and the `getLightColor`/`applyFog` pair
  transcribed from `main.fp`, both fog distance modes, coloured fog, software-lightmode path
- one draw per material (material-keyed vertex buffer; light and fog ride the vertex)
- GPU timestamp benchmarking and a scale probe

Not ported: sky, HUD/2D, menus, console, weapon and actor sprites, dynamic lights, translucency and
render styles, portals/mirrors/skyboxes, 3D floors, decals, particles, models, automap, texture
animation, translations, warp and brightmap shaders, and taking over the engine's own window.

Given the measurements above, finishing that list is not recommended as a performance play. It is
weeks of work whose measured payoff on this hardware is negative on the CPU and irrelevant on the
GPU. The port's real value has been as an instrument: it is what proved the GPU is idle, and that is
what tells us where the remaining milliseconds actually are.

### Recommended next step

Draw the whole baked level on the **existing GL renderer** and delete the culling that feeds it —
BSP walk, clipper, per-frame `GLWall::Process`. The measurement says the geometry is free; the
machinery that avoids sending it is not.

### Traps worth remembering

- `GLWall::Process` builds its wall from the **linedef's** endpoints, not the seg's. The engine's
  per-linedef `validcount` is what stops a BSP-split line emitting one full-length copy per fragment.
  A bake pass cannot use `validcount` (it drops the second side of every two-sided line) and must not
  simply bypass it (coplanar duplicates render as z-fighting noise) — it needs a per-**sidedef**
  guard. See `ClaimSideForBake`.
- Sprites must not go in a static mesh. A sprite quad is built facing one viewpoint, so baked
  geometry stops facing the player the moment they turn. This looked correct for a long time because
  it was only ever checked against a screenshot taken from the camera the bake ran from.
- `presented` ms/frame is not a renderer measurement. It swung 1.68 -> 0.27 ms between two runs of
  identical geometry purely on whether the backend window was occluded. Use the timestamp query.
- Vulkan clip space is `[0,1]` in Z, not GL's `[-1,1]`. A GL-form projection mostly works and quietly
  puts the ported lighting equation's `gl_FragCoord.z` bands in the wrong places.

### Regression check (Sunder MAP10, `fuactl capture`, 400 frames)

| state | p50 | p99 | render mean |
|---|---|---|---|
| A: default (wall cache on) | 4.112 ms | 5.576 ms | 4.09 ms |
| B: `gl_wallmesh 1` | 9.939 ms | 11.548 ms | 9.64 ms |
| C: B + full-level bake | 9.781 ms | 11.543 ms | 9.49 ms |
| D: `gl_wallmesh 0` after baking | 4.026 ms | 5.286 ms | 3.96 ms |

A vs D: the bake pass and the `clipper.bakeAll` hooks cost **nothing** in steady state — normal
rendering is untouched (the `bakeAll` early-outs are all `false` in a normal frame).

B vs C: baking the whole level rather than one room is also free. Mesh size does not drive frametime.

A vs B is the real finding, and it re-confirms P2c/P2d from a new angle: **`gl_wallmesh`'s GL draw
path is 2.4x slower than the streaming path it replaces.** Storing wall geometry CPU-side and drawing
it from a static VBO loses to `FFlatVertexBuffer`'s persistent-mapped streaming, again. `gl_wallmesh`
remains off by default and is a bake/feed switch for the backend, not an optimisation.

---

## Correctness pass: the Vulkan render now matches GL

Four bugs, three of which produced *plausible* pictures — which is why they survived so long.

### 1. One texture for the whole world (Diligent SRB misuse)

The draw loop was:

```cpp
for (batch : batches) { var->Set(srv); ctx->CommitShaderResources(srb); ctx->Draw(...); }
```

over a single shared SRB. A MUTABLE shader variable is baked into the SRB's descriptor set when it is
committed; re-setting it between draws in one command buffer does **not** produce a new set, so every
batch sampled whichever texture the set last held. Fix: one SRB per material, created once at upload.

Doom levels reuse textures heavily, so "everything is one texture" still looked like a Doom level.

### 2. Red/blue swapped and 4x too bright — in the *screenshot only*

The readback hard-coded a BGRA→RGB swap and Diligent's default swapchain is sRGB. The swapchain
actually came back RGBA (format 28), so the swap *created* an inversion, and the sRGB target stored
values numerically far brighter than the shader wrote.

Both were invisible in the window and only affected the PNG — so every GL-vs-Vulkan comparison was
wrong, and the resulting hunt for a lighting bug found nothing because there was no lighting bug.

Fixes: pick channel order from `bd.Format`; force `ColorBufferFormat = RGBA8_UNORM` to match the
engine's own framebuffer.

Measured after the fix (mean RGB over the same patch, `fuactl png --diff`):

| region | GL | Diligent |
|---|---|---|
| floor | 20.8, 17.3, 12.4 (lum 17.8) | 20.6, 16.5, 11.0 (lum 17.1) |
| wall  | 14.1, 10.0, 4.9 (lum 10.7) | 16.1, 11.4, 6.0 (lum 12.2) |

**The lighting was correct the whole time.** It is now confirmed correct against the GL renderer
rather than asserted.

### 3. Lighting was being re-derived instead of recorded

The backend reimplemented `gl_SetColor`/`gl_SetFog`. That silently dropped `rellight` (fake contrast),
`getExtraLight()`, desaturation and blendfactor. Now `CaptureShading` (staticmesh.cpp) runs the
engine's own functions at bake time and records the *result* into `MeshPiece`. There is one
implementation of Doom lighting and the backend reads its output.

### 4. No sky

Sky is not geometry in a Doom level — it is the absence of it. Sky walls and sky flats go to the
portal manager and never reach the mesh, so the world sat under a flat void.

Ported `FSkyVertexBuffer::SkyVertex`'s dome (same 60° max side angle, 10000-unit radius, mirrored X,
+300 nudge, UV convention) with `RenderDome`'s per-texture-height transform baked into the vertices.
Drawn first with depth test and depth write off, so the world paints over it — which makes sky flats
work for free.

Also fixed: the projection hard-coded a 16:10 aspect, so the backend framed the world differently
from the engine and screenshot pairs were not comparable.

### Where it stands

Sunder MAP10, full-level bake, live from the player's camera:

```
123075 verts (41025 tris), 54 material batches, 55 textures
submit-only 0.0233 ms/frame, GPU 0.0155 ms/frame
scale probe: 100x = 4102500 tris, 5400 draws -> GPU 0.7270 ms  (~5.6M tris/ms)
```

Coverage: 12659 of 33249 drawable segs (one wall per *sidedef* — several segs share one, so this
undercounts surfaces), 12982 wall pieces, 4522 flat pieces, 613 segs permanently uncacheable.

Still missing: HUD/2D, menus, console, sprites (removed from the static mesh — they are billboards
and belong in a per-frame dynamic stream), dynamic lights, translucency and render styles,
portals/mirrors/skyboxes, 3D floors, decals, particles, models, automap, texture animation,
translations, warp and brightmap shaders, and taking over the engine's own window.

### Tooling added, because guessing cost more than measuring

All of it is `fuactl` subcommands. It was eight shell scripts beside the tool for a while, which put
the paths a human actually uses in the one place with no tests — the argument-ordering bug that
shipped a "no monsters" build full of monsters lived there, under a comment claiming the opposite —
and every check needing a shape the eight did not have got improvised as a ninth.
`tools/fuactl/test/capture.test.mjs` now fails if a `.sh` reappears in that directory.

- `fuactl build` — builds and stages, failing loudly. Two measurements were taken against a **stale
  binary** because the build ran inside a shell one-liner ending in a copy, so the exit status came
  from the copy and a compile error scrolled past as ordinary output.
- `fuactl shot` / `fuactl sweep` / `fuactl doorshot` — matched GL/Vulkan pairs from one camera: at a
  named spot, across several maps, or with a door caught mid-swing (the only state that shows whether
  moving geometry is tracked at all).
- `fuactl mark` — fire at a junction, find where the mark actually landed, and capture a pair of it.
- `fuactl png` — mean RGB over a region of a PNG. Turned "it looks brighter" into "same hue, 3.8x
  magnitude, R and B transposed", which named the bug immediately.
- `fua_dg_lightmode` debug views: 0 flat, 1 full, 2 depth, 3 depth contours, 4 vertex colour,
  5 fog factor, 6 fog colour, 7 view depth, 8 fog density, 9 fog mode sign, 10 raw texel.
- `fua_levelmesh_stats` now reports mesh coverage and why captures were refused.

### Regression control, done properly

An earlier "baseline" of 4.11 ms and a later 4.75 ms reading looked like a 16% regression from the
bake hooks. It was not — the machine had simply been building for an hour in between. The only
trustworthy control is **the unmodified tree, measured back to back on the same machine state**
(`git stash` the whole branch, rebuild, measure, restore, rebuild, measure):

| tree | p50 | p99 | render mean |
|---|---|---|---|
| HEAD, none of this work | 5.502 / 5.504 / 5.741 ms | ~7.5 ms | 5.48 ms |
| this branch | 4.669 / 4.661 / 4.754 ms | ~7.6 ms | 4.75 ms |

**15% faster than upstream on Sunder MAP10**, and the bake hooks cost nothing in normal play (their
`clipper.bakeAll` guards are all false in a normal frame).

Lesson worth keeping: never compare a number against one taken an hour earlier. Absolute frametimes
on a developer machine drift by more than the effects being measured.

---

## Sprites: a dynamic stream, not the static mesh

Actors are billboards. A sprite quad is built facing one viewpoint, so anything baked into a static
buffer stops facing the player the moment they turn — and a full-level bake leaves every actor on the
map drawn at once from wherever each was first seen. They now go into a **dynamic stream**
(`DynClear`/`DynAppend` in staticmesh.h) that is cleared at the top of every `CreateScene`, appended
to as the engine generates each sprite, and consumed by the backend at the end of the frame. No
keying, no invalidation: the whole thing is thrown away and rebuilt, which is the correct lifetime
for view-dependent geometry.

Drawn after the world with the alpha-tested pipeline and normal depth, so the opaque world fills the
depth buffer first and sprite fragments are mostly rejected rather than shaded. Verified against GL
with a summoned Cacodemon: correct alpha-tested edges, correct lighting, correct occlusion.

Not yet handled: translucent render styles, which need a back-to-front sort and a blend state.

Cost in the default configuration (`fuactl capture`, Sunder MAP10): 4.76–4.90 ms p50, against
4.67–4.75 ms before sprites existed. Collection is free at this scale.

### Backface culling: measured, and reverted

Culling looks like free performance and is not, yet. Flats are triangle fans over a subsector's
edges, and a floor and a ceiling built the same way have **opposite winding** — one is viewed from
above, the other from below. The engine draws flats with culling disabled for exactly this reason.

`CULL_MODE_BACK` culled every floor on Sunder MAP10 and the sky poured through the hole. The sampled
floor patch went from (20.2, 16.0, 10.3) — matching GL's (20.8, 17.3, 12.4) — to **(173.5, 0.5, 0.2)**,
solid red sky. `CULL_MODE_FRONT` culled nearly everything (lum 4.1).

Default is back to `fua_dg_cull 0`. Making culling work means normalising winding at bake time
(reverse the fan for ceilings, and check walls the same way). Worth doing — it would roughly halve
flat fragment work — but it is an optimisation, not a prerequisite, and the cvar keeps the experiment
one command away.

### Two more traps

- **The screenshot used a stale camera.** `fua_diligent_shot` drew with whatever MVP
  `fua_diligent_scene` had snapshotted, so a shot taken after the player turned showed the room they
  *used* to be looking at. This made Doom 2 MAP01 look like it was missing half its geometry when it
  was simply facing the other way, and cost a long detour. `SceneScreenshot` now rebuilds the MVP.
- **Vulkan's dynamic heap is per frame, not per draw.** The sprite buffer was mapped with
  `MAP_FLAG_DISCARD` inside `DrawSceneOnce`, which the benchmark calls 300 times between frame
  boundaries — 300 x ~420 KB against an 8 MB heap, and the run died with "Space in dynamic heap is
  exhausted". The stream is now rebuilt and uploaded once per *engine frame*, gated on a generation
  counter (`DynGeneration`), using `USAGE_DEFAULT` + `UpdateBuffer` rather than the dynamic heap.

### Where it stands now

Sunder MAP10, full-level bake, live from the player's camera, matched against GL:

| region | GL | Diligent |
|---|---|---|
| floor | 20.8, 17.3, 12.4 | 20.2, 16.0, 10.3 |
| wall  | 14.1, 10.0, 4.9  | 15.2, 11.0, 5.9  |

```
123075 verts (41025 tris), 55 batches, 117 textures
submit-only 0.0267 ms/frame, GPU 0.0165 ms/frame
100x = 4102500 tris, 5500 draws -> GPU 0.7265 ms   (~5.6M tris/ms)
```

Done: walls, flats, full-level static bake, real materials with mipmaps, the engine's real lighting,
sky, sprites, live camera, GPU-timed benchmarking.

Left: HUD/2D, menus, console, translucency and render styles, dynamic lights, portals/mirrors/
skyboxes, 3D floors, decals, particles, models, automap, texture animation, translations, warp and
brightmap shaders, and taking over the engine's own window.

---

## Translucency and texture animation

### Translucency

Sprites were drawn alpha-tested only, so plasma, fireballs, explosions and every specter rendered as
solid quads. `MeshPiece` now carries `blendMode` and `alpha`, classified in `RegisterSprite` from the
actor's `FRenderStyle`:

| mode | source | pipeline |
|---|---|---|
| 0 | opaque / masked | alpha test, depth write |
| 1 | `trans < 1` | src-alpha blend, no depth write |
| 2 | `STYLEOP_Add` + `STYLEALPHA_One` | additive, no depth write |
| 3 | `STYLEOP_Shadow` (fuzz) | blended (approximation — fuzz shaders not ported) |

Four pipelines now share one vertex layout. Blended geometry never writes depth, or a nearer
translucent sprite would occlude the one behind it instead of letting it show through.

Two ordering rules, and they are correctness rather than batching:
- Opaque pieces first, grouped by material. Translucent pieces after, sorted **back to front** —
  blending is not commutative, so overlapping translucent sprites drawn in the wrong order composite
  wrongly. Within the translucent set, sorting beats batching, so material grouping is deliberately
  given up there.
- SRBs are keyed on **(PSO, material)**, not material alone. An SRB is created from a pipeline and is
  only valid with it.

Verified by counting which pipelines actually ran, rather than trying to catch a projectile in a
screenshot (`fua_dg_dynstats`):

```
summon Spectre     -> [opaque 8, translucent 0, additive 0, fuzz 1]
summon PlasmaBall  -> [opaque 7, translucent 0, additive 1, fuzz 1]
+ BaronBall        -> [opaque 7, translucent 0, additive 3, fuzz 1]
```

and then visually: a Spectre renders as a see-through dark shape over the floor while a Cacodemon
beside it stays opaque.

### Texture animation

A baked surface froze on whichever animation frame was showing when it was baked — nukage stopped
flowing, computer screens stopped flickering. `MeshPiece` now records the **base** `FTexture*` (from
the sidedef part or the sector plane), and the backend re-resolves it every frame through
`FMaterial::ValidateTexture(tex->id, false, true)`, swapping only the SRB when the frame changed.

Capturing the *resolved* frame's id would not have worked: in ZDoom an animation frame translates to
itself, so the surface would still be frozen. The base id is the only thing that animates.

Costs one table lookup per batch (~55 on Sunder MAP10). Verified with a counter: 30 → 66 frame swaps
over three seconds on Doom 2 MAP03.

### Regression control, back to back

Absolute frametimes on this machine drifted from ~4.45 ms to ~5.6 ms over the session (continuous
builds), which briefly looked like a 25% regression. It was not. The only trustworthy measurement is
HEAD and branch built and measured back to back:

| tree | p50 |
|---|---|
| HEAD, none of this work | 5.398 / 5.499 / 5.624 ms |
| this branch | 4.549 / 4.693 / 4.570 ms |

**16% faster than upstream**, matching the earlier control (5.50 vs 4.67) — so sprites, translucency
and animation cost nothing measurable. Sprite collection specifically was A/B'd on the live instance:
`fua_mesh_sprites 1` gives 5.15–5.25 ms, `0` gives 5.25–5.30 ms. Free.

**Rule, learned twice now: never compare a frametime against one taken an hour earlier.** Stash the
branch, rebuild, measure, restore, rebuild, measure.

### One more trap

Vulkan's dynamic heap is recycled per **frame**, not per draw. The sprite buffer was mapped with
`MAP_FLAG_DISCARD` inside `DrawSceneOnce`, which the benchmark calls 300 times between frame
boundaries — 300 × ~420 KB against an 8 MB heap, and the run died with "Space in dynamic heap is
exhausted". It is now rebuilt and uploaded once per engine frame, gated on `DynGeneration()`, using
`USAGE_DEFAULT` + `UpdateBuffer` instead of the dynamic heap at all.

---

## The 2D layer: HUD, menus, console, weapon

Everything the player actually reads. The engine draws 2D through `FGLRenderer::DrawTexture` -- one
call per texture, and text is one call per *character* -- plus `Dim`/`Clear` for untextured fills.
Rather than reimplement `DCanvas`, `features/hwrender/hud2d.{h,cpp}` records what each of those calls
*would* draw as a flat list of screen-space quads. Same relationship the level mesh has to the 3D
path: the backend consumes plain data and never sees a `DCanvas::DrawParms`.

Rebuilt from scratch every frame (cleared at the top of `D_Display`). 2D is entirely view-dependent,
so there is nothing to cache and a list that is refilled cannot go stale.

Its own pipeline: orthographic, depth off entirely, blended, scissored. **Submission order is draw
order and is never reordered** -- a 2D layer sorted by texture would put the status bar behind the
world and the console behind the menu.

Verified against GL across the fullscreen HUD, the classic status bar (`screenblocks 10` -- face,
ARMS, ammo table), the main menu, the server browser and the host tab. Mean RGB over the same patch:

| view / region | GL | Diligent |
|---|---|---|
| server list text | 47.4, 49.4, 56.6 | 47.4, 49.4, 56.5 |
| browser detail pane | 30.2, 28.9, 28.3 | 30.2, 28.9, 28.3 |
| menu tab pills | 57.4, 79.8, 68.2 | 58.0, 79.9, 68.3 |
| host panel | 25.2, 26.3, 31.4 | 25.2, 26.3, 31.4 |
| weapon sprite | 110.5, 83.8, 26.3 | 111.8, 84.8, 27.1 |
| menu text | 96.1, 24.8, 3.0 | 95.5, 24.3, 2.7 |

### Four bugs, and what each looked like

**1. The garbled HUD was a window-size mismatch.** `CreateWindow` takes the *outer* rect, so asking
for 640x400 gave a 624x361 client area -- and the backend then rendered the engine's 640x480 2D layer
into it. The HUD came out squashed to 75% height and point-sampled, which made small text
unreadable. `AdjustWindowRect` plus sizing the client area to `screen->GetWidth()/GetHeight()` fixed
it, and it also made every GL/Vulkan screenshot pair directly comparable for the first time.

**2. The status bar rendered along the top, mirrored.** Diligent's Vulkan backend renders with a
negative-height viewport so GL- and D3D-style projections both come out upright, which means the
shader must hand it **GL-style NDC** (+1 at the top). Passing Vulkan-native NDC flipped the layer.

**3. Fonts were dark and muddy -- a translation SIGN.** ZDoom draws coloured text by handing
`DrawTexture` a palette remap. The GL path passes the index **negated**, because it applies the remap
in a shader from a palette texture. `FGLBitmap::CopyPixelData` only honours a *positive* value
(`if (translation > 0)`), so feeding the negated one to `CreateTexBuffer` silently produced
untranslated pixels: the entire server browser rendered in the base Doom font's dark red instead of
white. The texture cache is now keyed on **(material, translation)** and passes the positive index.

The tell was the asymmetry -- graphic menu items, which carry no translation, looked perfect, while
everything drawn as *text* was wrong.

**4. Untextured fills were missing entirely.** `Dim` and `Clear` draw the menu backdrop and the pill
behind every menu tab. Without them the tab captions -- which are DARK text meant to sit on a LIGHT
pill -- landed straight on the dark world and read as "the text isn't working". Recorded as quads
with `material == NULL`, which the backend binds to its white texture.

Also ported: `TexMode` (`TM_MASK`/`TM_OPAQUE`/`TM_INVERSE`/`TM_REDTOALPHA`/`TM_CLAMPY`) transcribed
from `getTexel()` in main.fp, and the weapon sprite -- which is already a screen-space quad, so it
belongs in the 2D capture rather than the sprite stream.

### Regression control, back to back

| tree | p50 |
|---|---|
| HEAD, none of this work | 5.376 / 5.361 / 5.383 ms |
| this branch | 4.075 / 4.099 / 4.050 ms |

**24% faster than upstream.** The 2D capture runs on every `DrawTexture` in the default GL path and
costs nothing measurable.

### Where it stands

Done: walls, flats, full-level static bake, materials with mipmaps and translations, the engine's
real lighting, sky, sprites, translucency (normal/additive/fuzz), texture animation, the 2D layer
(HUD, status bar, menus, server browser, weapon), live camera, GPU-timed benchmarking.

Left: dynamic lights, portals/mirrors/skyboxes, 3D floors, decals, particles, models, the automap,
warp and brightmap shaders, and taking over the engine's own window (which requires creating it
without a GL context).

---

## Dynamic lights — and a camera bug they exposed

### The camera was wrong at every angle except zero

Chasing why the lights looked oversaturated turned up something much worse. `BuildMVP` built its view
rotation from `ry = 270 - yaw`, which makes the error scale with **2*yaw**: correct at yaw 0, and a
full 180 degrees out at yaw 90.

Sunder MAP10's player start faces angle 0. Every GL-vs-Vulkan screenshot comparison in this document
up to this point was taken there, matched pixel-for-pixel, and the bug stayed invisible. It only
surfaced on Doom 2 MAP01 — which starts facing north — where the two renderers showed **completely
different rooms while both were paused on the same frame**.

Replaced with a derived look-at. Doom angles are 0 = east (+x), 90 = north (+y); the mesh is
(x, z-up, y), so forward is `(cos a, 0, sin a)` and for a right-handed view down -Z with +Y up:

```
s    = normalize(cross(f, up)) = (-sin a, 0,  cos a)
u    = cross(s, f)             = ( 0,     1,  0)
row2 = -f                      = (-cos a, 0, -sin a)
```

MAP01 now matches GL exactly: (40.2, 32.8, 23.3) against (41.4, 34.0, 24.1), identical hue. Sunder
still matches too.

**The lesson is about the test, not the code.** One map's spawn angle happened to sit on the single
value where a wrong formula is right, and that was enough to certify it repeatedly. A second map
found it in one shot. `fua_diligent_shot` now prints `cam=(x, y, z) yaw=` so "are these two images
of the same viewpoint" is answerable with numbers instead of by eye.

### The lights themselves

The engine builds a per-surface list of lights and hands the shader an index into it. That index is
**rebuilt every frame**, so it cannot live in a static mesh: baking it produced a world where dynamic
lights simply never appeared, because every piece had recorded -1 on the frame it was baked.

Rather than re-upload geometry every frame to keep an index fresh, the whole mechanism is dropped.
The buffer holds *every* active light (straight off `TThinkerIterator<ADynamicLight>`, with
gl_GetLight's own filters) and the fragment shader tests each one. That machinery exists to save GPU
work, and the scale probe measured this GPU at 0.018 ms/frame for the entire visible world -- about
0.1% utilisation. There is nothing to save, and it is the shape a clustered forward renderer wants
next anyway.

`MeshPiece` gained a surface normal so the shader can do gl_GetLight's side test (a light behind a
surface does not light it) — walls from the seg direction, flats from the plane, both in mesh space.

Verified by A/B on a fixed camera with the sim paused: contribution-only view reads
(204.7, 61.3, 0.4) for a red torch, and the scene goes from lum 4.6 unlit to 27.3 lit.

**Known deviation:** the light is stronger than GL's — a torch adds +12.3 red where GL adds +3.4.
GL applies a light only to surfaces in its own subsector's light list, a spatial cull this does not
do, so lights reach further here than they should. Correct next step is that spatial cull, not a
fudge factor.

### Regression control, machine idle

The machine was busy with unrelated work during an earlier attempt at this control (HEAD 8.7 ms,
branch 7.4 ms — both inflated, and the sim time more than doubled with no sim changes, which is what
gave it away). Re-run once idle:

| tree | p50 |
|---|---|
| HEAD, none of this work | 5.93 / 5.63 ms |
| this branch | 4.21 / 4.20 ms |

**~28% faster than upstream**, with the dynamic-light path and its per-frame light-buffer mirror
active. Consistent with the four earlier controls (16%, 16%, 24%, 28%).

### The world was mirrored, and mean-colour tests could not see it

Reported as "the sprites look flipped". It was the camera: the whole scene was mirrored horizontally,
and a mirrored monster is simply the most recognisable thing on screen.

The mesh is `(x, z-up, y)` — that is `(east, up, north)` — and `east x up = -north`, so it is a
**left-handed** basis. The textbook right-handed look-at I had just derived is therefore wrong here:
`s = cross(f, up)` put the camera's right hand to the west. The correct basis for this handedness is
`s = cross(up, f)`:

```
s    = cross(up, f) = ( sin a, 0, -cos a)    facing north, right is east
u    =                ( 0,     1,  0)
row2 = -f           = (-cos a, 0, -sin a)
```

Its determinant is -1, which is exactly the reflection needed to take left-handed mesh space into the
right-handed eye space the projection expects.

**Why every previous check missed it.** Mirroring a roughly symmetric Doom corridor barely moves the
mean colour of a centred patch, so the numeric comparisons that had been certifying this port kept
passing. The camera-angle bug fixed just before it was found the same way — by looking at a picture,
on a map that happened to break the symmetry.

The fix for the method, not just the code: compare **asymmetric** regions. With a Cacodemon on the
left and a corridor on the right, a mirror is unmissable:

| region | GL | Diligent |
|---|---|---|
| left quarter (monster) | 40.3, 2.7, 1.9 | 39.6, 3.7, 3.0 |
| right quarter (corridor) | 18.6, 15.1, 9.8 | 18.5, 15.0, 9.7 |

Mirrored, the left quarter would have read as corridor and the right as monster. `fuactl png` takes
a region precisely so this kind of check is one command.
