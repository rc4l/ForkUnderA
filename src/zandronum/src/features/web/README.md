# web

The browser build's glue: the page's arguments, its frame loop, its errors and crash reports, and the mouse. `docs/web-port-design.md` (section 9) has the plan, and `web/` at the repo root is the page.

- `webdriver.h`/`.cpp`, compiled only by the Emscripten build:
  - `InstallDriver()` installs `features/mainloop`'s frame driver. The browser then calls `D_RunFrame` once an animation frame. A restart reloads the page, and an escaped engine error shows on the page and leaves through `exit`.
  - `Fatal()` stops the loop and hands the message to the page (`Module.fuaFatal`).
  - `SetPageFullscreen()` makes the page's canvas fullscreen or not, for the `fullscreen` setting.
  - `__wrap_exit` takes every `exit()` (linked with `--wrap=exit`). A page runs no atexit handlers and keeps calling the frame, so it ends the loop, runs the engine's terms (which write the config) and calls `Module.fuaExit`, where the page stores everything and offers to start again.
  - `AppendPageArgs()` adds `-offline` (`features/net/zx_offline.h`) and the page URL's query to the command line.
  - `fua_command` runs a console command from the page (`Module.fuaCommand`). Its errors are handled as the loop handles a console command's.
- `webhost.cpp`, compiled only by the Emscripten build in place of `posix/sdl/sdlhost_desktop.cpp` and `posix/sdl/i_fpslimit.cpp`: the page as the SDL layer's host (`posix/sdl/sdlhost.h`). It starts the driver, adds the page's arguments, makes the canvas fullscreen, reads the pointer lock, shows a fatal error, asks no IWAD dialog, and has the page download a screenshot after the frame. The page paces the frames, so `I_SetFPSLimit` does nothing.
- `webcrash.cpp`, compiled only by the Emscripten build in place of `features/crashreport/zx_crashreport.cpp`:
  - It implements `zx_crashreport.h` for a page. The page catches a crash and builds, offers and sends its report (`web/crashreport.js`).
  - The engine tells the page what a report says about the game whenever it changes (`Module.fuaCrashContext`): release, map, load order, `cl_crashreports`, and the tracker's DSN on an official build only.
- `computation/webargs_compute.h`: the query as arguments (`ComputeArgsFromQuery`), tested.
- `computation/crashcontext_compute.h`: that context as JSON, with bare file names and the DSN only for an official build, tested.
- `computation/mousecapture_compute.h`: what the game does with the mouse when a page's pointer lock comes and goes (`ComputeMouseCapture`), tested.

## In-engine hooks

- The SDL layer asks its host (`posix/sdl/sdlhost.h`) wherever a page differs from a desktop, and holds no browser conditions itself:
  - `i_main.cpp`: `main` starts the host with `call_terms` (the driver here, the crash catcher on a desktop) and adds its arguments.
  - `i_system.cpp`: `I_FatalError` shows the error on the page before it exits, and `I_PickIWad` asks no dialog.
  - `i_input.cpp`: the mouse follows `ComputeMouseCapture`, held while the page has the pointer.
  - `hardware.cpp`: `I_SetMode` never makes an SDL fullscreen window, and `fullscreen` makes the canvas fullscreen instead of rebuilding the window. `fullscreen` and `vid_winscale` ask for no mode change at game start, which rebuilt the window on the first frame and sized the canvas to zero.
  - `sdlvideo.cpp`: `SDLFB::ScreenshotLater` has the page download the canvas after the frame, since WebGPU reads nothing back at once.
- `d_main.cpp`: `D_RecoverFromError`, the loop's answer to a recoverable error, also handles one from a command the page runs (`fua_command`).
- `menu/menudef.cpp`: IfOption's platform names match through `ComputeMenuPlatformMatches` (`menu/menuplatform_compute.h`), where a browser is `Web` and not `unix`. The video mode menu says the page sizes the game and shows the Renderer greyed out on WebGPU (`vid_backend_choosable`), instead of offering a window's size and a renderer. Option menus take an IfOption's `else`.
- `v_video.cpp`: the backend's vid_fps line is named for its device (`ComputeBackendRateName`), WebGPU in a browser.
- `src/CMakeLists.txt`:
  - The browser module links with `--wrap=exit`.
  - It compiles `webhost.cpp` in place of `posix/sdl/sdlhost_desktop.cpp`, `posix/sdl/i_fpslimit.cpp` and `crashcatcher.c`.
  - It compiles `webcrash.cpp` in place of `zx_crashreport.cpp`, with the same `ZX_SENTRY_DSN`.
