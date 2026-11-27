# config-migrate

Puts saved settings back to their defaults once, for configs written before this fork changed those defaults. Archived cvars save their value whatever the default is, so a new default alone never reaches a config that already has the old value saved.

- `[LastRun] Version-fua` is this fork's config version, kept apart from `Version` (GZDoom) and `Version-Zandronum` (Zandronum) so an upstream re-sync never collides with it. A config without it reads as 0.
- `computation/configmigrate_compute.cpp` holds the table, one row per reset: `{ version, cvar }`. A new reset takes the next version, and the written `Version-fua` is the table's highest.
- On load, every cvar with a row newer than the config's `Version-fua` goes back to its default, global and per-game cvars alike.

| Version | Cvar | Why |
|---|---|---|
| 1 | `cl_fua_replay` | Instant replay turned default off. |
| 2 | `net_zstd_level` | Packet compression dropped from 12 to 1: with four clients level 12 took ~9% of a server's tic for 7% fewer bytes. |

## In-engine hooks

- `gameconfigfile.cpp` `DoGlobalSetup`: after the global cvars are read, resets the cvars `ComputeConfigResets` lists.
- `gameconfigfile.cpp` `DoGameSetup`: the same resets again once the game's own sections are read, where most archived cvars live, then none for the rest of the run.
- `gameconfigfile.cpp` `ArchiveGlobalData`: writes `Version-fua`.
- `CMakeLists.txt`: `configmigrate_compute.cpp` in the zdoom source list.
