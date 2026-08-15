# fuactl

Drive the engine from the outside. Launch it, step it one tic at a time, read its world, measure its frames. Dev builds only — release builds do not contain the bridge.

```mermaid
flowchart LR
    subgraph You
        CLI[fuactl CLI]
        AI[agent / script]
    end

    CLI -- "NDJSON over TCP" --> B
    AI -- "MCP" --> CLI

    subgraph Engine["dev engine (FUA_MCP_BRIDGE=ON)"]
        B[bridge]
        B --> SIM["sim clock<br/>pause · step · cheat at tic N"]
        B --> STATE["world state<br/>actors · hashes · RNG streams"]
        B --> PROF["profilers<br/>per-tic ms · worst frames · GPU passes"]
        B --> TRACE["event tracer<br/>every damage / kill / spawn"]
        B --> UI["input & UI<br/>keys · menus · screenshots"]
    end

    R["release engine"] x--x B2["no bridge<br/>zero symbols"]
```

## Quick start

```sh
ZX_MCP_BRIDGE=1 ./mac_compile.sh              # build a driveable engine
cd tools/fuactl && npm install
npx fuactl launch --map map01 --seed 777      # prints port + token
npx fuactl rpc sim.pause --port <P>
npx fuactl rpc sim.step '{"tics":1}' --port <P>
```

`fuactl mcp` runs the same surface as an MCP server for agents.

## What it does

| Area | RPCs | Use |
|---|---|---|
| Sim control | `sim.pause` `sim.step` `sim.cheatat` | run exactly N tics; fire a cheat at an exact tic |
| Determinism | `sim.hash {scope:"world"}` `sim.rngdump` `sim.trace` | compare two runs: world hash, every RNG stream, every damage/kill/spawn |
| Profiling | `perf.ticprof` `perf.capture` `gl.timers` | what is inside a slow tic; what composes a spike frame; GPU ms per pass |
| State | `sim.tic` `state.player` `state.actors` | leveltime, positions, health, class names |
| Driving | `console.exec` `input.event` `ui …` | commands, keys, menu reading, screenshots |

## Why the odd ones exist

- `sim.cheatat` — console cheats execute at a wall-clock-dependent tic. Pinning them makes two runs comparable.
- `sim.hash` skips dynamic-light actors — their population follows the renderer, and hashing them makes identical sims look different.
- `sim.trace` — diff two trace files; the first differing line is the event that diverged.

## Release builds

The bridge is off unless `ZX_MCP_BRIDGE=1` at build time. Off means: no sockets, no RPC code, every engine anchor compiles to nothing, `nm` finds zero bridge symbols.

## Where

| What | Path |
|---|---|
| CLI | `tools/fuactl/` |
| Engine side | `src/zandronum/src/mcp_*.{h,cpp}` |
| Full RPC reference | `src/zandronum/src/features/mcp-bridge/README.md` |
