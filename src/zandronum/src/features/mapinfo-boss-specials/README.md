# mapinfo-boss-specials

MAPINFO `e1m8special` / `e2m8special` / `e3m8special` / `e4m6special` / `e4m8special` — the vanilla
per-episode boss-death qualifiers. **Provenance:** uzdoom@e2e8ec8b3 · **Class:** PORTABLE.

Each qualifies A_BossDeath by the matching MF8 boss flag (`MF8_E#M8BOSS`, already present from the
MBF21 port); the action itself comes from the shared `LEVEL_SPECACTIONSMASK` switch (exit by
default). Hooks: `LEVEL3_E#M8SPECIAL` (g_level.h) via `MITYPE_SETFLAG3` table rows (g_mapinfo.cpp);
qualifier gate in `p_enemy.cpp` A_BossDeath.
