# mapinfo-propermonsterfallingdamage

MAPINFO `propermonsterfallingdamage` — makes monster falling damage use the real computed damage
instead of an instakill. **Provenance:** uzdoom@e74b9f195 · **Class:** PORTABLE.
Hook: `LEVEL3_PROPERMONSTERFALLDMG` (g_level.h) via `MITYPE_SETFLAG3` (g_mapinfo.cpp); in
`p_mobj.cpp` P_MonsterFallingDamage the historical `damage = TELEFRAG_DAMAGE` override is now gated
on the flag being *unset*.
