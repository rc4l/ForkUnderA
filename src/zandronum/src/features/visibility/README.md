# features/visibility

The visible set without GL: GL's BSP walk and angle clipper (`gl_bsp.cpp`, `gl_clipper.cpp`), marking
every subsector, sidedef and sector a view reaches. In standalone the backend's player pass then draws
only what the player's walk reached, as GL draws only what its walk reached: a wall GL's clipper
rejects is not painted over the sky. (The sprite sweep keeps every actor in the frustum, since
standalone's one sprite set also serves mirrors and the skybox, which see past the player's walk.)
Walks also mark lines `ML_MAPPED` for the automap, as GL's walks do.

Each frame the player's walk also makes what GL's walk used to hand the backend:

- the sky surfaces: the walls `GLWall::SkyNormal`, `SkyTop`, `SkyBottom` and `DoHorizon` raise to a
  sky from every linedef the walk reaches, the untextured one-sided walls the sky shows through, and
  the skybox they name;
- the stacked views: each stacked plane's walls (`RENDERWALL_SECTORSTACK`), the subsectors that open
  into it (GL's flats pass and `ProcessSectorStacks`), then a walk from the eye moved by the portal's
  displacement over the map sections those subsectors cover, its clipper seeded from the walls as seen
  from the player (`GLPortal::ClearClipper`), giving the view's walk bits, sky surfaces and sprites;
- the skybox walks: from each skybox viewpoint the player's, the stacked views' and the mirrors' sky
  walls name, turned as GL turns it, into the skybox walk slot;
- the line mirrors: each `Line_Mirror` wall reached, its sheen's light (the light band `SplitWall`
  gives its first piece), and a walk from the reflected eye that leaves out what lies behind the
  mirror, giving the reflection's sprites;
- the particles in each subsector reached, handed to GL's sprite path as its walk hands them over.

Before this, standalone drew all of these as GL's last walk left them, which on a map where nothing
moves was the spawn view, and drew no particles and no actors in mirrors at all (scenarios `skymove`,
`stackturn`, `skyboxturn`, `mirrorsprite`, `particles`).

- `computation/clipranges_compute`: the clipper as a value (`ClipRanges`), the pseudo-angle and the
  precise angle, the frustum and portal seeds, and the BSP box test.
- `computation/closedline_compute`: `gl_CheckClip`, whether a two-sided line shuts the view.
- `computation/coverage_compute`: `SetCoverage`, the BSP nodes over a stacked view's covered subsectors.
- `computation/mirrorview_compute`: a mirror's reflected eye, and `SplitWall`'s first piece for its sheen.
- `features/surfaces/computation/skywall_compute`: the sky walls' heights.
- `viswalk.cpp`: the walks.

`fua_vis_check 1` walks again beside GL's own walks every frame GL walks the player's view and compares
the visible sets, the sky surfaces, the stacked views, skybox walks, mirrors and sheens bit for bit;
`fua_vis_report` prints the counts. They matched on about 19,000 frames for the player's walk and its
sky across wdiex02, wdiexsec, WDI01, WDI03, WDI13, WDI17, WDI18, AOW03 and dbab04, with
`fua_dg_horizon` on and off; on about 6,900 frames for the stacked views and skyboxes across WDI01,
WDI03, WDI07, WDI12, WDI18, AOW03 and wdiex02; and on 2,220 mirrors and sheens across WDI01, WDI07,
WDI12 and WDI18. GL's occlusion query sometimes leaves a view it judges hidden unwalked, which these
walks never do; the report counts such frames apart. `fua_particles` counts the particles handed over.

`fua_vis_walk` (on) runs the walks in standalone; `fua_vis_cull 0` walks without culling the player's
pass. A stacked view's or mirror's models are not drawn yet in standalone.

## In-engine hooks

- `gl/scene/gl_scene.cpp` `FGLRenderer::CreateScene`: `CheckAgainstGL` after GL's walk of the player's
  view, from the view area GL started with.
- `gl/scene/gl_scene.cpp` `FGLRenderer::DrawScene`: `CheckPortalViews` after `GLPortal::EndFrame`; in
  the standalone branch, `PO_LinkToSubsectors`, `WalkView` (publishing to the player's walk slot
  unless `fua_vis_cull` is off, with `WalkParticles`) and `WalkPortalViews` with `StackViewSprite`.
- `gl/data/gl_setup.cpp` `gl_CleanLevelData`: `ForgetWalk`.
- `features/levelmesh/flatmesh.cpp`: `BeginSlotVisibility`/`NoteSlotVisible`, the walk slots;
  `BeginViewSky`/`AddViewSkyQuad`/`AddViewSkyHole`, a view's sky surfaces; `BeginPortalViews`,
  `AddStackWall`, `AddWalkedMirror` and `AddMirrorSheen`, the portal views; `SwapMainSky` and
  `SwapPortalViews` for the check; `fua_particles`.
- `features/hwrender/diligent/dgviews.cpp`: `WriteSceneConstants`, the player's pass takes the main walk
  slot; the mirror pass, a mirror the walks saw (`PortalViewsDerived`) rather than any on screen.

GL state it still reads, until the renderer interface supplies the view (P5): `GLRenderer->FrustumAngle`,
`SetViewArea`, `SetViewAngle` and `mViewActor`, `gl_FakeFlat`, `gl_CheckViewArea` and `in_area`,
`currentmapsection`, `FPortal` and `gl_BuildPortalCoverage`, `gl_ClampLight`, and `ProcessParticle`.
