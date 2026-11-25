# Diagnostics: what runs where, what it costs, and the rules for adding one

ForkUnderA measures itself heavily. This page says which of that reaches players, what it costs, and the
rules that keep a diagnostic from becoming the problem it was meant to find. Which tool answers which
question is in the skills (`perf-telemetry`, `game-thread`, `zandronum-session`), and each feature's README
lists its own counters and hooks; this page does not repeat them.

## Public builds (`FUA_DIAGNOSTICS` off, the default)

A build for players carries none of the measuring below. `FUA_DIAGNOSTICS` is off unless a build asks for it, and
when it is off telemetry's calls are inline no-ops the compiler drops, and `ZX_COUNT` never evaluates its amount.
Measured with `-timedemo` on the Sunder MAP11 Complex Doom lap (2026-11-21, three rounds each), a public build played it
3.5% faster than a diagnostics one (66.7 s against 69.1 s). What stays is what a player uses or a crash report needs:

- **The crash summary** (`features/crash-summary`): one clock read a frame and, once a second, the OS probe's light
  readings (no heap walk), for the last minute's frame times, stalls and memory a crash report sends.
- **`vid_fps` and its phase split** (`features/framephase`), and the GPU frame time dynamic resolution steers by.
- **Off unless switched on**: the checks against GL and the live game (`fua_vis_check`, `fua_sprite_check`,
  `fua_psprite_check`, `fua_shading_check`, `fua_copy_check`), the frame and view traces and the `fua_debug_*` aids,
  each a cvar check while off.

Instant replay (`features/replay`, `cl_fua_replay`) is a player feature, not a diagnostic, and the largest
always-on cost of this kind: about 2% of the main thread in a heavy Sunder MAP11 fight.

## Diagnostics builds (`FUA_DIAGNOSTICS` on)

Test and measurement builds: `ZX_DIAGNOSTICS=1` for the compile scripts, `-DFUA_DIAGNOSTICS=ON`, or the control
bridge, which reads them and turns the flag on. Never shipped.

- **Telemetry** (`features/telemetry`): counters, timed regions (`ZX_ZONE`), gauges read on a period, the
  flight recorder, the hitch log, the census and the console commands that read them. Its gauge reads are a timed
  region of their own, `telemetry/gauges`. Flying a heavy Sunder MAP11 fight it cost about 0.2% of the main thread.
- **Allocation tracking**: `allocwatch.cpp`'s global `operator new`/`delete`, and the backend's ledger of GPU objects.
- **Seams** (`features/handoff`): the game-thread checks on every crossing (`ZX_SEAM`), counting violations by
  site. Under 0.1% of the main thread in the same fight; the stress mode is off unless asked for.
- **Timings outside the registry**: the job pools' observer, the latency stamps (`features/latency`) and the
  thinker timings (`fua_telemetry_thinkers`).
- **Heavy readings on demand only**: walking the allocator's heaps (`mem/heap_bytes` and its kin) holds their
  locks for tens of milliseconds, so it runs for a soak, `fua_memsnap`/`fua_memdiff`, or `fua_telemetry_heaps 1`,
  never on a timer.
- **The control bridge** (`FUA_MCP_BRIDGE`, off by default): `fuactl`'s remote control, the in-engine sampler,
  the tic profiler, the sim trace, the hang watchdog and its crash files.

## Outside the game

`fuactl` and its fixtures and scenarios, the tripwires under `tools/`, the unit tests and the
ThreadSanitizer job.

## Rules for adding one

Each rule is a mistake this project made and paid for.

- **A diagnostic costs a public build nothing.** It reports through telemetry's calls and macros, or sits behind
  `FUA_DIAGNOSTICS`; a runtime switch of its own still costs a check on every pass.
- **Time what runs in normal play.** A diagnostic on a timer or a frame gets a `ZX_ZONE`, so its cost shows
  in a diagnostics build's `hitches` and `record`. The heap walk behind the memory gauges stalled a frame by 50 ms twice a second
  and hid because nothing timed it.
- **Expensive reads are on demand.** Heap walks, thread enumeration and readbacks run when a tool asks, not on
  a period in normal play.
- **A query never acts.** Anything a frame or a job asks must leave the world alone: the sprite build asked
  `P_CheckTickerPaused`, which also paused the sound device, thousands of times a second from the job threads.
- **A fallback is counted.** Code that degrades (a table that overflows, a path that gives up) counts it and
  says so in its stats; the light cluster table silently gave up above 1080p and every pixel paid for every
  light.
- **A measurement reads its setting back.** An A/B checks each setting took before it measures (`abperf`
  does); a command after a `load`'s `wait` dies with the level's thinkers and measures nothing.
- **A number in a comment is a number in a test.** "8192 cells covers a 4K screen" was off by a factor of
  six and no test held it.
