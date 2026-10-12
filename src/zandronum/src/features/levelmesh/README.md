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

`LightListBottom` (`levelmesh.h`) is a 3D floor light list entry's lower edge at a point, for the sprites,
the weapon and the screen tint alike.

## In-engine hooks

The frame's captures -- sky surfaces, sprites, models, decals (`flatmesh.cpp`, `modelmesh.cpp`, see
`docs/skybleed-scope.md`) -- belong to the player's outermost scene only, except that the sprites, models and
decals a stacked view's or a line mirror's walk draws are kept for that pass alone (`StackCaptureSlot`, `MirrorCaptureLine`):

- `gl/scene/gl_scene.cpp` `DrawScene`: a nesting guard (`EnterScene`/`LeaveScene`), since GL raises the
  portal depth for the player's own scene once its walk is done; `PlayerWalkActive` asks it.
- `gl/scene/gl_scene.cpp` `CreateScene`: `BeginSkyWalk`, and the frame's sprite, model and decal reset
  only on the player's scene; `RenderViewpoint` records which view is walking (`SetMainView`).
- `gl/scene/gl_walls.cpp` `PutWall`: `RegisterSkyQuad` for `RENDERWALL_SKY` and `RENDERWALL_SKYBOX`.
- `gl/scene/gl_drawinfo.cpp` `DrawWalls`: `ComputeWallDrawPath`, so GL batches its own vertices; GL never draws from the mesh,
  which holds no GL buffer (queue #42, P3; `fua_mesh_blank` empties it to check).
- `gl/models/gl_models.cpp` `gl_RenderModel`: `ModelCaptureBegin`/`ModelCaptureEnd` around the draw; a model
  `features/sprites` derives brackets its own capture with `ModelCaptureBeginDerived`.
- `gl/renderer/gl_lightdata.cpp` `gl_ShadingEnv`: GL's light settings (light mode, ambient, fog rules, fixed colormap,
  skybox) for `ComputeShading`, the light model `ShadePiece` lights every piece with; `captureshading.cpp` turns a GL
  colormap into its input.
- `gl/scene/gl_walls.cpp` `PutWall`: `RegisterStackWall` for `RENDERWALL_SECTORSTACK`, the walls GL stencils a stacked
  view through, gathered per portal into a few views.
- `r_defs.h` `sector_t`: the plane texture placement setters (`SetXOffset`, `AddXOffset`, the Y pair, scales, angle,
  `SetBase`) bump `fua_placement`, so `InvalidateMovedSectors` re-bakes a scrolled floor's flats under the map bake.
- `r_defs.h` `side_t::SetLight`: bumps the side's `fua_dirty`, so a wall `Transfer_WallLight` re-lights when its source
  sector's light changes re-bakes, in GL's wall cache and the map bake alike.
- `p_sectors.cpp` `sector_t::SetColor` and `SetFade`: bump `fua_dirty`, so a sector recoloured after the bake, and every
  sector it is a 3D floor in, re-bakes with its new colormap.
- `gl/scene/gl_walls.cpp` `PutWall`: `RegisterMirrorSheen` for a `RENDERWALL_MIRROR` with `gl_mirror_envmap` on, the light
  `RenderMirrorSurface` draws the mirror's sheen in, which the backend's mirror surface adds as GL does.
- `gl/scene/gl_walls.cpp` `Process`: `RegisterSkyHole` for a one-sided wall with no texture, which GL leaves undrawn: its
  full-screen sky shows through except where its walk already reached past it, every thing in a sector it entered and every
  seg and subsector it let through drawn whole, so the scene shader drops only the pieces behind a hole that walk did not draw.
- `gl/scene/gl_walls.cpp` `PutWall` (a portal wall) and `Process` (before `AddUpperMissingTexture` and
  `AddLowerMissingTexture`): `NoteUnreplayable`, since both hand GL per-frame state, so the wall cache never replays that seg.
- `gl/scene/gl_bsp.cpp` `DoSubsector` and `AddLine`: `NoteWalkedSubsector`/`NoteWalkedSide`, what a stacked view's walk or the
  player's drew.
- `gl/scene/gl_portal.cpp` `GLSkyboxPortal::DrawContents`: a `SkyboxWalk` around its `DrawScene`, so what that walk reaches is all the
  backend's sky portal pass draws of the level.
- `gl/scene/gl_portal.cpp` `GLSectorStackPortal::DrawContents`: a `StackWalk` around its `DrawScene`, so
  that walk's sky quads fill its view's list and its sprites, models and decals are filed under its view.
- `gl/scene/gl_portal.cpp` `GLMirrorPortal::DrawContents`: a `MirrorWalk(line)` around its `DrawScene`, so the sprites,
  models and decals it draws (the viewer's, and anything behind the player) are filed under that mirror and only that
  mirror's pass in the backend shows them.
- `textures/textures.h` `FTextureManager::GetAnimations`: the ANIMDEFS list, read-only, so the backend gives every frame
  of an animated texture a material slot and remaps baked pieces to the frame showing.
- `hwrender/diligent/dgscene.cpp` `DrawSkySurfaces`: reads the list of the view it is drawing;
  `RenderStackPortals`/`DrawStackSurfaces` render each stacked view and paint its planes from it.

The rest of this section predates the bake: **P0 had none.** P0 is a console command and a pure computation unit; it reads existing map structures
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
