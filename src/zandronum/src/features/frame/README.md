# features/frame

The frame, driven the same way whichever renderers are drawing it.

`framedriver` is the engine's renderer (`CreateFrameDriver(gl, backend)`). It owns GL's renderer and the
backend's, either of which may be missing. Each frame it runs GL's `BeginFrame`, then `BeginPlayerFrame`
once, then GL's `RenderView` when GL draws (`GLDraws`) and the backend's when the backend draws
(`BackendDraws`). The save picture, camera textures, `ClearBuffer` and `Init` go to whichever renderer
draws them. So neither renderer drives the other, and the frame's shared state is set up once:
`r_viewLens`, the player's field of view and aspect, and `r_frameSerial`, which `P_BobWeapon` steps
`curbob` by, so a frame both renderers draw bobs once.

`StandaloneScene` is the backend's scene for the view set up now:

- the upkeep: the dynamic stream, last frame's sprites, projected decals, horizons;
- the visibility walks (`features/visibility`), with the particles, the actors in stacked views and
  mirrors, and the decals each view saw;
- the re-bake of what moved or relit among what the walks reached (`InvalidateMovedSectors`);
- the camera textures a walk saw (`MarkSeenCameraTextures`, with `computation/cameraseen_compute`);
- the sweep of the actors in view (`features/sprites`).

`computation/actorcull_compute` is the sweep's frustum test, padded by the actor's size since a sprite
is wider than the point it stands on.

`playerview` is the player's view, shared by GL and the backend:

- `BeginPlayerFrame`: a new camera's interpolation reset, the tic fraction and clock, particles, last
  frame's camera textures, the fixed colormap and the lens;
- `BeginViewpoint`/`EndViewpoint`: which view the walks belong to, the frame set up from the camera,
  the view area and `features/viewsetup`'s view, then the frame count and interpolations;
- `MarkMapSection`.

`RenderPlayerView` is the backend's frame: those, then `StandaloneScene`, then the weapon, the targeter
and the screen tint into the 2D capture. A frame's sprites are checked against GL's after its scene,
when `fua_sprite_check` is on.

`levelrenderer` is the half of the renderer interface that is the level, its lights, fog and pitch
limits. GL's `FGLInterface` and the backend's renderer both derive from `FLevelRenderer` and override
only what they draw with. Its `PreprocessLevel` and `CleanLevelData` also set up and free the backend's
mesh, bake state and walks. `RenderBackendCamera` hands a camera texture's view to the backend.

`computation/oracle_compute` is the tally every check against GL keeps (`fua_vis_check`,
`fua_sprite_check`, `fua_psprite_check`, `fua_shading_check`): comparisons, differences and the first
few described.

## In-engine hooks

- `win32/hardware.cpp` and `posix/sdl/hardware.cpp` `I_CreateRenderer`: `CreateFrameDriver` over GL's
  renderer and the backend's.
- `r_renderer.h` `FRenderer::BeginFrame`: GL's frame prologue, which the driver runs once a frame.
- `r_utility.h`/`.cpp`: `r_viewLens` and `r_frameSerial`; `p_pspr.cpp` `P_BobWeapon` steps `curbob` once
  per `r_frameSerial`.
- `gl/scene/gl_scene.cpp` `FGLRenderer::RenderView`, `RenderViewpoint` and `ProcessScene`: `BeginViewpoint`,
  `EndViewpoint` and `MarkMapSection`. `FGLInterface` derives from `FLevelRenderer`.
- `gl/system/gl_framebuffer.cpp` and `features/hwrender/backendvideo.cpp`: `ForgetCamera` on a new frame
  buffer and a game restart.
- `features/hwrender/backendrenderer.cpp` `FBackendRenderer`: an `FLevelRenderer` whose `RenderView` is
  `RenderPlayerView`. Its `RenderTextureView` is a camera's viewpoint, `StandaloneScene` and
  `RenderBackendCamera`.
- `features/levelmesh/captureshading.cpp`, `features/sprites/spritebuild.cpp`, `pspritebuild.cpp` and
  `features/visibility/viswalk.cpp`: an `OracleTally` each for their check against GL.
