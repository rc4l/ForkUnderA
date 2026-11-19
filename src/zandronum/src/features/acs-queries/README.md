# features/acs-queries

ACS actor queries: actors found in the blockmap or by class, filtered, sorted and kept as handles,
`fua_` functions 24040-24048. Modder reference:
[`docs/modding/acs/Actor_queries.md`](../../../../../docs/modding/acs/Actor_queries.md).

- A query lives on its script (`DLevelScript::fua_queries`), so it is freed with the script and saved
  with it. A script holds up to 16; the 17th `fua_QueryCreate` returns 0 with one warning a map.
- Ids are a serial number per machine that never repeats within a game, so another script's id or a
  freed one finds nothing, and a hub map's scripts coming back never collide with the current map's.
  A new game (`P_ClearACSVars(true)`) starts them over; savegames keep the last one in a chunk (`fqId`)
  of their own. Tracers (`features/acs-traces`) take their ids from the same serial (`NextObjectId`)
  and save through the same `SerializeNamedRun`.
- A run looks up its filters' classes and handles once, then tests each actor in
  `ComputeQueryFilterOrder`'s order; `FQ_FLAG` looks its flag up once per class it meets. Results are
  sorted by `ComputeQuerySort` (stable, capped at 1024) before any actor is given a handle.
- On a snapshot-netcode client, no query finds a mirror (`NETFL_MIRROR`), the render shim drawn for an
  actor whose hidden copy is the one scripts read.
- Scripts save their queries from SAVEVER 4540, as one run of ints (`ComputeFlattenQueries`) with class
  and flag names saved as names; a damaged run loads as no queries.

## Hooks

- `p_acs.h`: `DLevelScript::fua_queries`.
- `p_acs.cpp`: the nine functions, `DLevelScript::Serialize`, and `P_ClearACSVars(true)` resetting the ids.
- `g_game.cpp`: the id chunk in savegames.
- `g_level.cpp`: `EnterLevel`, so the full-script warning shows once a map.
- `version.h`: SAVEVER 4540.
- `features/acs-argcount`: each function's minimum argument count.

## Tests

- `computation/queries_compute_test.cpp`: filter kinds and defaults, cost order and short-circuiting,
  range shapes, stable capped sorts, ids, the 16-query and 32-filter limits, clearing, and saved queries
  with damaged inputs.
- `fuactl scenarios acsqueries`: every filter, both sorts and a cap, class queries leaving carried
  inventory out and finding a `NOBLOCKMAP` puff, a result whose actor went, 16 queries and one warning,
  another script's and a freed id, and a script waiting mid-loop across a savegame, with the id counter
  surviving a new game and the load. It fails with `ReadIds` taken out.
- `fuactl scenarios netacsqueries`: the blockmap and class queries, the cap and a held query as CLIENTSIDE
  copies on the player's client, an observer and a late joiner, then on the server, each in its own world
  with its own ids; `FQ_PLAYER` finds every player in the game. It runs on the snapshot netcode at
  `fua_snap_nolegacy 2`, where taking out the `NETFL_MIRROR` skip finds every barrel twice on a client;
  with snapshots off, leaving the imp's `SVC_KILLTHING` unsent (`fua_debug_netdrop=55`) fails the
  clients' alive and dead filters.
