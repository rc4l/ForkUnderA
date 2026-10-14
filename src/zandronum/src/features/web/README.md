# web

The browser build's glue: the page's arguments, its frame loop, and its errors. `docs/web-port-design.md` (section 9) has the plan, and `web/` at the repo root is the page.

- `webdriver.h`/`.cpp`, compiled only by the Emscripten build:
  - `InstallDriver()` installs `features/mainloop`'s frame driver. The browser then calls `D_RunFrame` once an animation frame. A restart reloads the page, and an escaped engine error shows on the page and leaves through `exit`.
  - `Fatal()` stops the loop and hands the message to the page (`Module.fuaFatal`).
  - `__wrap_exit` takes every `exit()` (linked with `--wrap=exit`). A page runs no atexit handlers and keeps calling the frame, so it ends the loop, runs the engine's terms (which write the config) and calls `Module.fuaExit`, where the page stores everything and offers to start again.
  - `AppendPageArgs()` adds `-offline` (`features/net/zx_offline.h`) and the page URL's query to the command line.
  - `fua_command` runs a console command from the page (`Module.fuaCommand`). Its errors are handled as the loop handles a console command's.
- `computation/webargs_compute.h`: the query as arguments (`ComputeArgsFromQuery`), tested.

## In-engine hooks

- `posix/sdl/i_main.cpp`:
  - `main` installs the driver, handing it `call_terms`, and appends the page's arguments.
  - It skips the crash catcher, which needs signals a browser doesn't have.
- `posix/sdl/i_system.cpp`: `I_FatalError` calls `Fatal` before it exits.
- `d_main.cpp`: `D_RecoverFromError`, the loop's answer to a recoverable error, also handles one from a command the page runs (`fua_command`).
- `src/CMakeLists.txt`: the browser module links with `--wrap=exit`.
