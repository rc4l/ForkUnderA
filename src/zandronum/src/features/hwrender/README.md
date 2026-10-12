# features/hwrender

The Diligent (Vulkan) backend that draws the level mesh beside or over GL's window, and the pure
`computation/` helpers it is built from. `diligent/` holds the backend itself; `hud2d` is the
Diligent-free layer both renderers can see.

## Engine hooks

| file | hook | what for |
|---|---|---|
| `d_main.cpp` | `Clear2D`, `FrameTimingBegin`, `LiveFrame` | start the frame's 2D capture and renderer clock; draw the backend's frame after GL's |
| `gl/renderer/gl_renderer.cpp` | `Record2D`, `k2DBackendOnly` | record every 2D draw for the backend; draw a backend-only one with GL's colour writes off |
| `gl/scene/gl_scene.cpp` | `Record2D`, `RenderCameraTexture`, `SceneMVP`, `SetMainViewLens`, `StandaloneActive` | the blend quad, camera textures, the player's lens, and skipping GL's world in standalone |
| `gl/scene/gl_weapon.cpp` | `Record2D`, `BackendPresenting` | the weapon sprite; no HUD model under an embedded backend |
| `gl/scene/gl_sprite.cpp`, `gl_spriteview.cpp` | `SpriteView`, `SetSpriteViewProvider`, `StandaloneActive` | sprites the backend draws itself |
| `gl/scene/gl_drawinfo.cpp`, `gl_walls_draw.cpp` | `ComputeGLSpritesBefore`, `ComputeWallLightIndex` | GL's draw order and wall light lists, shared with the backend |
| `gl/scene/gl_skydome.cpp` | `SetSkyFog` | the sky's fade layer |
| `gl/models/*.cpp`, `gl/system/gl_framebuffer.cpp` | `StandaloneActive` | GL idle while the backend carries the frame |
| `v_video.cpp` | `BackendRateLive`, `RendererRateShown`, `ComputeRateText`, `Set2DTarget` | `vid_fps` shows each window its own renderer's frame time |
