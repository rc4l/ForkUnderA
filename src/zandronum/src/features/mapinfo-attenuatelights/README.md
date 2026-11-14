# mapinfo-attenuatelights

MAPINFO `attenuatelights` — GLDEFS lights that give no `attenuate` of their own are attenuated on this level.
**Provenance:** uzdoom@63bba40d7 · **Class:** PORTABLE.
Hooks: `LEVEL3_ATTENUATE` (g_level.h) via `MITYPE_SETFLAG3` (g_mapinfo.cpp); `FLightDefaults::ApplyProperties`
(`features/gldefs/gldefs.cpp`) resolves each light's flag through `ComputeLightAttenuated`
(`features/gldefs/computation/attenuate_compute`). What attenuation does is in `features/gldefs/README.md`.
