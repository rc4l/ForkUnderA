# Diagnostics: what runs where, what it costs, and the rules for adding one

ForkUnderA measures itself heavily. This page says which of that reaches players, what it costs, and the
rules that keep a diagnostic from becoming the problem it was meant to find. Which tool answers which
question is in the skills (`perf-telemetry`, `game-thread`, `zandronum-session`), and each feature's README
lists its own counters and hooks; this page does not repeat them.

## In every build, players' included

- **Telemetry** (`features/telemetry`): counters, timed regions (`ZX_ZONE`), gauges read on a period, the
  flight recorder and the hitch log. Its gauge reads are a timed region of their own, `telemetry/gauges`.
  Flying a heavy Sunder MAP11 fight it cost about 0.2% of the main thread.
- **Seams** (`features/handoff`): the game-thread checks on every crossing (`ZX_SEAM`), counting violations by
  site. Under 0.1% of the main thread in the same fight; the stress mode is off unless asked for.
- **Heavy readings on demand only**: walking the allocator's heaps (`mem/heap_bytes` and its kin) holds their
  locks for tens of milliseconds, so it runs for a soak, `fua_memsnap`/`fua_memdiff`, or `fua_telemetry_heaps 1`,
  never on a timer.
- **Off unless switched on**: the frame and view traces (`fua_viewtrace`), the checks against GL and the live
  game (`fua_vis_check`, `fua_sprite_check`, `fua_psprite_check`, `fua_shading_check`, `fua_copy_check`), the
  thinker timings (`fua_telemetry_thinkers`), the game-thread stress mode and the `fua_debug_*` aids.

Instant replay (`features/replay`, `cl_fua_replay`) is a player feature, not a diagnostic, and the largest
always-on cost of this kind: about 2% of the main thread in that fight.

## In development builds only

The control bridge and everything on it (`FUA_MCP_BRIDGE`, off by default and never shipped): `fuactl`'s
remote control, the in-engine sampler, the tic profiler, the sim trace, the hang watchdog and its crash files.

## Outside the game

`fuactl` and its fixtures and scenarios, the tripwires under `tools/`, the unit tests and the
ThreadSanitizer job.

## Rules for adding one

Each rule is a mistake this project made and paid for.

- **Time what runs in normal play.** A diagnostic on a timer or a frame gets a `ZX_ZONE`, so its cost shows
  in `hitches` and `record`. The heap walk behind the memory gauges stalled a frame by 50 ms twice a second
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
