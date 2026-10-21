# features/fakeflat

A sector as the view sees it through a height transfer, moved here unchanged from GL's
`gl/scene/gl_fakeflat.cpp` so the backend's walks, bake and weapon read it without GL:

- `gl_FakeFlat`: the planes, textures and light a `Transfer_Heights` sector shows from the view area the
  view is in (below the fake floor, above the fake ceiling, or between).
- `gl_CheckViewArea`: whether the view area changes across a line into such a sector.
- `gl_CheckClip`: whether a two-sided line shuts the view, as GL's clipper asks.
- `ViewDependent`: whether a sector takes another's heights (`Transfer_Heights`), whose planes then depend on
  where the viewer stands.
- `TransferSector` and `SetTransferArea`: a sector as a height transfer shows it from the view area the bake
  is set to, moved from `features/levelmesh` so the walks and the weapon read it below the mesh.

The names are GL's, so GL's walk calls them unchanged; the view area itself is `features/rendersettings`'
`in_area`.

## In-engine hooks

- `gl/renderer/gl_renderer.h`: includes `fakeflat.h` in place of the declarations it held, so GL's
  `gl_bsp.cpp`, `gl_flats.cpp`, `gl_walls.cpp` and the rest call these as before.
- `features/visibility/viswalk.cpp`, `features/levelmesh/flatmesh.cpp`, `features/sprites/pspritebuild.cpp`
  and `features/frame/levelrenderer.cpp` (`FakeFlat`): `gl_FakeFlat`.
- `features/levelmesh/flatmesh.cpp`, `fakeplanes.cpp`, `wallcache.cpp` and `features/surfaces/surfacebuild.cpp`:
  `ViewDependent`, `TransferSector` and `SetTransferArea`.
