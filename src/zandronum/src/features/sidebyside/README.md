# features/sidebyside

GL and the backend on one game state. This is the one module that knows both renderers. It composes
them for a launch that has GL, so that neither renderer has to drive the other. Every other renderer
module talks to GL only through the renderer interface.

## What is here

- `sidebyside.cpp`: the frame buffer of a launch with GL. It is GL's own `OpenGLFrameBuffer`, so every
  GL path that casts the screen to it still works. Each 2D draw (textures, lines, pixels, clears, dims,
  flat fills, the textured automap's polygons) is also recorded for the backend's window through
  `features/hwrender`'s `Record2DCanvas`. GL leaves out a draw meant for the backend's window alone
  (`k2DBackendOnly`, `vid_fps`'s per-renderer text). Before this, GL's renderer recorded each draw
  itself and drew a backend-only one with its colour writes off.

## In-engine hooks

- `win32/win32gliface.cpp` `Win32GLVideo::CreateFrameBuffer`: `CreateGLFrameBuffer` in place of
  `new OpenGLFrameBuffer`. A build without the backend gets GL's frame buffer unchanged.
- `CMakeLists.txt`: `sidebyside.cpp` uses `IMPLEMENT_CLASS`, so it is listed in `add_executable`
  before `zzautozend.cpp`.
