# fuactl vs. the old ZandronumMCP — parity matrix

The old tool was an external Python MCP that scraped the console and screen. fuactl is the native
bridge: the engine speaks a typed RPC protocol, so most old "tools" collapse into a handful of
first-class RPCs plus `console.exec` for anything that already had a CCMD. This matrix confirms
nothing from the old surface was lost, and marks where the new system does strictly more.

Legend: **RPC** = native bridge command · **CLI** = `fuactl <cmd>` · **UI** = `ui.mjs` helper ·
**CCMD** = existing engine console command via `console.exec` · **✅ better** = new capability the old
tool couldn't do.

## Lifecycle / instances
| old tool | new coverage |
|---|---|
| `launch_instance` | CLI `launch` / RPC-launched `launchInstance()` — now self-registers a pidfile |
| `attach_instance` | `BridgeClient.connect(port, {token})` — token-gated, loopback-only |
| `kill_instance` | CLI `reap --all` / `stopInstance()` — SIGTERM → clean `exit(0)` teardown |
| `reset` | RPC `sim.restore` (snapshot/restore) or relaunch; `console.exec map <m>` |
| — | ✅ CLI `reap` orphan-scoped multi-session reaper; ✅ pidfile registry `ls` |

## Determinism / stepping (new — the old tool had none of this)
| capability | new coverage |
|---|---|
| pause / resume | RPC `sim.pause` / `sim.resume` |
| single-step N tics | RPC `sim.step` → `stepped` event |
| seed control | RPC `sim.seed` (get/set/clear) |
| deterministic fingerprint | ✅ RPC `sim.hash` (level.time + RNG + actor transforms) |
| cross-instance desync check | ✅ CLI `session` / `runDeterminismCheck` |
| save/restore sim state | RPC `sim.snapshot` / `sim.restore` |

## State inspection
| old tool | new coverage |
|---|---|
| `player_state` | RPC `state.player` (x/y/z/angle/health) |
| `actor_state`, `actors_near`, `inspect_target` | RPC `state.actors` (positions/health, capped) |
| `list_actor_classes` | CCMD `dumpclasses` via `console.exec` |
| `hud_info`, `read_hud` | HUD tee (`MCP_HUD_*`) + `screenshot` readback |
| `viewport_info`, `renderer_info` | CCMD `vid_*`/`r_*` cvars via `console.exec` |
| `map_info` | RPC `sim.tic` (map/leveltime/inlevel) + CCMD `printinv`/`mapname` |

## Map / ACS introspection
| old tool | new coverage |
|---|---|
| `acs_index`, `find_acs_symbol`, `behavior_names` | gated ACS-introspection CCMDs via `console.exec` |
| `get_acs_var`/`set_acs_var`, `list_acs_vars` | CCMD `dumpscripts` / ACS var CCMDs |
| `get_map_var`/`set_map_var`, `get_map_array`/`set_map_array` | ACS var CCMDs via `console.exec` |
| `get_sector`, `get_linedef`, `find_sectors_by_tag` | map-introspection CCMDs via `console.exec` |
| `list_scripts`, `list_running_scripts`, `run_script` | CCMD `puke`/`pukename` + script-list CCMDs |

## Cheats / spawning
| old tool | new coverage |
|---|---|
| `summon` | CCMD `summon <cls>` |
| `give`, `take` | CCMD `give`/`take` |
| `set_pause`, `step` | RPC `sim.pause` + `sim.step` (deterministic, not wall-clock) |

## Input / UI  (ui.mjs — ported 1:1, then extended)
| old tool | new coverage |
|---|---|
| `menu_nav`, `menu_key`, `menu_text` | UI `menuNav` (paired down/up), `charEvent`, `typeText` |
| `verify_menu` | UI `verifyMenu` (open → nav → screenshot) |
| `mouse_click`, `mouse_move` | UI `click` / `mouseMoveEvent` — ✅ + `rightClick`, `middleClick` |
| `mouse_wheel` | UI `wheel` (up/down/**left/right**) |
| (drag — not in old tool) | ✅ UI `drag` (stepped move with button held) |
| (controller — not in old tool) | ✅ UI `padButton`/`padDpad` (KEY_JOY raw events); ✅ `stick`/`stickHold` analog axis injection |
| `screenshot` | UI `screenshot` (CCMD `screenshot` + PNG readback → base64) |

## Profiling  (new — the old tool had none of this)
| capability | new coverage |
|---|---|
| deterministic perf ablation | ✅ CLI `perf-ab` (causal Δms + sim/render verdict) |
| frame percentiles / 1%-low | ✅ RPC `perf.capture` |
| function-level hotspots (CPU) | ✅ CLI `sample` (macOS `sample` / Linux `perf`) |
| per-pass **GPU** milliseconds | ✅ RPC `gl.timers` / CLI `gl-timers` (scene / translucent / hud2d + total, GL_TIMESTAMP queries) — which GL pass eats a laggy frame |
| renderer identity + timer-query support | ✅ RPC `renderer.info` / CLI `renderer-info` |
| per-command receive bandwidth | ✅ RPC `net.bandwidth` / CLI `net-bw` |

## Saves / demos
| old tool | new coverage |
|---|---|
| `save_game`, `load_game`, `list_saves` | CCMD `save`/`load` + save-dir listing via `console.exec` |
| `play_demo` | CCMD `playdemo <name>` |
| `run_command` | RPC `console.exec` (the general escape hatch) |
| `get_crash`, `get_startup_errors` | crash tee (`MCP_Crash_Init`) + startup log via `ZANDRONUM_BRIDGE_LOG` |
| `list_functions`, `list_modules`, `profile_scripts`, `profile_window` | CCMD `dumpclasses`/`stat`/ACS profiler CCMDs via `console.exec` |

## Honest limits
- **Analog controller axes**: injected via `input.axis` (bridge-held override stamped into
  `G_BuildTiccmd`), NOT via the event queue — the OS gamepad is hardware-polled, so a fake gamepad
  event can't reach the sim. The override is the faithful path (full deadzone/accel pipeline).
- **Pause/step are single-player only**: `sim.pause`/`sim.resume`/`sim.step` drive the local `paused`
  flag, which only freezes the single-player `P_Ticker`. A server/client advances its sim from
  network tics, so these RPCs now return an explicit error on a netgame instance rather than
  silently no-op'ing. The determinism harness uses single-player instances, so it is unaffected.
- **Netgame bandwidth harness** requires the exact host recipe (`net-bw` bakes it in): host with
  `-port` (not the ignored `+port` cvar) and `+sv_cheats 1` at launch (a runtime change never reaches
  a connected client); the client joins with `+connect` and no local map, then must `join` to leave
  spectator (a spectator receives only ~8 B/s of keepalive); and the summon perturbation is issued
  from the client (the dedicated host has no console pawn).
- **Some ACS/map introspection** relies on existing engine CCMDs rather than a typed RPC; output is
  console text, not structured JSON. Promote to a typed RPC only if a workflow needs the structure.
