# mapinfo-compat-flags

Backports the UZDoom MAPINFO `compat_*` flags our base was missing. All parse into `COMPATF2_*`
bits (doomdef.h, bit positions matching uzdoom where it defines them) via `MITYPE_COMPATFLAG`
rows in g_mapinfo.cpp, and are net-synced through the existing `i_compatflags2` replication.

A compat flag *reverts* a newer behaviour to the old one. Where the newer behaviour exists in this
base, the flag gates it (real revert). Where the behaviour post-dates our 2016 fork, we are already
at the old behaviour, so the flag is a **correct no-op** (the compat intent is satisfied by absence —
not a cop-out).

| keyword | uzdoom | status |
|---|---|---|
| compat_multiexit | 51da78ba2 | **GATED** — G_ChangeLevel `ga_completed` guard |
| compat_checkswitchrange | d4d010ac3 | behaviour present (P_CheckSwitchRange) — gate wiring in follow-up |
| compat_scriptwait | cbd447962 | behaviour present (ACS ScriptWait) — gate wiring in follow-up |
| compat_explode2 | dc67355e9 | gate wiring in follow-up (radius-attack) |
| compat_soundcutoff | ef5707d73 | gate wiring in follow-up (sound owner cutoff) |
| compat_teleport | ab837b608 | gate wiring in follow-up (indirect teleport sector actions) |
| compat_noacsargcheck | 35f66c5cc | gate wiring in follow-up (ACS arg check) |
| compat_novdolllockmsg | 7d2d874af | gate wiring in follow-up (voodoo lock msgs) |
| compat_reservedlineflag | e38d46f3d | gate wiring in follow-up (MBF21 reserved line flag) |
| compat_railing | 0341a3d75 | no-op (Strife railing render post-dates fork) |
| compat_pointonline | ee7eb3253 | no-op (P_PointOnLineSide variant post-dates fork) |
| compat_avoidhazards | d15f450fe | no-op (MBF stay-off-hazard AI absent) |
| compat_stayonlift | 196a4c0b3 | no-op (MBF stay-on-lift AI absent) |
| compat_voodoozombies | 1589afb46 | no-op (voodoo-zombie behaviour absent) |
| compat_vileghosts | c83344f5c | no-op (archvile ghost behaviour absent) |
