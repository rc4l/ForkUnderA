# features/levelmesh

A persistent, indexed world mesh that is **patched where the sim changed it** instead of rebuilt from
scratch every frame. The full design, phasing and rationale live in `docs/levelmesh-PLAN.md`; this
README covers what is actually in the tree and which engine hooks it touches.

## Why

Measured on Sunder MAP10 (640x400, spawn): the frame is 5.83 ms p50, of which **0.28 ms is
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

`MarkSlotBits` and `NoteSlotMarked` set a walk slot's bits from several lanes, each owning whole words, and
keep the ids for the slot's next start; `CapturePolyColor` is `gl_SetColor`'s colour for a polygon.

`LightListBottom` (`levelmesh.h`) is a 3D floor light list entry's lower edge at a point, for the sprites,
the weapon and the screen tint alike.

## The frame's pieces

The level is baked from the map (`wallcache.cpp`, `flatmesh.cpp`), whole at load and again where a walk
reaches something that moved, relit or scrolled; GL's walk fills nothing. GL keeps its own replay cache of
`GLWall`s in `gl/scene/gl_wallcache.cpp`, keyed on the same dirty counters.

The dynamic stream is refilled each frame for each view: sprites, models and particles from
`features/sprites`, projected decals (`projdecals.cpp`), and with `fua_decalmode 0` the decals glued to
walls (`walldecals.cpp`). Those are built for the sidedefs each view's walk saw, on each plain wall piece
GL would draw them on, a light band at a time: `computation/walldecal_compute` is `GLWall::DrawDecal`'s
height, quad and clipping, and `DerivedPartBands` the bake's own band split and light. A stacked view's or
mirror's pieces are kept for that view's pass (`StackCaptureSlot`, `MirrorCaptureView`). Decals on a 3D
floor's face, on a mirror's sheen and on a mid texture GL draws through a sky (`GLWF_SKYHACK`) are not built
yet. Scenario `walldecal`.

## In-engine hooks

- `features/frame/levelrenderer.cpp` `FLevelRenderer::PreprocessLevel` and `CleanLevelData`: `AllocForLevel`,
  `MeshInitForLevel`, `FreeLevel` and `MeshFreeLevel`, the mesh and bake state for each level.
- `features/levelprep/levelprep.cpp` `PrepareLevel` and `features/frame/levelrenderer.cpp` `EndSerialize`:
  `SetStackViewCapacity`, one stacked view per portal the level has. The walk slots follow it (the player's walk
  0, the skybox walk 1, the views from 2; `ComputeWalkSlots`), and so do the sky lists and the backend's pool of
  stacked-view targets, all made at level start so no view waits on a slot or a target mid-game.
- `features/sidebyside/glchecks.cpp`: what GL reports to its observer (`gl/scene/gl_observer.h`), for the
  checks against GL only, read off GL's types there and handed over through the same calls the walks use.
  That is GL's scenes (`EnterScene`/`LeaveScene`), walks (`BeginSkyWalk`, `NoteWalkedSubsector`/`NoteWalkedSide`,
  and `SkyboxWalk`, `StackWalk` and `MirrorWalk` around a portal's walk), sky quads and holes into the walk's
  list (`WalkSkyList`, `AddViewSkyQuad`, `AddViewSkyHole`), stacked walls (`AddStackWall`), mirror sheens
  (`AddMirrorSheen`), sprites (`DynAppend`), models (`ModelCaptureBegin` with `kModelFromGL`,
  `CaptureModelSurface`, `ModelCaptureEnd`) and GL's shading for `fua_shading_check` (`SetShadingOracle`).
- `gl/scene/gl_drawinfo.cpp` `DrawWalls`: `ComputeWallDrawPath`, so GL batches its own vertices. GL never
  draws from the mesh, which holds no GL buffer (queue #42, P3; `fua_mesh_blank` empties it to check).
- `gl/scene/gl_bsp.cpp` and `gl_wallcache.cpp`: `WallCacheEligibility`, `WallCacheStamp` and `ComputeCanReuse`,
  the rules GL's cache shares with the bake.
- `gl/renderer/gl_lightdata.cpp` `gl_ShadingEnv`: GL's light settings (light mode, ambient, fog rules, fixed
  colormap, skybox) for `ComputeShading`, the light model `ShadePiece` lights every piece with;
  `captureshading.cpp` turns a GL colormap into its input, and `CaptureLevelShading` bakes a level surface
  without the fixed colormap, which the backend's shader applies.
- `r_defs.h` `sector_t`: the plane texture placement setters (`SetXOffset`, `AddXOffset`, the Y pair, scales,
  angle, `SetBase`) bump `fua_placement`, so `InvalidateMovedSectors` re-bakes a scrolled floor's flats.
- `r_defs.h` `side_t::SetLight`: bumps the side's `fua_dirty`, so a wall `Transfer_WallLight` re-lights when its
  source sector's light changes re-bakes, in GL's wall cache and the bake alike.
- `p_sectors.cpp` `sector_t::SetColor` and `SetFade`: bump `fua_dirty`, so a sector recoloured after the bake,
  and every sector it is a 3D floor in, re-bakes with its new colormap.
- `textures/textures.h` `FTextureManager::GetAnimations`: the ANIMDEFS list, read-only, so the backend gives every
  frame of an animated texture a material slot and remaps baked pieces to the frame showing.
- `features/frame/standaloneframe.cpp` `StandaloneScene`: `DynClear`, `ClearSprites`, `UpdateProjectedDecals`,
  `InvalidateMovedSectors` with what the walks reached, and `BuildWallDecals` as the walks' decal hook.
- `gl/data/gl_data.cpp` `gl_RecalcVertexHeights`: calls `RecalcVertexHeights` (`ComputeVertexHeights`), so GL and
  the bake split walls at the same heights under `r_seamless`, which `ComputeSeamlessFan` ports from
  `GLWall::SplitLeftEdge` and the rest; a change of `r_seamless` re-bakes the level's walls.
- `hwrender/diligent/dgscene.cpp` `DrawSkySurfaces`: reads the list of the view it is drawing;
  `RenderStackPortals`/`DrawStackSurfaces` render each stacked view and paint its planes from it.

The rest of this section predates the bake: **P0 had none.** P0 is a console command and a pure computation unit; it reads existing map structures
and writes nothing. The dirty-tracking hooks the later phases need are enumerated in
`docs/levelmesh-PLAN.md` under "Invalidation taxonomy" and are not wired up.

## Things that will bite

- **`side_t::numsegs` and `vertex_t::numsectors` come from `zx::levelprep::PrepareLevel`** (`PrepareSegs` and
  `InitVertexData` in `features/levelprep/levelprep.cpp`). A dedicated server skips that, so both read zero and
  the budget understates. `CollectSideBudgets` detects this and `fua_levelmesh_stats` says so rather
  than printing a confident wrong number.
- **The budget assumes the equal-height dedupe in `gl_RecalcVertexHeights` is dropped** (plan P2b).
  With the dedupe in place the per-edge vertex count varies as sectors move, which is exactly what a
  fixed range forbids.
- **`side_t::V1()`/`V2()` are `inline` inside `po_man.cpp`** and cannot be reused. `SideVertices` in
  `levelmesh.cpp` re-derives the same thing, mirroring `GetSideVertices` in `features/levelprep/levelprep.cpp`.
