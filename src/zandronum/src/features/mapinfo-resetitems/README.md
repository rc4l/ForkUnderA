# mapinfo-resetitems

- **Keyword:** `resetitems`
- **Class:** PARSE-ONLY (flagged divergence from the "portable ⇒ real behavior" rule)
- **Upstream:** uzdoom@d80dc098b

## Why this is parse-only (not a cop-out)

`resetitems` makes previously-collected pickups re-appear when a hub map is
re-entered. In this engine, hub re-entry restores the whole map from a saved
snapshot (`p_setup.cpp`, gated on `savegamerestore`); map things are **not**
re-spawned on re-entry. Implementing `resetitems` correctly means selectively
spawning fresh item actors *into* a snapshot-restored level while avoiding
duplicates — i.e. modifying the savegame/hub snapshot-restore invariants.

That is a deep, regression-prone change to a savegame-adjacent code path
(cf. the fixed64 skill's savegame-versioning caution), and none of the target
content (Eviternity / Eviternity II are non-hub) exercises it. So it is
deliberately classified parse-only and logged with a console warning tagged
`uzdoom@d80dc098b`, rather than risking hub save/load regressions.

## Code hooks (in-place)

- `src/zandronum/src/g_mapinfo.cpp` — `resetitems` entry in
  `ZXUnhandledMapKeys[]` (`ZXUH_PARSEONLY`); the map parser's unknown-property
  fallback emits the classified warning and skips the token.

## To promote to a real port later

Add a `LEVEL3_REMOVEITEMS` flag, and on hub re-entry (`savegamerestore` path)
re-run the item subset of `P_SpawnMapThing` for inventory pickups after the
snapshot loads, de-duplicating against restored actors.
