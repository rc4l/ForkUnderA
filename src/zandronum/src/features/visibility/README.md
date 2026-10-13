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
- the particles in each subsector reached, handed to `features/sprites` as GL's walk hands them over;
- the sidedefs carrying decals each view saw (the player's, a stacked view's, a mirror's), handed over
  from inside that view so `features/levelmesh` glues their decals into its pass;
- the sky's fog: `GLSkyPortal::DrawContents` lays the fade of the sky it draws over the dome, and the
  sky portal GL makes first is drawn last, so the fade is that of the first sky wall the player's walk
  meets (`features/hwrender/computation/skyfog_compute`; scenario `skyfog`);
- what every walk reached, as lists (`SectorsWalkedThisFrame`, `SidesWalkedThisFrame`): a sector or
  side that moved, relit or scrolled is re-baked once a walk reaches it rather than every frame it
  changes (scenario `movedbehind`).

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
pass. The actors a stacked view's or mirror's walk reaches go to `features/sprites`, models included.

`fua_vis_report` also times the player's walks and counts what they reach, since a frame capped by vsync
hides the walk from frame-time A/Bs. On Sunder MAP10 at the start a walk reaches 7,411 subsectors and
22,621 segs, 10,198 of them visible, in about 1.3 ms (2.5 ms when the capped frame lets the CPU clock
down). Sky walls are raised only from a sky or stacked plane, the back sector's heights are read once a
seg, and a seg facing away never loads its sidedef; the rest is cache misses on the level's own structs.
A flat plane's height (`ComputeFlatPlaneHeight`) and a mirror sheen band's light (`ComputeSheenBandLight`)
are tested rules; the three checks keep `features/frame`'s `OracleTally`.

## The sliced walk

The player's walk runs on `features/jobs`' pool in one slice a lane (`fua_vis_slices`, -1): each slice's
clipper is GL's seeded with the frustum and blocked outside its own share of the view, so the union of what
the slices see is what one walk sees (`computation/walkslices_compute`, tested against one walk on random
walls). Each subsector is numbered by its place in the front-to-back order from the eye (`ComputeLeafCounts`),
and a slice marks what it reaches in shared stamp arrays rather than lists, so nothing needs merging. What only
walk order decides is applied afterwards on the game thread, rank by rank: the order sectors were first reached
in, the particles, each line's first seen seg's sky and stacked walls (worked out on the lanes as events and
only appended here), and the flats pass that feeds the stacked views. The edges move walk to walk so the slices
carry about equal work (`ComputeBalancedSlices`, `fua_vis_balance`).

A rank is stamped when a slice finds a sky or stacked wall candidate in it, so the replay skips the ranks
with none without asking every slice: on Sunder MAP10 the walk went from 0.42 to 0.39 ms.

The first line from the open into a height transfer in force switches the view area (`gl_CheckViewArea`), which
changes the fake flats behind it. Slices take last walk's switch line as a guess, and clip again from what they
saw until the guess holds, at most three times (`fua_vis_report` counts these). `fua_vis_threadcheck 1` walks
in one slice beside the slices every frame and compares the two visit for visit.

A node's box keeps whole units, so a vertex between them can lie just outside it (WDIEX02 has one half a unit
past). One walk enters such a box through any gap in it and then sees the seg that sticks out; a slice whose
share holds only the part that sticks out passes the box over and sees past that seg. So a slice notes each seg
past a box it passes over that shows in its share (`ComputeLeakySegs`), and if another slice reached that seg's
subsector, the slices clip again with the boxes down to it forced open (`ComputeLeakForces`), until a forced box
is one some slice entered by its own test. The forced boxes carry over to the next walk as its guess, so a still
view clips once; turning on the spot, about one walk in 25 clips again.

On Sunder MAP10 at the start the walk went from about 1.15 ms to 0.42 ms, and the frame from 1.58 to 0.81 ms.
It matched one walk across the saved spots and turning on the spot, stacked views, mirrors and height transfers
included, and GL's walk on all of them.

## In-engine hooks

- `features/sidebyside/glchecks.cpp`: `CheckAgainstGL` when GL reports its walk of the player's view done,
  from the view area GL started with, and `CheckPortalViews` when it reports its portal views done.
- `features/frame/standaloneframe.cpp` `StandaloneScene`: `PO_LinkToSubsectors`, `WalkView` (publishing to
  the player's walk slot unless `fua_vis_cull` is off) and `WalkPortalViews`, with `WalkHooks` for the
  particles, actors and decals they reach.
- `features/frame/levelrenderer.cpp` `FLevelRenderer::CleanLevelData`: `ForgetWalk`.
- `features/hwrender/hud2d.cpp` `SetSkyFog`: the sky's fog, which the sky pass lays over its dome.
- `features/levelmesh/wallcache.cpp` `InvalidateMovedSectors`: with the walks' lists, only what they reached.
- `features/levelmesh/flatmesh.cpp`: `BeginSlotVisibility`/`NoteSlotVisible`, the walk slots;
  `BeginViewSky`/`AddViewSkyQuad`/`AddViewSkyHole`, a view's sky surfaces; `BeginPortalViews`,
  `AddStackWall`, `AddWalkedMirror` and `AddMirrorSheen`, the portal views; `SwapMainSky` and
  `SwapPortalViews` for the check; `fua_particles`.
- `features/hwrender/diligent/dgviews.cpp`: `WriteSceneConstants`, the player's pass takes the main walk
  slot; the mirror pass, a mirror the walks saw (`PortalViewsDerived`) rather than any on screen.

GL state it still reads, until the renderer interface supplies the view (P5): `gl_FakeFlat`,
`gl_CheckViewArea` and `in_area`,
`currentmapsection`, `FPortal` and `gl_BuildPortalCoverage`, `gl_ClampLight`, and `ProcessParticle`.
