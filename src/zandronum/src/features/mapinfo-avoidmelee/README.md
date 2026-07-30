# mapinfo-avoidmelee

MAPINFO `avoidmelee` — forces the MBF21 avoid-melee AI for all monsters on the level.
**Provenance:** uzdoom@ff497996a · **Class:** PORTABLE.
Hooks: `LEVEL3_AVOIDMELEE` (g_level.h) via `MITYPE_SETFLAG3` (g_mapinfo.cpp); read in `p_enemy.cpp`
A_Chase alongside the per-actor `MF3_AVOIDMELEE`.
