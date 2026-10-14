# web

The browser build's glue: the page's arguments, its frame loop, and its errors. `docs/web-port-design.md` (section 9) has the plan, and `web/` at the repo root is the page.

- `webdriver.h`/`.cpp`, compiled only by the Emscripten build:
  - `InstallDriver()` installs `features/mainloop`'s frame driver. The browser then calls `D_RunFrame` once an animation frame. A restart reloads the page, and an escaped engine error stops the loop and shows on the page.
  - `Fatal()` stops the loop and hands the message to the page (`Module.fuaFatal`).
  - `AppendQueryArgs()` adds the page URL's query to the command line.
- `computation/webargs_compute.h`: the query as arguments (`ComputeArgsFromQuery`), tested.

## In-engine hooks

- `posix/sdl/i_main.cpp`:
  - `main` installs the driver and appends the query's arguments.
  - It skips the crash catcher, which needs signals a browser doesn't have.
- `posix/sdl/i_system.cpp`: `I_FatalError` calls `Fatal` before it exits.
