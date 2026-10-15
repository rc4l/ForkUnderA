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
node src/cli.mjs soak --cycles 6 --port 7799           # reload the level six times; exit 1 if anything kept growing
node src/cli.mjs console "fua_allocsites start" --port 7799
node src/cli.mjs soak --cycles 3 --port 7799
node src/cli.mjs console "fua_allocsites report 15" --port 7799   # the call stacks still holding the most
```

- `soak --action "<console text>"` repeats anything, not only a reload.
- A soak judges every gauge and breakdown reading in bytes, objects, handles or threads, the backend's GPU memory by tag and in a browser what the page holds among them, except those registered with `soaked` false, such as a peak, which only ever grows. Its verdict names each counter that grew and by how much a cycle.
- `fua_allocsites` lists the allocations of the current level too. A leak is a stack whose count is a multiple of the cycles run.
- GPU memory in three views:
  - `gpu/mem/tag/` is what the backend made.
  - `gpu/vk_budget_used_bytes` is what the driver says the process holds, Diligent's own allocations included.
  - In a browser, `gpu/page/` is what the page holds and has neither destroyed nor collected.
  - A `gpu/page/` tag above its `gpu/mem/tag/` twin is an object left for the garbage collector. That was queue #48.

## In a browser

Headless Chrome (always muted) reproduces what the page holds:

- `node tools/web/check.mjs dist-web --only "holds no more memory"` runs the reload soak.
- `node tools/web/probe.mjs <staged dir> --query "iwad=...&file=...&map=MAP11" --soak reload --cycles 4 --settle 9000` soaks a player's own map, and `--run "map MAP11; wait 9000; fua_stats gpu/"` prints what the commands printed.
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
