# mapinfo-hazardcolor

Ports the `hazardcolor` and `hazardflash` MAPINFO map keywords from UZDoom.

- **Keywords:** `hazardcolor = "<color>"`, `hazardflash = "<color>"`
- **Class:** PORTABLE (real behavior)
- **Upstream:** uzdoom@b4079b991

## Behavior

Override the screen-blend colors used while a player stands in a Strife
hazard (radiation) sector:

- `hazardcolor` — the gradual blend color that intensifies with the hazard count.
- `hazardflash` — the strobing flash color used when the sector's
  `PF_HAZARD` palette-flash mode is active.

When a level does not set them (stored as `-1`), the engine keeps its exact
built-in Strife green, so existing content is unchanged.

## Code hooks (in-place)

- `src/zandronum/src/g_level.h` — `level_info_t::HazardColor` / `HazardFlash`
  (`int`, `-1` = engine default).
- `src/zandronum/src/g_mapinfo.cpp`
  - `level_info_t::Reset()` — reset both to `-1`.
  - `DEFINE_MAP_OPTION(hazardcolor/hazardflash)` — parse a color via `V_GetColor`.
- `src/zandronum/src/v_blend.cpp` — the hazard blend uses the level's colors
  when set, else the original hardcoded green.
