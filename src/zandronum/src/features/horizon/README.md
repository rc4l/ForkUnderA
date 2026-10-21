# features/horizon

`Line_Horizon` (special 9) for the Diligent/Vulkan backend.

GL never draws a horizon line as a wall. `GLWall::Process` hands it to `DoHorizon` and returns: above
eye level the line shows the sky (or the sector's ceiling carried out to infinity), below eye level
the sector's floor carried out to infinity, and nothing behind the line is ever visible. The
map-derived bake did not know the special existed. On wdiex02 it built 102 horizon lines as ordinary
one-sided walls, so a dark `SRISE_BK` slab stood across the skyline, and the strip under the skyline
showed the clear colour where GL runs the water out to the horizon.

## How it works

Each side of a horizon line becomes a **window**: a quad that writes depth like a wall, so nothing
the map-derived mesh holds beyond the line can show through. It is painted two ways:

- **The plane halves** are drawn by `DrawHorizonPlanes` (`hwrender/diligent/dghorizon.cpp`). Each
  fragment casts a ray from the pass's eye through the window and lights the point where it meets
  the floor or ceiling plane. The light is the world's own shading (`dglight_glsl.h`), and the UVs
  are the flat's own mapping (`ComputePlaneUVAffine`). Past GL's ±32768 square the ray stops at the
  square's edge, as `GLHorizonPortal` does.
- **The sky half** is fed into the existing sky-surface pass, so it samples the same sky image as
  every other sky surface.

Each pass culls on the CPU from its own eye: windows facing away, halves the eye leaves empty, and
quads off screen. Shared helpers carry the parts other code needed too: `ComputeBeyondSkyPortalReach`
(`hwrender/computation/skyportal_compute`), `ComputeUnpackRGB` (`levelmesh/computation/packedcolor_compute`),
`ComputeFanTriangleVertex` via `MakeWallQuad`, and `hwrender::ComputeBatchRuns` for one draw per material.

Windows are rebuilt from the map every frame (a level has a handful), so light changes, animated or
scrolling flats and moving floors need no invalidation. `fua_dg_horizon 0` turns the feature off for
an A/B. `fua_horizons` prints the windows.

## Files

| file | role |
|---|---|
| `computation/horizon_compute.{h,cpp,_test.cpp}` | Which lines are horizons, the window's span, the eye's split, the slice each half paints, the plane's light inputs, the ray/plane hit the shader mirrors, the eye from a view-projection, and the quad clip test. |
| `horizon.{h,cpp}` | Engine glue: finds the horizon sidedefs per level and builds this frame's windows with `CaptureShading`. |
| `../hwrender/diligent/dghorizon.cpp` | The plane pass: per-pass culling, the ray-casting shader and the draw. |

## In-engine hooks

- `gl/scene/gl_scene.cpp`: `UpdateHorizons()` runs once per outer walk in `CreateScene` and once per
  frame in the standalone branch of `DrawScene`.
- `gl/scene/gl_walls.cpp`: `RegisterSkyQuad` is passed the skybox explicitly (see below).
- `features/visibility/viswalk.cpp` and `features/sidebyside/glchecks.cpp`: `OwnsSky`, so a horizon line's
  sky is left to this feature rather than added to a view's sky list.
- `features/levelmesh/wallcache.cpp`: `BakeSegFromMap` builds no parts on a horizon line.
- `features/surfaces/surfacebuild.cpp`: `BuildDerivedWallSpan` declines a horizon line.
- `features/hwrender/diligent/dgscene.cpp`: `DrawWorld` calls `DrawHorizonPlanes`;
  `DrawSkySurfaces` appends `HorizonSkyVertices`; `SkyPortalReach` exposes the portal pass's reach.
- `features/levelmesh/staticmesh.cpp` and `gl/renderer/gl_renderstate.h`: `CaptureShading` restores
  the render state it wrote.

## Two things found on the way

- **GL render state.** `CaptureShading` runs `gl_SetColor`/`gl_SetFog`, which write GL's live render
  state, and its callers run inside GL's frame -- this feature's every frame, and the moved-sector
  re-bake. It now restores the fields those two write (`FRenderState::SaveShading`/`RestoreShading`),
  so asking cannot change what GL draws next. That is hygiene, not a measured fix: the black GL frames
  first blamed on it happen on the old build too (see Capture caveats in `docs/skybleed-scope.md`).
- **A crash on every plain-sky map.** `RegisterSkyQuad` read `wall.skybox`, which shares a union
  with `wall.sky`. On a texture sky that is a `GLSkyInfo`, and `RenderSkyPortal` dereferenced it as
  a skybox. Doom 2 MAP01 crashed on load under Vulkan. The caller now passes the viewpoint, and NULL
  for a plain sky.

## Known gaps

- A sky FLOOR on a horizon line is not drawn. The sky-surface pass cuts every sky fragment at the
  horizon, the same as every other sky floor.
- On a map whose sky is a texture (the dome), the sky half shows the dome behind it but does not
  occlude what lies beyond the line. That is the dome path's general limit, not this feature's.

## Regression test

`tools/fuactl/scenarios/horizon.mjs` builds a two-map template wad (`tools/fuactl/src/udmf.mjs`) and
compares GL against Vulkan at fixed vantages through `src/template.mjs` -- see the `template-maps` skill. MAP01 is a plain sky; MAP02 has a skybox and a tall
room behind the horizon line. The old build crashes on MAP01 and fails MAP02's horizon boxes
(24 and 17); this build passes every box at about 2.
