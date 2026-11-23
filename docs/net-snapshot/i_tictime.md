# i_tictime: the tic time base

Covers: src/zandronum/src/i_tictime.h, src/zandronum/src/i_tictime.cpp, src/zandronum/src/i_tictime_wait.cpp, src/zandronum/src/i_tictime_web.cpp, src/zandronum/src/i_tictime_test.cpp, src/zandronum/src/i_tictime_wait_test.cpp

## Responsibility

The monotonic high-resolution clock every platform derives its tics and milliseconds from. Tics come
from dividing the whole elapsed span of the counter in 64-bit arithmetic, so rounding error stays
below one tic and the long-run rate is exactly `TICRATE`, whatever the timer granularity, scheduler
load or wall-clock adjustment. Why that matters for the protocol is the design doc's [Time base and
tick synchronization](design.md#8-time-base-and-tick-synchronization).

It is not inside `features/net-snapshot`: the tic clock belongs to the engine, and the snapshot code
is one of its readers. It lands in this guide because the port brought it and because the protocol's
timing rests on it.

The counter is `QueryPerformanceCounter` on Windows and `clock_gettime(CLOCK_MONOTONIC)` (in
nanoseconds) elsewhere.

## Data structures

- `tictime_raw_t`: the raw count, 64 bits by assertion; a narrower type fails the build in this
  header rather than as a confusing error in the definitions.
- The frequency, read once during static initialization.

## Public functions

- `TIC_Init`: makes the frequency's initialization explicit; safe to call any number of times, and
  covers another unit's static initializer calling in first.
- `TIC_GetRawCounter`, `TIC_GetRawFrequency`: the counter (arbitrary epoch) and its counts per
  second. The frequency is always at least `TICRATE`; a lower or failed platform value is replaced.
- `TIC_ScaleRaw`: `floor(raw * mul / frequency)`, overflow-safe at any realistic uptime: `mul` 1000
  gives milliseconds, `TICRATE` whole tics.
- `TIC_TicsToRaw`: the first raw count at which the tic counter reads a given tic.
- `TIC_ScaleRawWith`, `TIC_TicsToRawWith`: the same two with the frequency as a parameter, which is
  what the tests sweep.
- `TIC_SleepUntilRaw`: waits until the counter reaches a deadline, returning at once if it passed,
  so a caller recomputes and calls again and a short wait costs a call rather than a tic. Defined by
  `i_tictime_wait.cpp`, or by `i_tictime_web.cpp` where a page drives the loop and nothing may
  block.
- `TIC_SleepWaits`: whether this build's wait actually waits, which the browser's does not, asked at
  run time so no caller needs a browser condition to know.
- `TIC_WaitPathReport`: a function pointer called once, by the first wait that really waits, with
  the name of the path it took, so the wait file needs no console and a loop that never waits says
  nothing.

## Rules that must hold

- **Divide the whole span, never accumulate.** A tic count built by adding per-frame deltas drifts;
  `TIC_ScaleRaw(now - base, TICRATE)` cannot.
- **`TIC_ScaleRaw(TIC_TicsToRaw(t), TICRATE) == t` for every t**, which holds exactly when the
  frequency is at least `TICRATE`. The wait loops depend on it to find the next tic's boundary.
- **The split division stays.** `TIC_ScaleRawWith` divides quotient and remainder separately so the
  product cannot overflow 64 bits.
- **A wait that cannot block still answers.** The browser build's wait returns at once and says so
  through `TIC_SleepWaits`, rather than pretending to have waited.

## Engine hooks

- `win32/i_system.cpp`, `posix/sdl/i_system.cpp` and `posix/cocoa/i_timer.cpp`: `I_MSTime`,
  `I_FPSTime`, `I_GetTimePolled` and the polled wait, with `TIC_Init` at timer start-up. Each site
  carries a `[Kaori-Chani]` line.
- `features/tic-pacing/pacing.cpp`: the pacing measurement reads the counter and scales it twice, by
  `TICRATE` and by 1000, so a window's tics and its milliseconds come from one span and the figure
  is about pacing rather than about any other clock.
- `sv_main.cpp`: `TIC_GetRawCounter` where the server's own report times a pump. Zandronum has its
  own unrelated `TIC_*` family in that file for the server's statistics; only the counter call here
  is this header's.
- `d_main.cpp`: `TIC_WaitPathReport`, set so the first real wait names its path.
- The snapshot module reads the counter in `ghostactor.cpp`, `apply.cpp` and `capture.cpp`, which is
  how the `[snap]` reports measure themselves ([ghostactor.md](ghostactor.md)).

## Tests

`i_tictime_test.cpp` sweeps the round trip at several frequencies, the boundary included, and
`i_tictime_wait_test.cpp` covers the wait's own decisions; both are GoogleTest files in
`zandrox_tests`, which is the only coverage the arithmetic gets on `wasm32`, the one 32-bit target.
The engine keeps one block of its own, the time base in `formattest.cpp`, since `TICRATE` and the
platform counter need a game binary ([formattest.md](formattest.md), [selftest.md](selftest.md)).

Live, the pacing check is what says the rate holds: `tools/tic_pacing_check.py` runs the engine for
a couple of minutes in CI on all three native platforms and judges lost tics and the counter against
the logfile's stamps.
