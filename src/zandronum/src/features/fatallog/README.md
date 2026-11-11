# features/fatallog

A fatal error used to reach only its dialog: the error pane under a start screen, a message box before
the window exists, or the Cocoa console window, while the logs stopped at the last startup line. A
launch nobody is watching (fuactl, a server, CI) learned nothing but that it stalled.

`ZX_FatalLog` writes the run's first fatal error to stderr, the `+logfile` log and the MCP bridge's log
before any dialog opens, as `fua: FATAL ERROR -- <message>` with later lines indented
(`ComputeFatalLogText`). `I_FatalError` and the catch its exception reaches both call it, and only the
first is written (`ComputeFatalLogClaim`), so one error is logged once. fuactl's `launch` prints the
line as soon as the engine writes it.

## In-engine hooks

- `win32/i_main.cpp`: both `catch (CDoomError &)` blocks (`DoMain`, `MainDoomThread`), before `ShowErrorPane`.
- `win32/i_system.cpp`, `posix/sdl/i_system.cpp`, `posix/cocoa/i_system.mm`: `I_FatalError`, in place of its own `Logfile` write.
- `posix/sdl/i_main.cpp`: the `catch (CDoomError &)` in `main`.
