# frameclock

The frame's clock: where in its tic a frame is, to the microsecond.

The Windows game clock reads the performance counter instead of `timeGetTime`, so `I_GetTimeFrac` places
a frame within its tic to the microsecond. In whole milliseconds, frames 2 ms apart at 480 Hz drew
content 1, 2 or 3 ms apart. The tic boundaries, `I_MSTime` and everything the server times with it are
the same millisecond clock as before, read from the counter.

`fua_debug_ticfrac_ms 1` cuts the fraction back to whole milliseconds, as the old clock gave it, for an A/B
by eye while moving (turning reads the mouse every frame and is the same either way).

The judder of queue #43 that this came from was the Windows compositor stalling for every app, a few screen
updates a second, until a restart (elevated PresentMon); the two present pacers tried and dropped here were
judged with `fuactl screen` during that time, so those results say nothing either way.

The pure parts are in `computation/frameclock_compute`.

## In-engine hooks

- `win32/i_system.cpp`: `UsClock`/`MsClock` behind `I_MSTime`, `I_GetTime` and `I_GetTimeFrac`, and
  `fua_debug_ticfrac_ms`.
