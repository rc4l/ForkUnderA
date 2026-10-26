---
name: zandronum-driver
description: How to drive a running ZandroX engine from an agent with fuactl (tools/fuactl) over the native MCP bridge — build a bridge-enabled engine on any OS, launch a supervised windowed instance, read and navigate menus, take screenshots, run console commands, and clean up instances. Use whenever launching or controlling the engine programmatically (the fuactl CLI, or its MCP tools such as launch_instance, ui_read, ui_menu_nav, ui_screenshot, rpc, reap).
---

# Driving the ZandroX engine with fuactl

`tools/fuactl` replaced the old external `zandronum-mcp` in `0f20ec79` (2026-08-12). The engine now
answers a typed RPC protocol on a loopback bridge, and fuactl is both its CLI (`npx fuactl <cmd>`)
and its MCP server (`npx fuactl mcp`). If you know the old tool names (`verify_menu`,
`get_startup_errors`, `reset`, …), `tools/fuactl/docs/parity.md` maps each one to its replacement.
The overview is `README.fuactl.md`; the full RPC list is `src/zandronum/src/features/mcp-bridge/README.md`.

## Before the first launch

- **Node.js**, then `cd tools/fuactl && npm install`.
- **A bridge-enabled engine.** Release builds contain no bridge at all, so launching one fails with
  "bridge port never opened". The per-OS build command and where the staged binary lands are the
  `zandronum-rebuild` skill's table (one place, so this one cannot drift from it). The flag is a
  compile definition on the whole engine, so switching it on or off rebuilds everything. Never hand
  a bridge build to players.
- `npx fuactl doctor` checks that the engine fuactl will use is bridge-enabled, newer than the source,
  has `iwads/` beside it, and that the Diligent submodule is checked out.
- Set `FUACTL_ENGINE` to run a binary other than the staged one.

## Rebuild before you launch

After any code or `wadsrc/` change, rebuild as the `zandronum-rebuild` skill says. A hand-rolled
`cmake --build --target zdoom` never rebuilds the pk3, so you end up driving a stale engine and
debugging the wrong layer.

## Launch

`npx fuactl launch --map MAP01 --skill 3` (MCP: `launch_instance`) prints the `pid`, `port` and
`token` every later call needs. Each instance:

- **starts in a map** (`MAP01` by default). At the title screen a non-ZDoom IWAD such as
  `freedoom2.wad` spam-loops `Cannot play non-ZDoom demos`.
- **runs windowed** at 640×400.
- **gets its own fresh ini** (`-config` in a temp folder), so it starts from engine defaults and never
  reads or writes the user's settings.
- **ignores the real keyboard and mouse** (`ZANDRONUM_BRIDGE_INPUT_LOCK`), so the user can keep
  working and only injected input reaches the game.
- **never takes the user's focus or mouse**. A hands-off window opens under every other window
  without activating, is never activated by a click, and never grabs, clips or hides the pointer.
  `--no-focus` (MCP: `focus: false`) or `FUACTL_NO_FOCUS=1` gives a hands-on `play`/`launch --play`
  the same opening, though it can still be clicked into and played; `--focus` overrides the
  variable. The project's `.mcp.json` sets `FUACTL_NO_FOCUS=1`. Engine side: `MCP_NoFocus` and
  `MCP_InputLocked` in `win32/i_main.cpp`, `i_input.cpp`, `i_mouse.cpp` and `dgwin32.cpp`.

`--iwad`, `--file a.pk3,b.pk3`, `--seed` and `--arg` (raw engine arguments) pass through.

## Driving the UI

Every call takes `--port P --token T`:

| CLI | MCP | Does |
|---|---|---|
| `ui read` | `ui_read` | the current menu as text |
| `ui find <label>` | `ui_find` | where a label is on screen |
| `ui nav down down enter` | `ui_menu_nav` | menu keys: up, down, left, right, enter, back, backspace |
| `ui click <x> <y>` | `ui_click` | a mouse click (the MCP tool can also click a `label`) |
| `ui type <text>` | `ui_type` | text input |
| `ui screenshot [name]` | `ui_screenshot` | a PNG written to `sweep/` beside the engine (a name with a folder in it is used as given) |
| `ui exec <ccmd>` | `rpc` with `console.exec` | any console command |

`ui exec openmenu <MenuName>` opens any menu by name. Load a custom `MENUDEF` with
`--file …/foo.pk3`; menudef-only changes need no rebuild.

## When an instance stops answering

`npx fuactl hang --port P` tells apart the four cases that look the same from outside: the process
died, it is stuck in one function (which it names), its tic is frozen, or it is simply paused.
On Windows the in-engine sampler can't answer for a wedged game thread, so it builds and runs
`native/stackdump` instead and names the busiest thread's first engine frame (`report.stack` has the
stacks). Don't blind-retry.

## Cleaning up

- `npx fuactl ls` lists the instances fuactl launched; `npx fuactl reap` removes orphans (`--all`,
  `--kill`). MCP: `list_instances`, `reap`.
- An instance exits by itself within a few seconds once the fuactl that launched it dies
  (`ZANDRONUM_BRIDGE_PARENT_PID`), so orphans should be rare.
- `kill`/`killall` from the agent's Bash tool is sandboxed and often can't reap a GUI engine; prefer
  `fuactl reap`.
- macOS only: an engine wedged in the window server (`ps` state `UE`/`TX`) survives SIGKILL.
  `scripts/zx-mcp-reset.sh` force-kills every engine and lists the wedged ones, which need Activity
  Monitor or a logout.
