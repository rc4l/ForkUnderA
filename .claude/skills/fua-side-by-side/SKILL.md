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

Then, once it answers, **turn standalone off** or GL's window is a still frame:

```bash
node src/cli.mjs ui exec "fua_dg_standalone 0" --port 7801 --token <T>
```

- **A pwad map needs its wads.** Without them the map silently does not exist and the engine sits in
  the IWAD's MAP01 — which looks like a real level, renders fine, and quietly invalidates every
  measurement taken on it. For a catalogue addon use `--preset <id>` (e.g. `--preset whodunit` for
  wdi/wdiex maps), which loads its files in catalogue order from the download store; add
  `--store DIR` (or set `FUACTL_PWADS`) when they live elsewhere. A missing one is named; fetch it
  with `fua_download <file>` in the console.
- `--side-by-side` only sets `fua_dg_embed 0`; that is what un-nests the Vulkan window.
- `play` runs the **staged** `dist-windows/forkundera.exe`, so restage a fix before relaunching the user's
  session. Your own instance on a fresh build takes `FUACTL_ENGINE=F:/ForkUnderA/build-win/Release/forkundera.exe`,
  and `shot` against it needs the same variable or its pair lands in the other binary's `sweep/`.
- **No `--lock`** when the launch is for the user — the lock exists for your own measuring instances
  so a stray cursor cannot diverge a run. Give theirs its own port (7801 by convention) and keep yours
  elsewhere (7799, 7796).
- `play` **ignores `--token`** and mints its own. Read `PORT`/`TOKEN` from `tools/fuactl/.play-session`
  (a two-line `KEY=VALUE` file, not JSON) — it is written immediately, while the launch log is
  buffered and often empty for the first several seconds. Right after a background launch the file
  can still hold the previous launch's token, so wait for `TOKEN` to change before `ready`.
- Do not sleep in long blocks waiting for it: poll `rpc sim.tic` until it answers. A bare launch is
  ready in seconds; a heavy wad set can take 15-25 s.

## Confirm it is the map you asked for

```bash
node src/cli.mjs rpc player.camera --port 7801 --token <T>
```

Compare against the map's known spawn. doom2/freedoom MAP01 spawns at **(-96, 784)** — seeing that
after asking for a pwad map means the wads did not load; fix that before believing anything on screen.
(Whodunit's wdiex02 spawns near (-7264, 800).)

## Cleaning up

`quit` needs the right token, and a failed quit leaves the old engine running on the same port while
the new one starts beside it — the user then looks at the stale window and reasonably concludes the
tool is broken. So check what is actually running, and stop a stray by **PID**, never by name, which
also kills the user's own instance and any other harness:

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
  at. Either way `fua_dg_standalone 0` is required, or GL's half of the pair is a dead buffer.
- To aim both at the same place, save the vantage first with the `fua-capture-spot` skill and shoot
  `--spot <name>`.
