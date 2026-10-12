# features/hwrender

The Diligent (Vulkan) backend that draws the level mesh beside or over GL's window, and the pure
`computation/` helpers it is built from. `diligent/` holds the backend itself; `hud2d` is the
Diligent-free layer both renderers can see.

The player's view is framed in the view window as GL's `SetViewport` frames it (a status bar raises it,
a smaller `screenblocks` shrinks it): `computation/backendview_compute`'s `ComputeViewWindowViewport` is
GL's viewport, and `ComputeNdcWindow`/`ComputeApplyNdcWindow` fold it into the projection `BuildMVP`
makes, so the target stays whole and every pass that reads `gl_FragCoord` against the MVP stays in step.
Scenario `viewwindow`.

## A launch without GL (`-vulkan`)

`-vulkan` on the command line makes no GL context at all (Windows builds with the backend). `I_CreateRenderer`
installs `FBackendRenderer` (`backendrenderer.cpp`), whose `RenderView` is `features/frame`'s
`RenderPlayerView` and whose level, light and fake-flat work is GL's CPU-side code. `Win32GLVideo` keeps the
window and modes but sets no pixel format, and makes `DiligentFrameBuffer` (`backendvideo.cpp`): the 2D draws
recorded through `features/draw2d`, the palette and gamma from `features/display`, `screenshot` read back from
the swapchain, and every frame presented from its `Update` by `PresentBackendOnly`, with the world only under
a frame that drew a view of it (the title, menus and console otherwise). The launch turns `fua_vulkan`,
`fua_dg_standalone` and `fua_dg_embed` on, and puts the config's values back before it is written.
`backendonly.h` is the Diligent-free interface between them. Wipes are cut straight over, the save picture is
a placeholder, and `FillSimplePoly` is not drawn yet. `fuactl play --no-gl` launches one, `dumprenderer`
says `renderer=vulkan` in one, and a scenario side with `noGl` runs as one (scenario `nogl`).

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
| `win32/hardware.cpp` | `BackendOnly`, `BeginBackendOnly`, `CreateBackendRenderer` | the renderer of a launch without GL |
| `win32/win32gliface.cpp` | `BackendOnly`, `CreateBackendFrameBuffer` | no pixel format or context, and the backend's frame buffer |
| `features/frame/playerview.cpp` | `NoteWorldView`, `SetMainViewLens` | the frame drew the world, through the player's lens |
| `gl/data/gl_vertexbuffer.cpp`, `gl/models/gl_models.cpp`, `gl/system/gl_interface.h` | `gl_HasContext` | vertex buffers keep only their CPU copy without GL |
| `gl/textures/gl_texture.cpp`, `gl_material.cpp`, `gl_hqresize.cpp`, `gl/data/gl_data.cpp` | `FMaterial::TextureBuffer`, `FMaterial::FlushAll` | texture reads and flushes that do not go through `GLRenderer` |
