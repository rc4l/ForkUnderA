---
name: fua-side-by-side
description: Launch ForkUnderA with GL and the Diligent (Vulkan) backend in two windows of one process, for the user to play and A/B by eye. Use when the user asks to see a map side by side, compare GL against Vulkan live, or "launch the build for me".
---

# Side-by-side GL / Vulkan for the user

One process, two top-level windows: GL's own window plus the Diligent (Vulkan) window. The user drives
it with mouse and keyboard and A/Bs the two. The Diligent backend is Windows-only today
(`FUA_DILIGENT`); elsewhere there is only the GL window.

Everything goes through `fuactl`, run as `node src/cli.mjs <command>` from `tools/fuactl` (see the
`zandronum-session` skill).

## The launch

```bash
cd tools/fuactl
nohup node src/cli.mjs play --map <MAP> --side-by-side --port 7801 [--preset <id> | --file a.pk3,b.wad] > sbs.log 2>&1 &
```

`--spot <name>` instead of `--map`/`--file` takes a saved spot's map and wads (by name, from `FUACTL_PWADS`
or `--store`) and stands the player there.

**Closing it for an exclusive run** (`scenarios`, a template comparison): `quit --park --port 7801` files
their camera, wads and renderer first, and afterwards `play --unpark --port 7801 --store <wad folder>`
(backgrounded as above) brings it back at that camera, side by side again, or without GL if it was a
`--no-gl` launch.

- **A pwad map needs its wads.** Without them the map silently does not exist and the engine sits in
  the IWAD's MAP01 — which looks like a real level, renders fine, and quietly invalidates every
  measurement taken on it. For a catalogue addon use `--preset <id>` (e.g. `--preset whodunit` for
  wdi/wdiex maps), which loads its files in catalogue order from the download store; add
  `--store DIR` (or set `FUACTL_PWADS`) when they live elsewhere. A missing one is named; fetch it
  with `fua_download <file>` in the console.
- `--side-by-side` sets `fua_dg_embed 0`, which un-nests the Vulkan window, and GL draws whenever that
  window is separate (`ComputeFrameRoute`), so both windows are live. The Vulkan window is the same derived
  frame players get; `--no-gl` has no GL window, so never combine it with `--side-by-side`.
- `play` runs the **staged** `dist-windows/forkundera.exe`, so restage a fix before relaunching the user's
  session. Your own instance on a fresh build takes `FUACTL_ENGINE=F:/ForkUnderA/build-win/Release/forkundera.exe`,
  and `shot`/`ab`/`burst`/`motion --port P` find its `sweep/` from the binary its play session recorded.
- **No `--lock`** when the launch is for the user — the lock exists for your own measuring instances
  so a stray cursor cannot diverge a run. Give theirs its own port (7801 by convention) and keep yours
  elsewhere (7799, 7796).
- `play` takes `--token` or mints one, and once the instance is up writes `PORT`, `TOKEN` and `ENGINE` to
  `tools/fuactl/.play-session` and `.play-session-<port>` (`KEY=VALUE` lines, not JSON); the launch log
  is buffered and often empty for the first several seconds. A launch first clears the files naming its
  port, so `ready --port P` waits for the new token, and `--port 7801` with no `--token` finds the user's
  instance even after your own launches have overwritten `.play-session`.
- Wait for it with `ready --port 7801`, never a hand-rolled `rpc` poll loop: the bridge serves only its
  newest client, and a poll that connects while `play` is still checking its world knocks it off and the
  launch dies with `hello timeout`. Every command now waits out a launch on its port by itself (`play`
  holds `.play-session-<port>.launching` until it is up), so `--port` must name the launch's port. A
  bare launch is ready in seconds; a heavy wad set can take 15-25 s.

## Confirm it is the map you asked for

```bash
node src/cli.mjs rpc player.camera --port 7801 --token <T>
```

Compare against the map's known spawn. doom2/freedoom MAP01 spawns at **(-96, 784)** — seeing that
after asking for a pwad map means the wads did not load; fix that before believing anything on screen.
(Whodunit's wdiex02 spawns near (-7264, 800).)

## Cleaning up

`node src/cli.mjs quit --port 7801 --token <T>` quits by token and waits for the port to close,
printing `stopped`, `absent` or `running` (exit 1); `play --restart` does the same for the last
session. A quit that did not take leaves the old engine on the port while the new one starts beside
it — the user then looks at the stale window and reasonably concludes the tool is broken. So on
`running`, check what is actually up, and stop a stray by **PID**, never by name, which also kills
the user's own instance and any other harness:

```bash
# Windows (PowerShell)
Get-CimInstance Win32_Process -Filter "Name='forkundera.exe'" | Select-Object ProcessId, CommandLine
# macOS / Linux
pgrep -fl forkundera
```

The tail of the command line names the map, which is the quickest way to tell two instances apart.

## Useful once it is up

- `fua_vulkan 0` / `1` in the console — live A/B inside one window.
- `fua_dg_skyportal 0` / `1` — sky portal against the plain dome.
- `fuactl shot` does **not** work against a side-by-side instance: the un-embedded Vulkan window hands
  back a 1x1 PNG and the tool refuses it. For pixel work launch a SECOND instance without
  `--side-by-side`, with `--lock`, on your own port, and shoot there — theirs stays untouched to look
  at. Set `fua_dg_standalone 0` on that embedded instance, or GL's half of the pair is a dead buffer.
- To aim both at the same place, save the vantage first with the `fua-capture-spot` skill and shoot
  `--spot <name>`.
