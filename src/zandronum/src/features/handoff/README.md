# features/handoff

The line between the game and the main thread. Tics run on their own thread (`fua_gamethread`) or inline, and
either way the main thread touches game data only in the handoff, the stretch between tics; everything else it
draws comes from `features/gamecopy`'s copy, and everything a tic sends out goes through a queue.

- `seam.h`/`.cpp`: `ZX_SEAM("group/name", Owner)` declares a crossing once, and that one line checks the thread
  entering it (`computation/handoff_compute`'s `ComputeOwnerAllows`), times it as a telemetry counter of the same
  name, records it as the seam the thread is in for a crash report and the hang watchdog, beats the thread's
  heartbeat, and under `fua_gamethread_stress <us>` (seeded by `fua_gamethread_stress_seed`) delays about one entry
  in eight to shake out what only breaks when the threads interleave differently. `ZX_SEAM_UNTIMED` is the same
  without the timing, for crossings too frequent to time. `fua_seams` lists every seam with its owner, entries and
  violations; `handoff/seam_violations` counts them, and a soak fails when it grows.
- Owners: `Main` (the main thread), `GameThread` (the thread running tics, which is the main thread while they run
  inline), `GameData` (a running tic, or the main thread in the handoff), `Handoff` (the main thread between tics)
  and `Any`. Until the loop sets the phase, the main thread counts as always in the handoff.
- `computation/lock_compute`: `zx::Lock`, the one lock for state both threads touch, named, counting the takes that
  waited, and reporting a thread that takes one while it holds another, so no two can be taken in opposite orders.
- `computation/queue_compute`: `zx::Queue<T>`, the one queue that carries work between the threads: values only,
  drained oldest first, each listed with its depth and high-water mark.

Rules the boundary keeps are in `tools/boundary-rules.json`, checked by `tools/boundary_tripwire.py` against
allowances in `tools/boundary-allow.json` that may only shrink.

## In-engine hooks

- `d_main.cpp` `D_DoomMain`: `NoteMainThread` first, so every seam's check knows which thread is main.
- `mcp_crash.cpp`: every crash, abort and hang file names the seam each thread was in (`seams:` line), which
  `fuactl crashes` prints.
- `features/crashreport/zx_crashreport.cpp` `zx_before_send`: the `zx_seam_main` and `zx_seam_game` tags.
- `tools/zone-tripwire.sh`: seam names follow the telemetry counter rules and are declared at one site each.

<!-- boundary:begin (python3 tools/boundary_tripwire.py --doc writes this section) -->
## What crosses the boundary

Generated from the field tables and the `ZX_SEAM` declarations; do not edit by hand.

Copied by `features/gamecopy/fields.h`:

- line: `flags`, `special`, `Alpha`, `args`
- sector: `planes`, `floorplane`, `ceilingplane`, `ColorMap`, `special`, `lightlevel`, `MoreFlags`, `Flags`, `reflect`
- side: `textures`, `Light`, `Flags`
- vertex: `x`, `y`

Seams:

- `copy/blend` (Main), `features/gamecopy/gamecopy.cpp`
- `copy/fill` (Handoff), `features/gamecopy/gamecopy.cpp`
<!-- boundary:end -->
