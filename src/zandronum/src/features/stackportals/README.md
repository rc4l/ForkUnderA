# features/stackportals

The stacked sectors' portals: a floor or ceiling that shows another sector through it, displaced by the
anchor pair's offset (`FPortal`, `portals`), found once a level (`gl_InitPortals`), and the subsectors each
portal's view covers (`gl_BuildPortalCoverage`). Moved unchanged from GL's `gl/data/gl_portaldata.cpp`, since
GL's stacked-sector portals and the backend's walks of the stacked views both read it.

`FPortal::GetGLPortal`, GL's render data for a portal, stays GL's and is defined in `gl/scene/gl_portal.cpp`;
`FPortal` holds it as an opaque pointer.

## In-engine hooks

- `gl/data/gl_data.h`: includes `stackportals.h` in place of the declarations it held.
- `gl/data/gl_setup.cpp` `gl_PreprocessLevel` and `features/frame/levelrenderer.cpp` `EndSerialize`:
  `gl_InitPortals` at level setup and on a loaded game.
- `features/visibility/viswalk.cpp`, `features/levelmesh/flatmesh.cpp`, `features/sidebyside/glchecks.cpp`: the
  portals and their coverage.
