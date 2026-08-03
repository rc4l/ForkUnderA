---
name: wad-reloading
description: Swap the running ZandroX engine's IWAD/PWAD set at runtime with the in-engine `wad_reload` command — no process relaunch. Covers syntax, how it drives ZDoom's restart cycle, the validate-before-teardown safety model, and driving it over the MCP bridge.
---

# In-engine WAD reloading (`wad_reload`)

## What it is
`wad_reload` swaps the engine's loaded WAD set at runtime by driving ZDoom's
existing restart cycle from code — **no process relaunch**. Lives in
`src/zandronum/src/features/wadreload/` (Phase 1 of issue #82).

## Syntax
`wad_reload <iwad|-> [pwad ...] [map=MAPXX]`
- First arg = the IWAD path, or `-` to keep the current IWAD.
- Remaining args replace the entire `-file` (PWAD) set.
- Optional `map=MAPXX` spawns straight into a map on the new set.
- **Load order matters** — later files override earlier lumps, exactly like `-file`.

Examples:
- `wad_reload doom2.wad` — IWAD only, drop all PWADs.
- `wad_reload - "Eviternity II.wad" map=MAP12` — keep IWAD, one PWAD, into MAP12.
- `wad_reload tnt.wad dnd_v2.6f.pk3 dnd_monsters_v2.6f.pk3 map=MAP24` — IWAD + PWAD stack.

## How it works
`D_DoomMain` re-reads the WAD set from the global `Args` on every restart-loop
iteration. So a reload is: rewrite `Args`, throw `CRestartException`, come back
up on the new set. `zx::wadreload::RequestReload` + `CCMD(wad_reload)` wire that
from a programmatic entry point.

## Safety model (stronger than the console `restart`)
- **Match-skip:** if the requested set already equals what's loaded (same IWAD +
  same PWAD list *in the same order*), it's a no-op (`WantedMatchesLoaded`).
- **Validate before teardown:** the new set is checked for real **WAD/PK3/PK7
  magic before** the running game is torn down — deliberately **stricter than
  `FWadCollection::AddFile`**, whose catch-all would wrap a truncated/garbage
  download as one lump. A corrupt file is refused outright and the running game
  keeps playing.
- There is **no post-teardown rollback** on purpose: `I_FatalError` here calls
  `exit(-1)` (it does not throw), so nothing is catchable once the new set fails
  to load. The design never tears down unless the new set is provably loadable —
  a strictly stronger guarantee (no visible failed-boot, no second restart).

## Pure, tested helpers (`features/wadreload/computation/`)
- `WantedMatchesLoaded` — same-IWAD + same-ordered-PWAD-list check.
- `ComputeReloadArgv` — rewrites argv, dropping `-iwad`/`-file` + their values and
  appending the new set. **Correct at the array tail**, where `DArgs::RemoveArgs`
  leaves the last value behind (that bug is why this helper is owned + 100% covered).

## Driving it over MCP
- `run_command "wad_reload …"` **times out (~5 s)** — the restart drops the bridge.
  That's expected: the reload still happens; the **next** MCP call reconnects to the
  same instance/port.
- After a reload, do a `screenshot` (or `map_info`) to confirm the new set/map loaded.
  Heavy TCs (MM8BDM ~170 MB, big gameplay mods) take several seconds; a too-early
  screenshot returns black/empty — retry.
- **One reload per `run_command`.** `CRestartException` unwinds immediately, so
  anything after `wad_reload` in the same command buffer is lost — you can't chain
  reloads (or append `; fua_clip`) in one string.

## Pitfalls
- IWAD is always the FIRST arg. A PWAD-magic file that a TC uses as its base
  (e.g. MM8BDM `megagame.wad`, which is `PWAD`) still goes in the IWAD slot.
- Windows-extracted archives can produce filenames with **literal backslashes**
  (`…-archive\file.pk3`) that the path parser splits on — copy to a clean path first.
- The FUA instant-replay recorder **survives** a `wad_reload` (see the
  `in-engine-recording` skill), so a recording continues across the swap — good for
  demoing both features together.

Code: `src/zandronum/src/features/wadreload/` (`zx_wadreload.{h,cpp}`,
`computation/wadreload_compute.*`).
