# features/acs-handles

ACS actor handles: an actor named by an `int` rather than a TID, `fua_` functions 24020-24023, and
the per-actor values kept with each slot, 24032-24035. Modder references:
[`Actor_handles.md`](../../../../../docs/modding/acs/Actor_handles.md) and
[`Actor_data.md`](../../../../../docs/modding/acs/Actor_data.md).

| Bits | Field |
|---|---|
| 31 | always 0, so handles are positive and 0 means no actor |
| 30-20 | generation (11 bits) |
| 19-0 | slot (20 bits, 1,048,575 slots; slot 0 is never used) |

- A slot is given the first time a script asks about an actor and kept in `AActor::fua_handleSlot`.
- Destroying the actor bumps the slot's generation and queues it; freed slots are reused oldest first.
  A slot whose generation reaches 2047 retires instead, so no generation repeats and a stale handle can
  never name a newer actor. The table grants 2^31 handles in all.
- The table belongs to the machine, not the level: a server's scripts and a client's `CLIENTSIDE`
  scripts each use their own, and nothing is sent. Scripts name actors across machines by net ID.
- No handle names a client's snapshot mirror (`NETFL_MIRROR`): asked for one, `HandleFor` gives the handle of the
  actor it draws (`netsnapshot::ActorBehindMirror`), or 0 when the client holds none.
- Leaving a map destroys its actors, so their handles go stale and carried handles read 0; generations
  keep running across map changes. A new game, where `P_ClearACSVars(true)` clears world and global
  variables, starts the table over.
- Leaving a hub map for another of its hub parks its actors' slots (`G_SnapshotLevel`, then the level's
  actors are destroyed); a player's pawn and its inventory travel and stay live. Coming back, each
  actor rebuilt from the snapshot claims its slot again. `G_ClearSnapshots` releases every parked slot.
- With no free slot left, the least recently visited hub map's parked slots are released first, with a
  warning; with none parked, a request returns 0 with an error.
- Savegames keep the table in a chunk (`fhNd`) of their own, read before the level is rebuilt so its
  actors can claim their slots; map keys are saved as names, since name numbers change between runs.
  Actors save their slot from SAVEVER 4533.
- A slot holds up to 64 key/value pairs (`kMaxDataKeys`), keys as name numbers. Releasing the slot clears
  them; parking keeps them; the 65th key is refused with one warning a map. The chunk's version 2 saves
  them with their keys as names; a version 1 chunk loads without values.

## Hooks

- `actor.h`: `AActor::fua_handleSlot`.
- `p_mobj.cpp`: `AActor::Destroy` releases the slot, `AActor::Serialize` saves and claims it.
- `g_level.cpp`: `ParkLevel` after a hub map's snapshot, `EnterLevel` after a level loads.
- `g_mapinfo.cpp`: `G_ClearSnapshots` releases the parked slots.
- `features/net-snapshot`: `ActorBehindMirror`, the actor a mirror draws.
- `g_game.cpp`: the savegame chunk, and `FinishLoad` after the level is rebuilt.
- `p_acs.cpp`: the eight functions, and `P_ClearACSVars(true)` resetting the table.
- `version.h`: SAVEVER 4533.
- `features/acs-argcount`: each function's minimum argument count.

## Tests

- `computation/handles_compute_test.cpp`: encoding, reuse order, retirement, capacity, parking, the
  release-oldest valve, reset, the savegame chunk with damaged inputs, and the values: the 64-key cap,
  their life with the slot, their keys remapped on load, and version 1 chunks.
- `fuactl scenarios acsactordata`: setting, reading in any case, defaults, a handle as a value, clearing,
  the 64-key cap's single warning, a removed actor's values not reaching its slot's next actor, and a
  savegame round trip.
- `fuactl scenarios netacsactordata`: the first step as CLIENTSIDE copies on the player's client and an
  observer, then on the server, each machine with its own store; a value only the server sets never reaches a
  client or a late joiner, and a barrel the server removes takes each client's values along
  (`fua_debug_netdrop=58` fails it).
- `fuactl scenarios acshandles`: getting, checking, pointers, the activator, a removed actor's slot reused
  one generation on, a savegame round trip, a map change and a new game.
- `fuactl scenarios acshandleshub`: three maps in a hub cluster, eight round trips with the table's counts
  steady and a value on the kept actor coming back each time, a removed actor staying stale, and leaving the hub freeing every parked slot. It fails with
  `ParkLevel` taken out.
- `fuactl scenarios netacshandles`: a server sends a net ID to a clientside script, which takes its own
  handle, on the player's client and a late joiner; its copies configs run `acshandles`' first step on
  the server, the player's client, an observer and a late joiner, then a barrel the server removes leaves
  each client's handle to it stale (`fua_debug_netdrop=58`, the removal unsent, fails them).
- `fua_handles` (dev mode) prints the table's slots by state.
