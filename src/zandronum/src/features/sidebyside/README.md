# features/sidebyside

GL and the backend on one game state. This is the one module that knows both renderers. It composes
them for a launch that has GL, so that neither renderer has to drive the other. Every other renderer
module talks to GL only through the renderer interface.

## What is here

- `sidebyside.cpp`: the frame buffer of a launch with GL. It is GL's own `OpenGLFrameBuffer`, so every
  GL path that casts the screen to it still works. Each 2D draw (textures, lines, pixels, clears, dims,
  flat fills, the textured automap's polygons) is also recorded for the backend's window through
  `features/hwrender`'s `Record2DCanvas`. GL leaves out a draw meant for the backend's window alone
  (`k2DBackendOnly`, `vid_fps`'s per-renderer text).
- Whether GL draws a frame at all. `Update` sets the frame buffer's `mPresents`: GL presents unless the
  backend covers its window and nothing wants GL's view (`BackendCoversGL`, `GLViewWanted`: standalone
  and embedded with no check on). A frame GL does not present skips its `glFinish` and `SwapBuffers`,
  and the frame driver runs GL's `RenderView` by the same rule (`GLDraws`).
- `glchecks.cpp`: the checks against GL. GL reports its walk and draw to an observer it owns
  (`gl/scene/gl_observer.h`), and only while one is set. `WatchGLForChecks` sets `FGLChecks` while
  `fua_vis_check`, `fua_sprite_check` or `fua_psprite_check` is on. It hands GL's walk bits, sky
  surfaces, portal views, sprites, models and weapon to the captures those checks compare against.

## In-engine hooks

- `win32/win32gliface.cpp` `Win32GLVideo::CreateFrameBuffer`: `CreateGLFrameBuffer` in place of
  `new OpenGLFrameBuffer`. A build without the backend gets GL's frame buffer unchanged.
- `gl/system/gl_framebuffer.cpp` `OpenGLFrameBuffer::Swap`: no `glFinish` or `SwapBuffers` without
  `mPresents`.
- `gl/scene/gl_bsp.cpp`, `gl_walls.cpp`, `gl_portal.cpp`, `gl_scene.cpp`, `gl_sprite.cpp`, `gl_weapon.cpp`
  and `gl/models/gl_models.cpp`, `gl_models_md2.cpp`, `gl_models_md3.cpp`, `gl_voxels.cpp`: what they report
  to `gl_observer` when it is set.
- `CMakeLists.txt`: `sidebyside.cpp` uses `IMPLEMENT_CLASS`, so it is listed in `add_executable`
  before `zzautozend.cpp`.
