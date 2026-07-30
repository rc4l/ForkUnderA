# mapinfo-nogravity

MAPINFO `nogravity` — the level runs at zero gravity. **Provenance:** uzdoom@3781c43ae · **Class:** PORTABLE.
Hooks: `LEVEL3_NOGRAVITY` (g_level.h) via `MITYPE_SETFLAG3` (g_mapinfo.cpp); in `g_level.cpp`
G_InitLevelLocals, `level.gravity` is forced to 0 after the sv_gravity/info->gravity default.
