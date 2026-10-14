# app-icon

ForkUnderA's application icon, from one master picture: `wadsrc/static/fua_appicon.png` (512x512, in the
core pk3).

| Platform | Where the icon comes from |
|---|---|
| Windows | `win32/zandronum.ico`, made from the master by `tools/make-appicon.py` and compiled in by `win32/zdoom.rc` (`IDI_ICONST`) and the server console's resources. |
| macOS | `mac_compile.sh` `make_icon` turns the master into the bundle's `.icns` with `tools/make-iconset.py`. |
| Linux | The engine reads the master out of the core pk3 at run time and gives it to SDL; the tarball also ships it as `forkundera.png` with `install-desktop-entry.sh` for the application menu. |

## Files

- `computation/appicon_compute.{h,cpp}` (+ `_test.cpp`): `ComputeIconDownscale`, an alpha-weighted area
  average so clear texels add no dark fringe.
- `appicon.{h,cpp}`: `LoadAppIcon` reads `fua_appicon.png` with `Wads.CheckNumForFullName`, decodes it
  through `FTexture::CreateTexture` into an `FBitmap` (BGRA) and downscales it. Built only with the SDL
  platform sources.

## In-engine hooks

- `posix/sdl/sdlglvideo.cpp`: `SetAppIcon` calls `SDL_SetWindowIcon` right after `SDL_CreateWindow`; the
  Vulkan backend takes over the same window, so both renderers show it.
- `features/hwrender/diligent/dgwin32.cpp`: the backend's own window class (`FuaDiligentWnd`, top-level in
  a side-by-side launch) loads `IDI_ICONST`.
- `CMakeLists.txt`: `features/app-icon/appicon.cpp` in `PLAT_SDL_SOURCES`.
- `mac_compile.sh` `make_icon` and `linux_compile.sh`'s staging step, as above.

To change the icon, replace the master and run `python tools/make-appicon.py`.
