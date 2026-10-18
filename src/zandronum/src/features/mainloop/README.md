# mainloop

The main loop as steps that a driver can call, so the same loop runs on the desktop, where it repeats forever, and in a browser, where it gets one call per animation frame and may never wait.

- `mainloop.h`:
  - `SetDriver()` installs a driver that calls one frame at a time from outside, as a browser's main loop does, and `Stepped()` says whether one is installed.
  - When a driver is installed, `D_DoomLoop` hands `D_RunFrame` to it through `StartStepped()`. That throws `SteppedLoopStarted` to unwind `D_DoomMain` back to the driver's caller.
  - With a driver, a wipe advances once a frame instead of looping until it ends.
  - `Quit()` waits out the quit sound and ends on the desktop. On a stepped loop, `FinishPendingQuit()` ends the program at the first frame after the sound's time.
- `computation/mainloop_compute.h`: the timing decisions, tested (`ComputeWipeTicsDue`, `ComputeVblMilliseconds`, `ComputeDeadlinePassed`), and when the engine is at rest after a wipe (`ComputeFramesSinceWipe`, `ComputeAtRestAfterWipe`), which a soak mark waits for.

## In-engine hooks

- `d_main.cpp`:
  - `D_RunFrame` is one pass of `D_DoomLoop`, which now repeats it, or hands it to the driver.
  - `D_Display` starts a wipe and runs it to its end through `D_WipeStep` unless the loop is stepped.
  - `D_WipeStep` is one tic of a running wipe, and `D_WipeRunning` says whether one is running.
  - `D_RunFrame` calls `FinishPendingQuit` first, and gives a running wipe the whole frame.
  - `D_RunFrame` counts the frames since a wipe ended, and telemetry's `SetSoakReady` asks whether the engine is at rest.
- `menu/messagebox.cpp`: the quit menu's yes calls `Quit`, with the quit sound's wait.
- `posix/sdl/i_main.cpp`: `main` returns once a driver has the loop (`SteppedLoopStarted`).
- `d_net.cpp`: `TryRunTics` on a stepped loop neither waits for a tic nor runs one before it's due. It returns, and the driver calls again next frame.
