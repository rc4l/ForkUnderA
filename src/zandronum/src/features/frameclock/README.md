# frameclock

The frame's clock: where in its tic a frame is, to the microsecond.

The Windows game clock reads the performance counter instead of `timeGetTime`, so `I_GetTimeFrac` places
a frame within its tic to the microsecond. In whole milliseconds, frames 2 ms apart at 480 Hz drew
content 1, 2 or 3 ms apart. The tic boundaries, `I_MSTime` and everything the server times with it are
the same millisecond clock as before, read from the counter.

Two present pacers were tried here and measured with `fuactl screen` on Sunder MAP14 (queue #43): holding
each present to a fixed time after its view was sampled, and presenting at a fixed cadence. Both made
the judder worse, so neither is here. What reaches this compositor smoothly is a steady frame under about
2 ms, so the heavy view's cost is the lever.

The pure parts are in `computation/frameclock_compute`.

## In-engine hooks

- `win32/i_system.cpp`: `UsClock`/`MsClock` behind `I_MSTime`, `I_GetTime` and `I_GetTimeFrac`.
