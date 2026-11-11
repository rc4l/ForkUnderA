# dev-mode

The gate on test and debug tools, so they stay out of real games. A setting declared with
`CVAR_FUA_DEV_MODE` or a command declared with `DEV_MODE_CCMD` works offline, on a server's own
console (and so over RCON), during demo playback, and on a server with `sv_cheats` on. A client of a
server without cheats gets the refusal "`<name>` is a dev-mode tool: sv_cheats must be true on this
server.", and its dev-mode settings read as their defaults until it leaves or the server allows cheats.

Prompted by a playtester's `gl_wallmesh` report: typing a renderer debug setting off and on stopped the
Vulkan view drawing every floor, so players showed through them. This only stops an unmodified client;
what the server sends is the protection against a modified one.

## Rules

- **One rule:** `ComputeDevModeAllowed(isClient, svCheats)` is `!isClient || svCheats`. A client's
  `sv_cheats` is the server's, which the server sends and the player cannot set.
- **One check per kind:** every way a setting changes (console, `set`, menus, exec'd files, the config,
  a mod's `ConsoleCommand`) ends in `FBaseCVar::SetGenericRep`, which asks `MaySet`; every dev-mode command
  runs through `FDevModeConsoleCommand::Run`, which asks `MayRun`.
- **Held, not lost:** as the gate closes, each dev-mode setting off its default is set to its default and
  its value kept; as it opens, the value comes back. The config saves the kept value, so connecting to a
  server never costs a player their own settings.
- **Declare it, nothing else:** a new test or debug tool is a dev-mode tool by its declaration alone.

```cpp
DEV_MODE_CCMD( fua_light ) { ... }
CVAR( Bool, fua_mesh_sky, true, CVAR_ARCHIVE | CVAR_FUA_DEV_MODE )
```

## Which tools are dev-mode

The repo's standard (`AGENTS.md`), as the first sweep sorted every cvar and command:

- **Dev-mode:** anything that exists to test, measure, compare, bisect or debug. That covers a
  developer command players are never asked to run, a `fua_debug_*` switch, a `*_check`, `*_log`,
  `*_trace` or `*_stress` switch, and an unarchived `fua_` A/B or diagnostic switch.
- **Left open:** a player's setting; a menu, hosting or download command; a diagnostic players are asked
  to run from a netgame (`fua_stats`, `fua_flightdump`, `fua_hitches`).
- **Not this gate:** a server's setting (`sv_`, `compat_`, `CVAR_SERVERINFO`), which the server already
  owns, and a player option that gives an edge, which the server must force or limit instead.
- **Keep the other checks:** the gate replaces a tool's own "offline or cheats" check, not checks such
  as `demoplayback` or `gamestate`.

## Files (this folder)

- `devmode.{h,cpp}`: `Refresh`, `MaySet`, `MayRun`, `HeldValue`.
- `computation/devmode_compute.{h,cpp}`: `ComputeDevModeAllowed`, `ComputeDevModeChange`,
  `ComputeDevModeHolds`, `ComputeDevModeArchivedValue`.
- `computation/devmode_compute_test.cpp`: their tests.
- `tools/fuactl/scenarios/devmode.mjs`: a client of a server without cheats is refused each setting and
  command in its `TOOLS` and gets its own values back once the server allows cheats; a server with cheats
  allows them all until it turns cheats off.

## In-place engine edits

- `c_cvars.h`: `CVAR_FUA_DEV_MODE`.
- `c_cvars.cpp`: `FBaseCVar::SetGenericRep` asks `MaySet`; `C_ArchiveCVars` saves `HeldValue`.
- `c_dispatch.h` / `c_dispatch.cpp`: `FDevModeConsoleCommand` and `DEV_MODE_CCMD`, beside
  `FUnsafeConsoleCommand` and `UNSAFE_CCMD`.
- `network.cpp`: `NETWORK_SetState` calls `Refresh` (connect, disconnect, hosting).
- `cl_main.cpp`: `Refresh` after each of the three places a client sets `sv_cheats` (the connect, the
  reset and the server's game settings).
- `CMakeLists.txt`: `devmode.cpp` in the engine's source list.
