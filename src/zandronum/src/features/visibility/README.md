# features/visibility

The player's visible set without GL: GL's BSP walk and angle clipper (`gl_bsp.cpp`, `gl_clipper.cpp`),
marking every subsector, sidedef and sector the view reaches and building nothing. In standalone the
backend's player pass then draws only what the walk reached, as GL draws only what its walk reached:
a wall GL's clipper rejects is not painted over the sky. (The sprite sweep keeps every actor in the
frustum, since standalone's one sprite set also serves mirrors and the skybox, which see past the
player's walk.) The walk also marks lines `ML_MAPPED` for the automap, as GL's walk does.

- `computation/clipranges_compute`: the clipper as a value (`ClipRanges`), the pseudo-angle, the frustum
  seed and the BSP box test.
- `computation/closedline_compute`: `gl_CheckClip`, whether a two-sided line shuts the view.
- `viswalk.cpp`: the walk.

`fua_vis_check 1` walks again beside GL's own walk every frame GL walks the player's view and compares
the two; `fua_vis_report` prints the count. It matched on about 4,500 frames across AOW03, wdiex02,
WDI01, WDI09, WDI18 and dbab04.

`fua_vis_walk` is off by default. On wdiex02 the walk costs 0.22 ms a frame and the cull 0.09 more
(`fua_vis_cull 0` walks without culling), and drawing less in the vertex shader does not repay it. It
goes on when culling pays: whole batches or indirect draws skipped on the CPU (P6), and the per-frame
sky surfaces and stacked views standalone still takes stale from GL's last walk.

## In-engine hooks

- `gl/scene/gl_scene.cpp` `FGLRenderer::CreateScene`: `CheckAgainstGL` after GL's walk of the player's
  view, from the view area GL started with.
- `gl/scene/gl_scene.cpp` `FGLRenderer::DrawScene`, the standalone branch: `PO_LinkToSubsectors` and
  `WalkView`, publishing to the player's walk slot unless `fua_vis_cull` is off.
- `features/levelmesh/flatmesh.cpp`: `BeginMainVisibility`/`NoteMainVisible`, the player's walk slot.
- `features/hwrender/diligent/dgviews.cpp` `WriteSceneConstants`: the player's pass takes the main walk slot.

GL state it still reads, until the renderer interface supplies the view (P5): `GLRenderer->FrustumAngle`,
`gl_FakeFlat`, `gl_CheckViewArea` and `in_area`, `currentmapsection`.
