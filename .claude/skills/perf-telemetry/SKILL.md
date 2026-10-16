---
name: perf-telemetry
description: How to diagnose a frame-rate, stutter, memory-growth or out-of-memory report in ForkUnderA with features/telemetry, on desktop or in the browser. Use when a report says choppy, hitching, slow, leaking, "out of memory", or growing over time; when you need to know what a frame or a level load costs; or before claiming a change made something faster or smaller.
---

# Diagnosing performance and memory with telemetry

Every timing and memory count the engine keeps is in one registry (`src/zandronum/src/features/telemetry/README.md`), read the same way on Windows, Linux, macOS and in a browser. Measure first. Every cause in this repo that was reasoned out rather than measured turned out wrong.

Run from `tools/fuactl` as `node src/cli.mjs …` against your own locked instance (`play --lock --port 7799`), never the user's.

## What a frame costs

```bash
node src/cli.mjs stats frame/ --port 7799    # the frame's own times: present to present, each renderer's share, tics
node src/cli.mjs stats phase/ --port 7799    # where the frame went, by features/framephase's phases
node src/cli.mjs stats gpu/ --port 7799      # GPU time per pass and GPU memory
node src/cli.mjs hitches 10 --port 7799      # slow frames and the regions they spent most in
node src/cli.mjs stats --port 7799           # every counter there is; the groups are named in the telemetry README
```

- Each row gives last, mean, p99 and max over the recent frames. A stutter is a p99 far above the mean. No average shows one.
- `fua_telemetry_thinkers 1` adds per-class thinker time under `sim/thinker/`.
- `record --seconds 10 --name X` writes every counter for every frame, plus every timed region on every thread, under the engine's `telemetry/` folder. Open `X-trace.json` in Perfetto to see the job lanes against the game thread.
- To compare two settings, `abperf` them inside one session. Never compare two launches.
- To judge a change, use `perfgate NAME --spot S`. Its first run saves this machine's baseline under `~/.forkundera/baselines/`, and later runs fail on a regression. `compare a.json b.json` sets two `stats --json` files side by side.
- A frame time that drops for no reason is an empty world until a shot proves otherwise.

## What memory does

```bash
node src/cli.mjs memsnap before --port 7799
# ...do the thing...
node src/cli.mjs memdiff before --port 7799            # what changed, biggest first
node src/cli.mjs soak --cycles 6 --port 7799           # reload the level six times; exit 1 if anything kept growing or got slower
node src/cli.mjs soak --cycles 8 --warmup 2 --sites --port 7799   # and the allocation stacks that grew from cycle 3 to cycle 8
```

- `soak --action "A|B|..."` repeats any console steps in order, waiting on each that starts a level (`levelStarted` in `telemetry.mjs` says which) and marking once a cycle, so every mark lands on the same map. Each cycle's step time and frame time print, and a later half slower than the earlier fails it.
- Recipes the restart and hosting audit used:
  - Rapid WAD changes: `--action "wad_reload <other iwad> map=E1M1|wad_reload <iwad> <pwad> map=MAP01"` (forward slashes); with no `map=`, the restarts stay at the title.
  - Self-hosting: `--action "fua_hostmap MAP01"`. A server: launch one with `launch --arg "-host,-port,10670"`, then `soak --port <client> --judge-port <server> --judge-token T --action "connect 127.0.0.1:10670|disconnect"` judges the server while the client joins and leaves.
  - Resizing: `--action "check: resize 900x700|check: resize 1340x860"` sizes the game window from outside as a drag does (`window --resize WxH` once), since `vid_setmode` leaves a window's size alone; fullscreen: `--action "fullscreen 1|fullscreen 0"`, which covers the user's screen while it runs.
- Turn on `fua_render_in_background` for a soak (scenarios do): a window without focus presents nothing, and the backend frees what it released only after a present, so it reads as a leak.
- Private bytes settle over the first sixty or so restarts (the heap's commit and the driver's caches) and level off; judge a restart soak with `--ignore mem/private_bytes` and live allocations, heap bytes, GPU memory and handles, or run eighty restarts and watch the plateau. `mem/win/` and `mem/heaps_committed_bytes` say whether growth sits in a heap or outside every one.
- A soak judges every gauge and breakdown reading in bytes, objects, handles or threads, the backend's GPU memory by tag and in a browser what the page holds among them, except those registered with `soaked` false, such as a peak, which only ever grows. Its verdict names each counter that grew and by how much a cycle.
- `fua_allocsites` lists the allocations of the current level too, so one report proves little; `--sites` diffs two, where a leak's stack grows with every cycle and the level's stay put.
- GPU memory in three views:
  - `gpu/mem/tag/` is what the backend made.
  - `gpu/vk_budget_used_bytes` is what the driver says the process holds, Diligent's own allocations included.
  - In a browser, `gpu/page/` is what the page holds and has neither destroyed nor collected.
  - A `gpu/page/` tag above its `gpu/mem/tag/` twin is an object left for the garbage collector. That was queue #48.

## In a browser

Headless Chrome (always muted) reproduces what the page holds:

- `node tools/web/check.mjs dist-web --only "holds no more memory"` runs the reload soak.
- `node tools/web/probe.mjs <staged dir> --query "iwad=...&file=...&map=MAP11" --soak reload --cycles 4 --settle 9000` soaks a player's own map, and `--run "map MAP11; wait 9000; fua_stats gpu/"` prints what the commands printed. A soak step `check: resize 900x700` resizes the page; `fullscreen true|fullscreen false` switches it.
- A `wad_reload` reloads the page; nothing in the engine survives that, so what to watch across reloads is the page's stored data (`navigator.storage.estimate()`), which holds the config and the player's WADs.
- The check "lets go of" compares the page's view with the engine's.
- Headless Firefox has no WebGPU. When a Firefox player reports a problem, ask them to type these in the console and paste what they print:
  - `fua_flightdump` writes the last frames, seconds and hitches as downloads and prints a summary.
  - `fua_hitches`.
  - `fua_memdiff` after a `fua_memsnap`.
- A crash report's "Last minute" section already holds the summary.

## Adding a counter

- `ZX_ZONE("group/name")` times a scope.
- `ZX_COUNT("group/name", "unit", n)` adds to a per-frame total.
- `Register(CounterSpec{ ... }, read)` makes a gauge read a few times a second. Pass `false` as its `soaked` field for a level a soak should not judge.
- Names are lower-case `group/name` literals, and each timed region's name is used at one site only (`tools/zone-tripwire.sh`).
- List the hook in `features/telemetry/README.md`.
