# features/frame-limit

`vid_maxfps` and `cl_capfps` held as a frame rate on every renderer and in the browser.

## Why

Measured on 2026-10-21 with the backend drawing alone and vsync off: `vid_maxfps 60` left frames at
0.32 ms, as uncapped, and `cl_capfps 1` did too. The old limiters armed a timer that only the
Windows D3D9 and DirectDraw framebuffers waited on (Linux's semaphore had no waiter, macOS's and the
browser's were empty), and `cl_capfps`'s wait sits in `TryRunTics`, which a frame drawn beside a
game-thread tic never reaches.

## Upstream

UZDoom `9f758b0032` (implement vid_maxfps) and `9f0f659db0` (remove the old implementations),
adapted rather than ported:

- The wait is upstream's `DFrameBuffer::FPSLimit`: sleep to 2 ms before the deadline, then yield
  and look again, just before each present. Each deadline is a period after the last one rather
  than after the last wake, so the rate holds instead of drifting under the limit.
- `cl_capfps` limits to `TICRATE` here, where upstream skips the limit for it, because upstream's
  tic wait applies to every frame and ours does not.
- A stepped loop (a browser, `features/mainloop`) cannot wait, so it skips an animation frame
  that is not due yet, counting one within 2 ms of its deadline as due.

`computation/framelimit_compute` holds the decisions: which limit applies, each deadline, the
sleep, and a browser frame's due test.

## In-engine hooks

- `framelimit.cpp` declares the one `vid_maxfps`, which `win32/win32video.cpp`,
  `posix/sdl/hardware.cpp` and `posix/cocoa/i_video.mm` each declared with its own timer before.
- Before each present, `WaitBeforePresent`:
  - `features/hwrender/diligent/dgscene.cpp` `PresentFrame`, inside its present phase, so the wait
    counts as present time rather than work;
  - `gl/system/gl_framebuffer.cpp` before `SwapBuffers`;
  - `win32/fb_d3d9.cpp` and `win32/fb_ddraw.cpp` where they waited on the old timer.
- `features/web/webdriver.cpp` `Frame`: `SteppedFrameDue` first, skipping the frame when false.
- `d_net.cpp` `cl_capfps` is a plain cvar, its timer switching gone; `posix/sdl/i_fpslimit.cpp`
  is deleted.
- `frame/limit_wait` times the wait for traces (`fuactl record`).
