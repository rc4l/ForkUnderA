# features/frame

The standalone frame, driven without GL: what runs each frame in place of GL's scene derivation when
the backend draws the world (`fua_dg_standalone`). `StandaloneScene` does the upkeep (the dynamic
stream, last frame's sprites, projected decals, horizons), the visibility walks (`features/visibility`,
with the particles and the actors in stacked views and mirrors they reach), the re-bake of what moved
or relit among what the walks reached (`InvalidateMovedSectors`), and the sweep of the actors in view
(`features/sprites`). `computation/actorcull_compute` is the sweep's frustum test, padded by the actor's
size since a sprite is wider than the point it stands on.

What the backend does not derive goes back through `SceneHooks` to a renderer still running beside it:
GL takes an actor with `fua_sprites_own` off or the check on, and a particle the backend left. A
renderer with nothing beside it passes no hooks.

`playerview` is the player's view, shared by GL and a renderer with no GL beside it (`-vulkan`):
`BeginPlayerFrame` (a new camera's interpolation reset, the tic fraction and clock, particles, last frame's
camera textures, the fixed colormap and the lens), `BeginViewpoint`/`EndViewpoint` (which view the walks
belong to, the frame set up from the camera, the view area and `features/viewsetup`'s view, then the frame
count and interpolations) and `MarkMapSection`. `RenderPlayerView` is the frame without GL: those, then
`StandaloneScene` with no hooks and the weapon, targeter and screen tint into the 2D capture.

`levelrenderer` is the half of the renderer interface that is the level, its lights, fog and pitch
limits; GL's `FGLInterface` and the backend's renderer both derive from `FLevelRenderer` and override only
what they draw with, and `RenderBackendCamera` hands either one's camera texture view to the backend.

`computation/oracle_compute` is the tally every check against GL keeps (`fua_vis_check`, `fua_sprite_check`,
`fua_psprite_check`, `fua_shading_check`): comparisons, differences and the first few described.

This is the first part of P5's frame driver; the view setup (`features/viewsetup`), the weapon
(`features/sprites`) and the 2D layer (`features/draw2d`) are already worked out without GL.

## In-engine hooks

- `gl/scene/gl_scene.cpp` `FGLRenderer::DrawScene`: `StandaloneScene` with GL's `ProcessSprite` and
  `ProcessParticle` as the hooks; when it returns true GL presents, draws the sprites its hooks collected
  and returns, and when false (a moved sector the map bake does not drive) GL derives the scene itself.
- `gl/scene/gl_scene.cpp` `FGLRenderer::RenderView`, `RenderViewpoint` and `ProcessScene`: `BeginPlayerFrame`,
  `BeginViewpoint`, `EndViewpoint` and `MarkMapSection`; `FGLInterface` derives from `FLevelRenderer`, and its
  `RenderTextureView` calls `RenderBackendCamera`.
- `gl/system/gl_framebuffer.cpp` and `features/hwrender/backendvideo.cpp`: `ForgetCamera` on a new frame buffer
  and a game restart.
- `features/hwrender/backendrenderer.cpp` `FBackendRenderer`: an `FLevelRenderer` whose `RenderView` is
  `RenderPlayerView`, with the backend frame buffer's palette brightness.
- `features/levelmesh/captureshading.cpp`, `features/sprites/spritebuild.cpp`, `pspritebuild.cpp` and
  `features/visibility/viswalk.cpp`: an `OracleTally` each for their check against GL.
