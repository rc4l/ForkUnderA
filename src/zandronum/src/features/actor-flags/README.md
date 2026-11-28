# features/actor-flags

An actor's flag changed by name, for DECORATE's `A_ChangeFlag` and ACS's `SetActorFlag` (`features/acs-parity`).

- `ChangeActorFlag(actor, name, on, printError, localOnly)`: `FindFlag` by name (`"Class.FLAG"` too, split by
  `ComputeFlagName`), deprecated flags through `HandleDeprecatedFlags`, `NOBLOCKMAP` and `NOSECTOR` relinked, the
  map's kill, item and secret totals moved (`ComputeCountDelta`), the invasion count kept, and a changed word sent
  through `features/net-replication` (`SendThingFlags`, `SendMapTotals`). False when the actor has no such flag.
- A kill counts only while the actor lives (`ComputeCountsKill`), as UZDoom's `ModActorFlag` has it, so a corpse
  given or losing `COUNTKILL` leaves the map's total alone.
- On a client the server owns the flags: a server actor's change waits for the server's send, as `A_ChangeFlag`
  always did, but `localOnly` (a clientside script) changes the client's own copy and leaves the server's totals.
- Deprecated flags (`FIREDAMAGE`, `LOWGRAVITY` and the rest) write other fields, which nothing sends to clients;
  that was so before this feature.

## Hooks

- `thingdef/thingdef_codeptr.cpp`: `A_ChangeFlag` calls `ChangeActorFlag`.
- `features/acs-parity`: `SetActorFlag` (ACS 202).
- `features/net-replication/actorstate`: `SendThingFlags` and `SendMapTotals`.

## Tests

- `computation/actorflags_compute_test.cpp`: the name split, the living kill and the totals' moves.
- `fuactl scenarios acsparity` and `netacsparity`: `SetActorFlag` clearing `SHOOTABLE`, an unknown flag, and
  `COUNTKILL` moving the map's monster total; the `decorate*` scenarios cover `A_ChangeFlag`.
