# mapinfo-loadacs

Ports the `loadacs` MAPINFO map keyword from UZDoom.

- **Keyword:** `loadacs = "<lump>"[, "<lump>"...]`
- **Class:** PORTABLE (real behavior)
- **Upstream:** uzdoom@6ae417725

## Behavior

Loads one or more extra ACS library lumps (from the `ns_acslibrary`
namespace) for this level, in addition to any modules pulled in by the
global `LOADACS` lump. The base engine already had the global `LOADACS`
mechanism (`FBehavior::StaticLoadDefaultModules`); this adds the per-map
MAPINFO form.

## Code hooks (in-place)

- `src/zandronum/src/g_level.h` — `level_info_t::ACSLibraries`
  (`TArray<FName>`), next to `PrecacheSounds`.
- `src/zandronum/src/g_mapinfo.cpp`
  - `level_info_t::Reset()` — `ACSLibraries.Clear()`.
  - `DEFINE_MAP_OPTION(loadacs)` — parses the comma-separated lump list.
- `src/zandronum/src/p_setup.cpp` — in `P_SetupLevel`, right after
  `FBehavior::StaticLoadDefaultModules()`, each named library is resolved
  via `Wads.CheckNumForName(name, ns_acslibrary)` and loaded with
  `FBehavior::StaticLoadModule`.
