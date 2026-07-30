# mapinfo-unsupported-mapopts

UZDoom MAPINFO **map** properties that need a subsystem this base lacks. **Class: NOT-PORTABLE** —
accepted and skipped cleanly, logged once as "not supported in this port (uzdoom@<sha>)".

Authoritative per-keyword provenance: `ZXUnhandledMapKeys[]` in `src/zandronum/src/g_mapinfo.cpp`.

| keyword | uzdoom | needs |
|---|---|---|
| intro / outro | cda6394a9 | cutscene engine |
| enteranim / exitanim | e88d91289 | ID24 interlevel-animation system |
| eventhandlers | 9b3b21c73 | per-map ZScript static event handlers (VM) |
| edata | 6ae417725 | Eternity EDF extradata loader |
