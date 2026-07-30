# mapinfo-per-level-finale

Ports the per-level finale MAPINFO keywords from UZDoom.

- **Keywords:** `exittext`, `textflat`, `textpic`, `textmusic`
- **Class:** PORTABLE (real behavior)
- **Upstream:** uzdoom@49b77f3a1

## Behavior

The base engine only had cluster-level finale text (`ExitText`/`EnterText`
on `cluster_info_t`). These keywords let an individual map define its own
finale that plays when the map is exited, taking precedence over the
cluster's exit/enter text:

- `exittext` — the finale text (supports the standard `lookup`/`$`/multi-line
  forms via `ParseLookupName`).
- `textflat` — the finale backdrop flat.
- `textpic` — the finale backdrop as a full-screen pic (sets `LEVEL3_FINALEPIC`).
- `textmusic` — the finale music (with optional order).

If the map sets none of these, the existing cluster finale logic is used
unchanged. Only fires in single-player, matching the cluster path.

## Code hooks (in-place)

- `src/zandronum/src/g_level.h`
  - `level_info_t::FinaleText / FinaleFlat / FinaleMusic / finalemusicorder`.
  - `LEVEL3_LOOKUPEXITTEXT`, `LEVEL3_FINALEPIC` flag bits.
- `src/zandronum/src/g_mapinfo.cpp`
  - `level_info_t::Reset()` — clear the four fields.
  - `DEFINE_MAP_OPTION(exittext/textflat/textpic/textmusic)` parse handlers.
- `src/zandronum/src/g_level.cpp` — `G_WorldDone()` starts the per-level
  finale (falling back to cluster music/flat when unset) before the cluster
  exit/enter-text logic and returns.
