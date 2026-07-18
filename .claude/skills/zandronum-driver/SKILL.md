---
name: zandronum-driver
description: How to drive the ZandroX engine via the Zandronum MCP without wasting time — launch into a map, never idle on the launch call, avoid stale-instance/port traps, and recover with reset. Use whenever launching or controlling the engine through the MCP (launch_instance, verify_menu, screenshot, run_command, menu_nav).
---

# Driving the ZandroX engine via the MCP

The MCP (`zandronum-mcp`, v0.4.0+) spawns a bridge-patched engine and drives it over
loopback TCP. Follow this to avoid the failure modes that otherwise eat a lot of time.

## Launch

- **Always launch into a map.** Pass `map: "MAP01"` (and `skill: 3`). Without a map the
  engine sits at the title screen; with a non-ZDoom IWAD (e.g. `freedoom2.wad`) it also
  spam-loops `Cannot play non-ZDoom demos`. A map gives you a steady, drivable instance.
- **Always windowed.** `fullscreen: false` (+ a modest `width`/`height`, e.g. 960×600).
  Never fullscreen — it takes over the user's screen.
- **The launch call blocks (~up to 120s), but the engine is up in a few seconds.**
  Do NOT sit waiting on it. As soon as it returns *or* backgrounds, immediately drive the
  instance — `get_startup_errors` (reads the log, works even if the bridge is down) then a
  real command / `verify_menu` / `screenshot`.
- IWAD lives next to the engine: `build/ZandroX.app/Contents/MacOS/freedoom2.wad`.

## Opening an arbitrary menu

`openmenu "MenuName"` opens any menu descriptor by name (via `verify_menu`'s `open`, or
`run_command`). Load a custom `MENUDEF` lump with `files: ["…/foo.pk3"]` — no rebuild
needed for menudef-only changes.

## When commands time out

A command timing out almost always means one of:
1. **You attached to a stale/stopped instance** on the conventional port. v0.4.0 prevents
   this (free-port allocation + PID handshake), but if you see it: run the **`reset`** tool,
   then relaunch.
2. The engine is genuinely busy — screenshots/`verify_menu` already get a longer timeout.

Diagnose with `get_startup_errors` and, if needed, `lsof -nP -iTCP -sTCP:LISTEN | grep 777`
— don't blind-retry.

## Stale instances (should be rare now)

The engine runs a **parent-death watchdog**: it exits within ~3s once the MCP that
launched it dies (`ZANDRONUM_BRIDGE_PARENT_PID`). So orphans from ended sessions should
not accumulate. If they do (e.g. an engine wedged in the macOS window server during
teardown — `ps` state `UE`/`TX`, survives SIGKILL):

- Run the MCP **`reset`** tool (reaps orphans + frees ports), or
- Run `scripts/zx-mcp-reset.sh` from a real shell.
- Truly wedged (`UE`/`TX`) processes survive SIGKILL and need Activity Monitor / a logout.

## Don't fight the sandbox

`kill`/`killall` from the agent Bash tool is sandboxed and often can't reap GUI engines.
Prefer the `reset` tool (runs inside the MCP process) or ask the user to run
`scripts/zx-mcp-reset.sh` (or `! killall -9 zandronum`) themselves.
