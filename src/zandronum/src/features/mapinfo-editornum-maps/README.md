# mapinfo-editornum-maps

- **Keywords:** `doomednums`, `spawnnums`, `conversationids` (top-level blocks)
- **Class:** NOT-PORTABLE here (skip + warn), flagged divergence
- **Upstream:** doomednums uzdoom@15dbbc913, spawnnums uzdoom@2ec8e2c2a,
  conversationids uzdoom@b6a4511dd

## Why skip + warn (not a cop-out)

These blocks remap editor numbers / spawn numbers / Strife conversation IDs to
actor classes from MAPINFO. Doing so correctly requires GZDoom's *deferred*
class-resolution machinery (`SpawnMap` / `InitClassMap` /
`SpawnablesFromMapinfo`), which resolves class names *after* all actors are
defined. This base resolves `DoomEdMap` / `SpawnableThings` / `FStrifeTypeMap`
eagerly at load time and has no deferred-resolution pass, so honoring these
blocks needs that subsystem ported first.

Crucially, an unhandled top-level block was previously a **fatal**
`Unknown top level keyword` error — so a wad using any of these would fail to
load. This change makes the parser skip the block body cleanly and emit a
classified console warning, so such wads load (minus the remap) instead of
aborting.

## Code hooks (in-place)

- `src/zandronum/src/g_mapinfo.cpp` — `ParseMapInfo()` recognizes the three
  keywords, prints the `uzdoom@<sha>` warning, and skips the brace block.

## To promote to a real port later

Port the deferred class-map resolution pass, then fill `DoomEdMap` /
`SpawnableThings` / `FStrifeTypeMap` from the parsed entries after actor
definitions are finalized.
