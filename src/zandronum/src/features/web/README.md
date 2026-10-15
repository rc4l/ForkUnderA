# web

The browser build's glue: the page's arguments, its frame loop, its errors and crash reports, and the mouse. `docs/web-port-design.md` (section 9) has the plan, and `web/` at the repo root is the page.

- `webdriver.h`/`.cpp`, compiled only by the Emscripten build:
  - `InstallDriver()` installs `features/mainloop`'s frame driver. The browser then calls `D_RunFrame` once an animation frame. A restart reloads the page, and an escaped engine error shows on the page and leaves through `exit`.
  - `Fatal()` stops the loop and hands the message to the page (`Module.fuaFatal`).
  - `SetPageFullscreen()` makes the page's canvas fullscreen or not, for the `fullscreen` setting.
  - `__wrap_exit` takes every `exit()` (linked with `--wrap=exit`). A page runs no atexit handlers and keeps calling the frame, so it ends the loop, runs the engine's terms (which write the config) and calls `Module.fuaExit`, where the page stores everything and offers to start again.
  - `AppendPageArgs()` adds `-offline` (`features/net/zx_offline.h`) and the page URL's query to the command line.
  - `fua_command` runs a console command from the page (`Module.fuaCommand`). Its errors are handled as the loop handles a console command's.
- `webcrash.cpp`, compiled only by the Emscripten build in place of `features/crashreport/zx_crashreport.cpp`:
  - It implements `zx_crashreport.h` for a page. The page catches a crash and builds, offers and sends its report (`web/crashreport.js`).
  - The engine tells the page what a report says about the game whenever it changes (`Module.fuaCrashContext`): release, map, load order, `cl_crashreports`, and the tracker's DSN on an official build only.
- `computation/webargs_compute.h`: the query as arguments (`ComputeArgsFromQuery`), tested.
- `computation/crashcontext_compute.h`: that context as JSON, with bare file names and the DSN only for an official build, tested.
- `computation/mousecapture_compute.h`: what the game does with the mouse when a page's pointer lock comes and goes (`ComputeMouseCapture`), tested.

## In-engine hooks

- `posix/sdl/i_main.cpp`:
  - `main` installs the driver, handing it `call_terms`, and appends the page's arguments.
  - It skips the crash catcher, which needs signals a browser doesn't have.
- `posix/sdl/i_system.cpp`: `I_FatalError` calls `Fatal` before it exits.
- `posix/sdl/i_input.cpp`: the mouse follows `ComputeMouseCapture`, held while `emscripten_get_pointerlock_status` says the page has the pointer.
- `posix/sdl/hardware.cpp`:
  - `fullscreen` and `vid_winscale` ask for no mode change at game start, which rebuilt the window on the first frame and sized the canvas to zero.
  - In a page, `I_SetMode` never makes an SDL fullscreen window, and `fullscreen` calls `SetPageFullscreen` instead of rebuilding the window.
- `d_main.cpp`: `D_RecoverFromError`, the loop's answer to a recoverable error, also handles one from a command the page runs (`fua_command`).
- `menu/menudef.cpp`: IfOption's platform names match through `ComputeMenuPlatformMatches` (`menu/menuplatform_compute.h`), where a browser is `Web` and not `unix`. The video mode menu says the page sizes the game and shows the Renderer greyed out on WebGPU (`vid_backend_choosable`), instead of offering a window's size and a renderer. Option menus take an IfOption's `else`.
- `v_video.cpp`: the backend's vid_fps line is named for its device (`ComputeBackendRateName`), WebGPU in a browser.
- `src/CMakeLists.txt`:
  - The browser module links with `--wrap=exit`.
  - It compiles `webcrash.cpp` in place of `zx_crashreport.cpp`, with the same `ZX_SENTRY_DSN`.
