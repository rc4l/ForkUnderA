# fuactl

Reproducing a bug or a lag spike by hand is guesswork: no two runs are alike, and you cannot see inside a slow frame. fuactl fixes that by making a dev engine fully scriptable. Launch it, run it one tic at a time, read its world, measure its frames, and get the exact same run every time.

```mermaid
%%{init: {"theme":"base","themeVariables":{"fontSize":"14px","lineColor":"#64748b"},"flowchart":{"curve":"basis","nodeSpacing":45,"rankSpacing":60}}}%%
flowchart LR
    AI([agent]) -->|MCP| CLI([fuactl CLI])
    CLI ==>|TCP| B{{bridge}}

    subgraph DEV["dev build · ZX_MCP_BRIDGE=1"]
        direction LR
        B --> SIM("sim clock<br/>pause · single step")
        B --> STATE[("world state<br/>actors · hashes · RNG")]
        B --> PROF("profilers<br/>tic ms · frames · GPU")
        B --> TRACE[/"event tracer<br/>damage · kills · spawns"/]
        B --> UI[/"input and UI<br/>keys · menus · screenshots"/]
    end

    REL("release build<br/>no bridge · zero symbols")

    classDef you fill:#dbeafe,stroke:#3b82f6,stroke-width:2px,color:#1e3a5f
    classDef hub fill:#fef3c7,stroke:#f59e0b,stroke-width:2px,color:#78350f
    classDef cap fill:#d1fae5,stroke:#10b981,stroke-width:2px,color:#064e3b
    classDef off fill:#fee2e2,stroke:#ef4444,stroke-width:2px,color:#7f1d1d,stroke-dasharray:6 4
    class AI,CLI you
    class B hub
    class SIM,STATE,PROF,TRACE,UI cap
    class REL off
    style DEV fill:#f8fafc,stroke:#94a3b8,stroke-width:1px,color:#475569
```

## Quick start

```sh
ZX_MCP_BRIDGE=1 ./mac_compile.sh              # build a driveable engine
cd tools/fuactl && npm install
npx fuactl launch --map map01 --seed 777      # prints port + token
npx fuactl rpc sim.pause --port <P>
npx fuactl rpc sim.step '{"tics":1}' --port <P>
```

`npx fuactl mcp` exposes the same surface as an MCP server for agents.

## What it does

| Area | RPCs | Use |
|---|---|---|
| Sim control | `sim.pause` `sim.step`, etc | run exactly N tics, every time |
| Determinism | `sim.hash` `sim.trace`, etc | compare two runs: world hash, every RNG stream, every damage/kill/spawn |
| Profiling | `perf.ticprof` `perf.capture`, etc | what is inside a slow tic; what composes a spike frame; GPU ms per pass |
| State | `sim.tic` `state.actors`, etc | leveltime, positions, health, class names |
| Driving | `console.exec` `ui …`, etc | commands, keys, menu reading, screenshots |

## Release builds

The bridge is off unless `ZX_MCP_BRIDGE=1` at build time. Off means: no sockets, no RPC code, every engine anchor compiles to nothing, `nm` finds zero bridge symbols.

## Where

| What | Path |
|---|---|
| CLI | `tools/fuactl/` |
| Engine side | `src/zandronum/src/mcp_*.{h,cpp}` |
| Full RPC reference | `src/zandronum/src/features/mcp-bridge/README.md` |
