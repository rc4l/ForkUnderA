# mapinfo-clustertext

MAPINFO `noclustertext` / `needclustertext` — UMAPINFO control over the cluster exit/enter finale text.
**Provenance:** uzdoom@20b6395cf · **Class:** PORTABLE.
`noclustertext` suppresses the cluster text on this map's exit; `needclustertext` forces it even
within the same cluster. Upstream uses LEVEL2_ bits; our LEVEL2 word is full, so these use
`LEVEL3_NOCLUSTERTEXT`/`LEVEL3_FORCECLUSTERTEXT` (behaviour identical). Hooks: `MITYPE_SETFLAG3`
rows (g_mapinfo.cpp); gate in `g_level.cpp` G_WorldDone cluster-finale decision.
