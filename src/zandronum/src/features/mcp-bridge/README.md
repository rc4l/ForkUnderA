# features/mcp-bridge — the native "programmable engine" control bridge

An in-engine control plane that turns ForkUnderA into a **typed, inspectable, deterministic simulator**
an agent (or CLI, or CI) can drive over a loopback socket. Formerly an injected overlay from the
ZandronumMCP repo; now first-class engine code, rewritten around a typed RPC + event protocol with
determinism controls, structured state, and self-supervising lifecycle.

## Build gating — never in releases

Compiled **only** when the CMake option `FUA_MCP_BRIDGE` is `ON` (default `OFF`). A release binary
therefore contains **zero** bridge code and no remote-control surface. Turn it on for a dev build:

```
ZX_MCP_BRIDGE=1 ./mac_compile.sh        # -> -DFUA_MCP_BRIDGE=ON
```

When OFF, `mcp_bridge.h` makes the entry points `inline` no-ops, so the two call-site anchors stay
byte-identical and simply vanish.

## Runtime safety (even in a dev build)

- **Opt-in**: the listener starts only when `ZANDRONUM_BRIDGE_PORT` is set.
- **Loopback only**: binds `127.0.0.1`.
- **Token**: if `ZANDRONUM_BRIDGE_TOKEN` is set, a client must present it before any command runs, so no
  other local process can drive an armed bridge.
- **Self-supervised lifecycle** (see below).

## Files

| File | Role |
|------|------|
| `mcp_bridge.cpp` | Transport + lifecycle. **Engine-free** (no engine headers). Loopback socket, request queue, `SIGTERM/SIGINT`→clean-quit, pidfile registry, orphan watchdog. |
| `mcp_rpc.cpp` | The RPC dispatch + handlers (**engine-facing**): determinism, structured state, input, run on the game thread. |
| `mcp_bridge.h` | Entry points; inline no-op stubs when `FUA_MCP_BRIDGE` is off. |
| `mcp_crash.cpp` / `mcp_event.cpp` / `mcp_hud.cpp` / `mcp_renderinfo.cpp` | Backends: crash backtrace, input injection, HUD/frame capture, renderer info. |
| `computation/mcprpc_compute.{h,cpp,_test.cpp}` | Pure, unit-tested core: NDJSON framing, JSON field extraction, the FNV state-hash mixer, step planning. |

## In-place anchors (the only edits outside this feature)

1. `d_main.cpp` — `#include "mcp_bridge.h"` + `MCP_Bridge_Poll();` at the top of each `D_DoomLoop`
   iteration (before the `NETWORK_GetState()` switch). Runs every frame in all net modes.
2. `c_console.cpp` — `#include "mcp_bridge.h"` + `MCP_Bridge_TeeOutput(outlinecopy);` after `I_PrintStr`,
   mirroring every console line as an `out` event.

Both compile to nothing when the bridge is off.

## Wire protocol v2 (NDJSON over loopback TCP)

```
request  {"id":<int>,"cmd":"<name>","args":{...}}
response {"id":<int>,"ok":true,"result":{...}}  |  {"id":<int>,"ok":false,"error":"<msg>"}
event    {"t":"event","event":"<name>","data":{...}}
hello    {"t":"hello","engine":"forkundera","bridge":"2.0.0","pid":N,"caps":[...]}
```

### Commands

- `ping`, `capabilities` (self-describing), `console.exec {text}`
- **Determinism**: `sim.tic`, `sim.hash`, `sim.seed {op:get|set,value}`, `sim.pause`, `sim.resume`,
  `sim.step {tics}`, `sim.snapshot {slot}`, `sim.restore {slot}`
- **Structured state**: `state.player`, `state.actors {limit}`
- **Input**: `input.event {evtype,subtype,data1,data2}`
- **Perf**: `perf.capture {frames}` → async `perf` event with p50/p95/p99 + 1%-low fps and the coarse
  sim/render split; `perf.counters` → actor/segment counts. The sim|render boundary is one anchor
  before `D_Display` (no markers in the hot render path). Use `fuactl perf-ab` for a deterministic
  ablation — measure a scene, perturb it with everything else held constant, diff → a *causal*
  frametime delta attributed to CPU (sim) vs GPU-ish (render).

`sim.hash` mixes `level.time` + the RNG position + every actor's transform/health (FNV-1a) into a stable
fingerprint. Two instances at the same `level.time` with identical simulation return the same value; a
mismatch is a desync — this is what makes cross-instance session checks possible.

## Lifecycle — no more hanging instances

- **Clean quit**: `SIGTERM`/`SIGINT` set a flag honored on the game thread, which calls `exit(0)` →
  the engine's full `atexit`/`call_terms` teardown (GL/SDL/Cocoa). No mid-render hard kill, so no macOS
  window-server wedge.
- **Registry**: every armed instance writes `~/.forkundera/instances/<pid>.json` on startup and removes
  it on clean exit — discoverable however it was launched.
- **Watchdog**: if launched with `ZANDRONUM_BRIDGE_PARENT_PID`, the engine self-quits (cleanly, then
  hard as a last resort) when its controller dies.
- **Reaper**: `fuactl reap` reads the registry, SIGTERMs live instances, and prunes dead entries — so the
  registry is self-healing.

## Driving it — fuactl

The companion tool `tools/fuactl` is the CLI + MCP frontend over this bridge:

```
fuactl session --instances 2 --seed 42 --map MAP01   # determinism + desync check
fuactl launch --map MAP01                             # one supervised instance
fuactl rpc sim.hash --port <p> --token <t>            # one raw RPC
fuactl reap --kill                                    # reap orphans
fuactl mcp                                            # MCP stdio server for agents
```

## Supersedes

This replaces the injected `engine-bridge/` overlay + `apply-bridge` in the ZandronumMCP repo (now
redundant for ForkUnderA) and the old console-scrape readers (`mcp_actorstate.cpp`, the `mcp_*vars.inc`
lumps), whose actor reads are now first-class `state.*` RPCs.
