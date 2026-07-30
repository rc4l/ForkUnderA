# mapinfo-forceworldpanning

MAPINFO `forceworldpanning` — force world-unit texture panning for all textures on the level.
**Provenance:** uzdoom@74ea9143e · **Class:** PARSE-ONLY (flagged reclassification).

Our GL renderer caches `bWorldPanning` in the per-texture FMaterial (built once, shared across
levels); there is no per-wall/per-frame worldpanning read to gate on a level flag. Honouring this
per-level would require moving the worldpanning decision out of the cached material into the render
path — a renderer refactor beyond a MAPINFO port. So it is recognised and logged once
("parsed but not yet wired") via the `ZXUnhandledMapKeys[]` manifest rather than silently accepted
or partially wired. `LEVEL3_FORCEWORLDPANNING` is reserved in g_level.h for a future render-path pass.
