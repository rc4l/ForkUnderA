---
name: zandronum-session
description: Launch and drive a bridge-enabled ForkUnderA dev engine with the fuactl CLI. Use when the user wants to start, run, test, profile, or screenshot the engine — loading a map, measuring frametimes or GPU pass times, comparing GL against Vulkan, reading the UI, or stepping the sim deterministically.
---

# Drive the engine with `fuactl`

`fuactl` (`tools/fuactl/`) is the only supported way to launch and drive a dev engine. It supervises
the instance, allocates the port and auth token, isolates the config, and exposes every bridge RPC.
**Do not launch the engine binary by hand and do not use the old `zandronum` MCP server** — both are
superseded, and the hand-rolled launch wrote to the user's real config file (clobbering their mouse
settings on exit).

Run it as `node src/cli.mjs <command>` from `tools/fuactl` — Node 18+, no `npm install` needed. It is
the same on every OS; the few platform differences are called out where they matter.

## Preconditions

1. **A bridge-enabled build must be staged** where fuactl looks: `dist-windows/forkundera.exe`,
   `build/ForkUnderA.app/Contents/MacOS/forkundera`, or `dist-linux/forkundera`.
   `FUACTL_ENGINE=/abs/path` runs any other binary. Build and stage with the `zandronum-rebuild` skill.

2. **Game data is found from the binary's folder**, which is the engine's working directory; GL
   captures land in its `sweep/`.

## Launch

```bash
cd tools/fuactl
nohup node src/cli.mjs play --map MAP01 --lock --port 7799 --res 1280x720 > play.log 2>&1 &
until [ -s .play-session ]; do sleep 0.5; done; cat .play-session    # PORT= and TOKEN=
node src/cli.mjs ready                                               # returns once it answers
```

Delete a stale `.play-session` before launching, or the wait above reads the previous launch's.

- `play` is the launch to use: Vulkan on, freelook, no monsters, cheats verified. `--lock` keeps the
  engine off the user's mouse and keyboard — always on your own launches, never on one the user plays.
- Pick a port of your own (7799, 7796, ...), never the one the user's instance is on.
- `--file a.pk3,b.wad` for pwads, `--iwad` for another IWAD (an absolute path works), `--gl` for GL
  only, `--side-by-side` for the user's two-window session (see `fua-side-by-side`).
- `--preset ID [--variant V]` loads a `catalogue/<ID>/addon.json` wad set from the download store
  (`--store DIR`, else `FUACTL_PWADS`, else the engine's own `pwads/` folder). A missing file is named;
  fetch it with `fua_download <file>` in the engine console.
- `.play-session` is written by whichever launch went last. Keep each instance's token yourself.
- It stays up until stopped. Quit by port: `ui exec quit --port P --token T`. Never `reap --kill` or
  `reap --all` — the registry is shared with other sessions — and never kill by process name
  (`Stop-Process -Name`, `pkill`), which takes the user's instance and other harnesses with it.
- `fua_dg_standalone` defaults on, which stops GL deriving the scene: set `fua_dg_standalone 0` before
  any GL/Vulkan comparison, or GL's half is a still frame.

## Drive

Every command takes `--port P --token T`, and without them uses the last launch's `.play-session`; a
port with no token hangs and reports `rpc timeout`.

| goal | command |
|---|---|
| console command, with its output | `ui exec "<ccmd>"` |
| any RPC | `rpc <cmd> '<jsonArgs>'` — e.g. `rpc player.camera`, `rpc state.actors '{"limit":5000}'` |
| where the camera is; save it as a spot | `here [--save NAME --note TEXT]` (see `fua-capture-spot`) |
| stand at a saved spot | `goto --spot NAME` |
| does a render cvar change the picture | `ab <cvar> <a> <b> --spot NAME [--box x0,y0,x1,y1]` |
| matched GL/Vulkan pair, sim frozen | `shot <tag> --spot NAME` or `--at x,y,z --face yaw,pitch` |
| what the crosshair is on, and the mesh piece drawing it | `look --at x,y,z --face yaw,pitch` |
| what a linedef is, or every line near a point | `line <index>` or `line --at X,Y --radius R` |
| a map's sectors, lines, specials and ACS source, from its files | `mapdata <MAP> --preset ID [--sector N] [--line N] [--scripts REGEX]` |
| compare two captures | `png --diff a b`, `png --boxdiff a b x0 y0 x1 y1 [block]`, `png --diffimg a b out` |
| frametime distribution (p50/p95/p99) | `capture [--frames N]` |
| GPU ms per pass | `gl-timers` (check `renderer-info` first; no timer queries reads as zeros) |
| pause / step the sim | `rpc sim.pause` · `rpc sim.step '{"tics":1}'` |

`png --boxdiff` compares in 16-pixel block means: the renderers frame the world a fraction of a pixel
apart, which reads ~13 per pixel on a bright tiled floor while both are right.

## Captures lie under these conditions

- **Another engine window on screen.** GL reads its picture back from its window, and where another
  window covers it the pixels are undefined. `shot` warns when more than one engine is running.
- **Vulkan on, some skybox maps.** GL captures on the horizon template's MAP02 came back stale, black
  or scrambled run to run in old and new builds alike; take GL references from a `--gl` launch there.
- **A pwad map without its wads.** The engine sits in the IWAD's MAP01 and measures perfectly.
- **A paused sim** does not rebuild dynamic geometry or lights.
- **Animated lights between captures.** Two shots of a flickering map differ by ~0.4 with nothing
  changed; `ab` captures both values inside ONE pause and reports the repeat's diff as the noise floor.
- Before blaming a change for an oddity, reproduce it on the OLD binary under identical conditions.

## Regression scenarios

`scenarios/*.mjs` drive live engines and assert. To isolate a feature on a generated map and make it a
GL-vs-Vulkan regression test, use the `template-maps` skill (`src/udmf.mjs` + `src/template.mjs`;
`scenarios/horizon.mjs` is the worked example).

## Where things are

| what | path |
|---|---|
| CLI | `tools/fuactl/src/cli.mjs` (`--help` lists every command) |
| saved vantages | `tools/fuactl/spots.json` |
| addon wad sets for `--preset` | `catalogue/<id>/addon.json` |
| full RPC reference | `src/zandronum/src/features/mcp-bridge/README.md` |
| engine side | `src/zandronum/src/mcp_*.{h,cpp}` |
