# command-safety

What a mod can make the console do, and where console commands may write files. Prompted by UZDoom's
writeini fix (uzdoom@d82dfa8cb, uzdoom@2a44a21e6).

Three rules:

- **A mod cannot run an unsafe command.** `FUnsafeConsoleCommand::Run` refuses a script's `ConsoleCommand`
  as well as an unsafe context (KEYCONF aliases, menu commands), in one place.
  - A menu item's command is unsafe unless it asks for confirmation (`SafeCommand`) and comes from the
    engine's own pk3, which loads before the IWAD. That applies to option menus and freeform menus alike.
  - A mod's `SafeCommand` still asks first, then runs unsafe.
- **A command that writes where it is told is unsafe**, and the cvars that name where the engine writes
  are `CVAR_NOSETBYACS`.
- **A command that writes a named file gives it its own extension.** `writeini` also writes only a plain
  file name beside the config, so a path cannot take it elsewhere. The other commands keep a folder in the
  name, so a server's `+logfile logs/server` still works.
- **`save` and `load` look for a bare name in the save directory**, as UZDoom's do. The browser's working
  directory is never kept, so `save complex` was lost on reload. `load` falls back to the name as given,
  where saves made before this went.

## Files (this folder)

- `computation/commandsafety_compute.{h,cpp}`:
  - `ComputeOwnExtension`, `ComputePlainFileName`, `ComputeFileBeside` and `ComputeBareFileName`, the file
    names;
  - `ComputeMenuCommandUnsafe`, which menu commands are trusted.
- `computation/commandsafety_compute_test.cpp`: their tests.
- `tools/fuactl/scenarios/commandsafety.mjs`: a wad whose OPEN script, confirmed option item and freeform
  item each try `atexit`; it passes when all three are refused and a named `writeini` lands beside the config.

## In-place engine edits

**Callers**

- `c_dispatch.cpp`:
  - `FUnsafeConsoleCommand::Run` also refuses `ACS_IsCalledFromConsoleCommand`.
  - `C_DoMenuCommand` runs a menu item's command, never lifting an unsafe context it runs in.
- `c_cmds.cpp`: `save` and `load` put a bare name in the save directory (`G_BuildSaveName`).
- `tools/web/check.mjs`: the save checks save and load by a bare name and find it in the save directory.
- The script checks this replaces are gone from `unbindall`, `logfile`, `error_fatal`, `crashout`, `load`,
  `save`, `restart`, `writeini` and `dumpmap`.
- `menu/optionmenuitems.h`: `FOptionMenuItemCommand` takes its unsafe flag, and a confirmed `SafeCommand`
  runs through `Activate`.
- `menu/menudef.cpp`: `SafeCommand` items and freeform `Command` items get their flag from
  `ComputeMenuCommandUnsafe` and the lump's file.
- `features/freeform-menu/freeformmenuitems.h`, `freeformmenu.cpp`: freeform command items carry the flag
  and run through `C_DoMenuCommand`.

**Commands made unsafe**

- `screenshot` (`m_misc.cpp`). A bound key still takes one.
- `recordmap` (`g_level.cpp`).
- `fua_viewtrace` (`features/frame/viewtrace.cpp`).
- `fua_diligent_shot` (`features/hwrender/diligent/dgdiag.cpp`).
- `fua_simtrace` (`mcp_simtrace.cpp`).

**Their own extension**

| Command | File | Extension |
|---|---|---|
| `writeini` | `m_misc.cpp` | `.ini`, beside the config |
| `screenshot` | `m_misc.cpp` | `.png`/`.pcx` |
| `logfile` | `c_cmds.cpp`, `StartLogging` | `.log`; its copies are bounded |
| `save` | `c_cmds.cpp` | `.zds` |
| `writeopl` | `sound/i_music.cpp` | `.raw` or `.dro` |
| `writewave` | `sound/i_music.cpp` | `.wav` |
| `writemidi` | `sound/i_music.cpp` | `.mid` |
| `dumpmap` | `p_writemap.cpp` | `.wad` |
| demos | `g_game.cpp`, `G_RecordDemo` | `.lmp` |
| client demos | `cl_demo.cpp`, `CLIENTDEMO_BeginRecording` | `.cld` |
| `statfile` | `statistics.cpp` | `.txt` |

`fua_capture` and `fua_flightdump` (`features/telemetry/telemetry.cpp`) take a plain name, so their files
stay in `telemetry/`.

**Cvars made `CVAR_NOSETBYACS`**

- `screenshot_dir`
- `save_dir`
- `statfile`
- `fua_dg_shaderdump`
- `cl_fua_download_dir`
- `cl_fua_downloadsites`
