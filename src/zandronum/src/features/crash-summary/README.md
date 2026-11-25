# Crash summary

The last minute a crash report sends, in every build, public ones included, where telemetry is not compiled
(`docs/diagnostics.md`). Once a frame it reads the clock; once a second it takes the OS probe's light readings
(never its heap walk) and keeps that second's frame time mean and max, the frames over 50 ms and the process's
private and GPU memory. Its text is telemetry's flight summary (`ComputeFlightSummary`).

`computation/crashsummary_compute.*`: the ring of seconds and its text. `crashsummary.*`: the clock, the probe and
the listener.

Hooks: `features/framephase/framephase.cpp` (`FrameBoundary` calls `NoteFrame`), `features/crashreport/zx_crashreport.cpp`
(the desktop report's extras) and `features/web/webcrash.cpp` (the page's report).
