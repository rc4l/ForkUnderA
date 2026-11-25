# polyobj: polyobjects on the snapshot path

Covers: src/zandronum/src/features/net-snapshot/polyobj.h, src/zandronum/src/features/net-snapshot/polyobj.cpp

## Responsibility

Polyobjects as one record each (`SNAPMV_POLY`): the action list every `FPolyObj` keeps, the server's
capture of the whole polyobject, each action's capture and load, the polyobject sounds a gated
client needs, and the client's record sink with its gate handling. Records travel through the mover
machinery: the server's slot table in `capture.cpp` and the client's sink, hold and reset window in
`movers.cpp` ([movers.md](movers.md)). The design: [Movers](design.md#71-movers), its Polyobjects
part.

`polyobj.h` is included by `p_spec.h`.

## The record

One record per polyobject, not per action, since the Polyobj_OR specials run a second action on a
busy polyobject and a create could not tell an ended predecessor from a concurrent one. The header
is `StartSpot`, `angle` and a count, which describe the polyobject completely; then each live action
in creation order (rotate, move, move-to, slide, swing) with its generation and flags. No entries is
the GONE. The body is `format.h`'s `SnapMoverPoly`.

## Server

- `PolyLink`, `PolyUnlink`: each `FPolyObj` keeps its actions on `snapActions`, linked from the
  action's constructor and from `DPolyAction::Serialize` on a savegame or hub load, and unlinked in
  its `Destroy`. That load also puts the action back in the polyobject's `specialdata`, which the
  engine's own busy tests read.
- `PolyCapture`: the whole polyobject into its slot (`MoverCapturePoly`), each action filling its
  entry through `DPolyAction::SnapCapture`. Entries past the body's room are left out and counted.
  With `bAllocate` false it only updates a slot the polyobject already holds, which is how an
  action's `Destroy` recaptures its polyobject without taking a new slot.
- `PolyNewGen`: a generation for an action, 0 off the server, so a client's own actions carry none.
- `PolySendStartSound`, `PolySendStopSound`: the sounds a polyobject's legacy command used to carry,
  sent by polyobject number while the gate is on so a client that runs no legacy handler still hears
  them; gate off, stock's own bytes stand.
- The slot's owner is the `FPolyObj`, so a new action on a polyobject takes the slot over by
  construction. A perpetual rotator is re-emitted every 175 tics, each polyobject on its own phase,
  since nothing it does is a transition.

## Client

- `PolyApplyRecord`: one record, from the mover sink or the hold's replay. It refuses a record older
  than the last applied to that polyobject, and while the polyobject's legacy handlers are live it
  keeps the newest record unapplied for a later gate-on. Otherwise it reconciles the client's
  actions to the record by generation and type: record-made actions not listed are destroyed, listed
  ones are loaded (`DPolyAction::SnapLoad`, `DMovePoly::SnapLoad`, `DMovePolyTo::SnapLoad`,
  `DPolyDoor::SnapLoad`), new ones are created. An action a CLIENTSIDE script made is left alone.
- Position is written per axis, translation for move, move-to and slide and rotation for rotate and
  swing, on the first record, a new action on that axis, an axis at rest, the GONE and a rotator's
  re-anchor: never while the axis moves.
- `DPolyAction::SnapHolds`: a record-made action holds while blocked, and once its countdown has run
  out, until the record that ends it.
- `OwnsPoly`: whether the legacy position and rotation writes must skip this polyobject.
- `PolyGateOn`, `PolyGateOff`: at a mover gate change, drop the legacy-made or the record-made
  actions; a gate-on then applies each polyobject's kept record.
- `PolyDestroyAll`: a map reset's, every action on every polyobject.
- `PolyAdvance`: ticks a replayed polyobject's record-made, unblocked actions forward by the hold.
- `PolyCountSuppressed`, `PolyClientStats`: the `[snap] polys` line's counts
  (`SnapPolyClientStats`, with axes set by reason, `SNAPPO_SET_FIRST` to `SNAPPO_SET_RESYNC`).

## Rules that must hold

- **The record is the polyobject's whole state.** The client reconciles to it rather than applying a
  change, so a lost record is repaired by the next.
- **Never write an axis while it moves**: that is the stutter. Absolute writes of `StartSpot` and
  `angle` happen only at the moments listed above.
- **One sequence per polyobject.** An override move replays the start sound and its end silences a
  rotation that carries on, as in stock.
- **A reloaded action is linked and made busy.** `DPolyAction::Serialize` does both, since the
  constructor never runs for it, and a polyobject whose `specialdata` stayed null reads as idle to
  every special that tests it.
- **Limits**: an override polyobject with one action moving and another blocked on the same axis is
  written only when that axis rests; the server's thrust on the local player is not predicted.

## Engine hooks

Each site carries a `[Kaori-Chani]` line. Three stock files call this header:
- `po_man.cpp`: `PolyLink` and `PolyUnlink` in the actions' constructor, `Serialize` and `Destroy`;
  `SnapCapture` in each action's `Tick`; `SnapHolds` where an action would move; `PolyCapture` when
  an action is destroyed; `PolyNewGen` for a new action's generation; and `PolySendStartSound` and
  `PolySendStopSound` where the legacy commands' sounds were. `po_man.h` adds `snapActions` to
  `FPolyObj`.
- `g_game.cpp`: `PolyCapture` after a map reset restores each polyobject.
- `cl_main.cpp`: `OwnsPoly` and `PolyCountSuppressed` in `client_SetPolyobjPosition` and
  `client_SetPolyobjRotation`.

The five polyobject action classes need no stock declaration of their own: this tree keeps them in
`p_local.h`, which `polyobj.cpp` includes, so it has the complete types.

## Tests

`format_test.cpp` covers the polyobject body's walk and codec, and
`computation/mover_compute_test.cpp` which actions turn and the capture's compare key
([movers.md](movers.md)). Live: `fua_snap_planedump` adds a row per polyobject for the cross-machine
diff, and the `[snap] polys` line counts records, actions made and evicted, holds, axes set by
reason and rotator re-anchors. `done` takes a tic or so on a LAN, `evicted` counts an action ending
beside others and a churn handoff, `first` a new action joining a polyobject, `stale` reads 0
outside loss, and resync corrections stay small after the first.
