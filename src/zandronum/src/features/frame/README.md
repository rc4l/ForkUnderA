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

`playerview` is the player's view for a renderer with no GL beside it (`-vulkan`): the engine half of
GL's `RenderView`, `RenderViewpoint` and `EndDrawScene`. It sets the frame's clocks, runs the camera
textures, sets the view up (`features/viewsetup`), runs `StandaloneScene` with no hooks, and records the
weapon, the targeter and the screen's tint into the 2D capture.

This is the first part of P5's frame driver; the view setup (`features/viewsetup`), the weapon
(`features/sprites`) and the 2D layer (`features/draw2d`) are already worked out without GL.

## In-engine hooks

- `gl/scene/gl_scene.cpp` `FGLRenderer::DrawScene`: `StandaloneScene` with GL's `ProcessSprite` and
  `ProcessParticle` as the hooks; when it returns true GL presents, draws the sprites its hooks collected
  and returns, and when false (a moved sector the map bake does not drive) GL derives the scene itself.
- `features/hwrender/backendrenderer.cpp` `FBackendRenderer::RenderView`: `RenderPlayerView`, with the
  backend frame buffer's palette brightness.
