# movers: sector movers on the snapshot path

Covers: src/zandronum/src/features/net-snapshot/movers.h, src/zandronum/src/features/net-snapshot/movers.cpp, src/zandronum/src/features/net-snapshot/computation/mover_compute.h, src/zandronum/src/features/net-snapshot/computation/mover_compute.cpp, src/zandronum/src/features/net-snapshot/computation/mover_compute_test.cpp

## Responsibility

The mover classes' half of the snapshot path that is not the server's capture table: each class's
`SnapCapture` and `SnapApply`, the client's record sink with its hold and map reset window, plane
ownership, the client's side of the mover gate, the join's link order, the questions the stock
thinkers ask before they move or sound, and the client state that DoScroller and DoPusher carry. The
server's slot table, the legacy stops and the mover sounds are in `capture.cpp`
([capture.md](capture.md)); polyobjects are in `polyobj.cpp` ([polyobj.md](polyobj.md)). The design:
[Movers](design.md#71-movers) and [Sectors, polyobjects and
ACS](design.md#7-sectors-polyobjects-and-acs).

`movers.h` is included by `p_spec.h`, so every stock caller compiles against it unchanged.

## Capture and apply

Each tracked class has two members, declared in `p_spec.h` and defined here:
- `DPlat::SnapCapture`, `DCeiling::SnapCapture`, `DFloor::SnapCapture`, `DDoor::SnapCapture`,
  `DElevator::SnapCapture`, `DPillar::SnapCapture`: fill the class's body from its fields and hand
  it to the server's capture (`MoverCapturePlat` and the rest). Each `Tick` calls it at both ends,
  and `Destroy` with `bGone`.
- `DPlat::SnapApply`, `DCeiling::SnapApply`, `DFloor::SnapApply`, `DDoor::SnapApply`,
  `DElevator::SnapApply`, `DPillar::SnapApply`: load a record's body into the client's thinker,
  writing the plane only where the record says the client cannot derive it.

A door's `m_BotSpot` crosses as a line end, encoded and decoded here, since vertex numbering differs
where a client builds GL nodes.

## The client sink

`MoverInstallSinks` installs the sink with `SetMoverSink`; `entities.cpp` routes every arriving
`SNAPEV_MOVER` to it, between `MoverBeginDatagram` and `MoverEndDatagram`, which carry the
datagram's reset count. On arrival the sink refuses a record with no level loaded, drops one counted
before the client's reset count, and holds one counted past it, or arriving while a reset window is
open or before the count is known: coalesced per plane and generation, at most three a plane. A
record applied now, or replayed from the hold later, then goes through `snapmv_ApplyRecord`, which:

1. refuses one built before the gate's record floor, which `MoverSetServerGate` sets at a flip;
2. hands a polyobject record to `PolyApplyRecord`;
3. refuses one for a sector, class or length this build cannot use;
4. refuses one older than a record already applied to its plane (a per-plane stamp, rebuilt when the
   stream epoch moves), which matters more than for a ghost: a stale mover record stands until the
   next transition, where a stale ghost record is corrected by the next refresh;
5. checks the plane's occupant by class and generation, evicting another mover for a live record and
   ending a tracked occupant for a GONE;
6. applies it: finds or makes the thinker and calls its `SnapApply`, or for a GONE writes the plane
   it carries (`MoverApplyGonePlane`) and destroys the thinker.

`MoverSetResetCount`, from `DestroyAllSectorMovers` and `SnapJoin`, replays the hold for that count
in arrival order and ticks each replayed mover forward through its own `Tick` by the gametics since
its newest record arrived, up to 5 s.

## The reset window

`MoverResetBegin`, from the client's `GAME_ResetMap`, opens a window in which nothing is owned and
destroys the client's movers (`DestroySectorMovers`); `MoverResetEnd`, from the
`DestroyAllSectorMovers` the server sends after its absolute writes, closes it. Inside it
`SetSectorFloorPlane` and `SetSectorCeilingPlane` skip linked propagation (`MoverInReset`). A window
open across two reports is stuck, and turns ownership and linked propagation off until a reconnect.

## What the thinkers ask

A gated client runs its movers but owns nothing, so each stock thinker asks this module the
questions its own state used to answer, one call a site:

- `MoverHoldsPlane`, `MoverHeldPlanes`: whether a blocked record-made mover holds rather than
  reverses, for one plane or for a two-plane class's pair.
- `MoverPlaneStalled`, `MoverPlanesStalled`: the stall bit a plane carries now, folded from what it
  was and what the record says.
- `MoverPlaysWaitSound`, `MoverClientSkipsWait`, `MoverCountDoorWait`, `MoverCountDoorWaitEnded`:
  whether a wait's sound is this client's to play, whether the wait itself is the server's to time,
  and the counts behind the line that reports it. `MoverClientSkipsWait` takes the class and leaves
  the wait to the server wherever `MoverSuppressesLegacy` is false, so a client below the gate times
  none of its own.
- `MoverClientDerivesReversal`: whether the client turns a ceiling around itself rather than waiting
  to be told, which takes the class for the same reason.
- `MoverSkipsPlaneWrite`, `MoverSkipsMidtexY`: whether a legacy plane or midtexture write must stand
  aside for what a record owns, counted apart (`MoverCountPlaneSuppressed`,
  `MoverCountLinkSuppressed`, `MoverCountMidtexSuppressed`).
- `MoverDiscardsSectorVeto`, `MoverDiscardsAttachedVeto`, `MoverCountNoBlock`,
  `MoverCountAttachedVeto`: the vetoes a client overrides, since its movers must not stall on actors
  it does not own, and the counts that say how often it did.
- `MoverLegacyStandsAside`, `MoverMadeByLegacy`: whether this class's legacy handlers defer to
  records, and the mark a legacy-made mover carries so a gate flip knows which to drop.

## Data structures

- `SnapMoverHeld`: one held record, with its class, flags, generation, index, body, frame, reset
  count, and the client's gametic when it arrived, which the replay's advance counts from.
- The per-plane ordering stamps, the reset count and window state, and the gate's state.
- `SnapMoverClientStats`, `SnapMoverGateStats`, `SnapScrollPushClientStats`: the counters behind the
  client's `[snap] movers`, gate and scroller lines.
- `SnapLinkEdge` (`computation/mover_compute.h`): one edge of the sector link graph, shared by the
  write order and the census.

## Public functions

The sink, the hold and the window:
- `MoverInstallSinks`, `MoverBeginDatagram`, `MoverEndDatagram`.
- `MoverSetResetCount`, `MoverForgetResetCount`, `MoverBeforeResetRecords`.
- `MoverResetBegin`, `MoverResetEnd`, `MoverInReset`, `DestroySectorMovers`.
- `MoverApplyGonePlane`: a GONE's plane written where no local mover took it.
- `MoverEvictForWaggle`: a legacy waggle taking a plane ends the tracked occupant there.
- `MoverMoveAttached`: moves a corrected plane's 3D midtextures and linked sectors by the same
  delta, as `DMover::MoveAttached` does.

Ownership, which keeps the join's absolute writes off what a record owns:
- `MoverOwnsPlane`, and per class `MoverPlatOwnsPlane`, `MoverCeilingOwnsPlane`,
  `MoverFloorOwnsPlane`, `MoverDoorOwnsPlane`, `MoverElevatorOwnsPlane`, `MoverPillarOwnsPlane`.
- `LinkControlOwned`: a plane linked to an owned control. `Midtex3dLineOwned`: a line attached to an
  owned plane.

The gate, client side:
- `MoverSetServerGate`, `MoverForgetServerGate`: `SnapMoverGate`'s state and record floor, and their
  reset at disconnect.
- `MoverSuppressesLegacy`: the server's gate is on and the class is tracked. It decides the legacy
  handlers, ownership, eviction on a GONE, timed doors and the sector sink's own refusal: below the
  gate a record is not applied, the newest per plane and sector being kept for the gate turning on
  as the polyobject sink's is ([polyobj.md](polyobj.md)).
- `ServerGatesLegacy`: the server's latch alone, for the mover code that needs it without a class in
  hand; the latch itself is `capture.cpp`'s ([capture.md](capture.md)).
- `ClientMoverGateStats`.

The join:
- `LinkWriteOrder`: builds the map's link edges and orders them (`ComputeLinkWriteOrder`, below);
  returns the sectors on or behind a link cycle.
- `P_LinkPlaneBits`: `p_linkedsectors.cpp`'s plane bits for one plane.
- `LinkCensus`: the link graph and the edges an index-ordered pass writes in the wrong order, behind
  `fua_snap_linkcensus`.

Scrollers and pushers:
- `SpecialResolveSource`: a point pusher's source by NetID, real actor then mirror, dropped when the
  id holds another class.
- `SpecialSendJoinState`, `SpecialApplyJoinState`: the scrollers, pushers and waggles a joining
  client is told about, and the client's side of it.
- `SpecialClearForFullUpdate`: destroys scrollers, pushers and waggles before a repeated full update
  re-sends them.
- `SpecialCountScroller`, `SpecialCountPointPusher`, `SpecialClientStats`: what DoScroller and
  DoPusher carried.

Counters and names:
- `MoverCount*` (16): the client's counts, each at the point where its event happens (a crush,
  a held tic, a wait, a vetoed block, a suppressed legacy write, a record's outcome).
- `MoverClientStats`, `MoverStaleRecords`.

The movers' decisions (`computation/mover_compute.h`), also used by `polyobj.cpp`, `capture.cpp` and
`ghostactor.cpp`:
- `ComputeLinkWriteOrder`: every sector after each control driving it, by longest path over floor
  and ceiling links, the sectors on or behind a link cycle appended last; and the planes the links
  drive.
- `ComputeMoverClassName`: a class's name for the reports, "?" past the classes.
- `ComputeMoverNoFlags`: the flags that narrow a GONE off a plane set.
- `ComputePolyTurns`: whether a polyobject action rotates (rotate, swing) rather than translates.
- `ComputePolyCompareKey`: a polyobject capture with the phases its dirty compare leaves out zeroed:
  its position and angle, and each entry's speeds and countdowns. Every speed change they hide is
  the last-step clamp, which the client derives, or a door reversal, which flips `close` and
  `direction` too.

## Rules that must hold

- **Find, then correct.** The sink finds the thinker on the sector's plane (`floordata`,
  `ceilingdata`) rather than keying on a class's own id, since two-plane movers drive both from one
  thinker; it creates one only where none is there.
- **Records apply in order per plane, held records included.** A held record keeps its own frame,
  and the stamp refuses it on replay if something newer took the plane.
- **Every snapshot-path plane write carries attached geometry**, through `MoverMoveAttached`, since
  every correction after the join is relative.
- **A new class needs a line in each of** `snapmv_RecordClassOf`, `MoverPlanes`,
  `MoverSuppressesLegacy` and `MoverOwnsPlane`.
- **The client never blocks.** Its movers do not stall on actors it does not own; blocked-ness
  arrives in the record.
- **A plane is written on a mover's first record, at rest and on a stall, never while it moves.** A
  travel the client runs itself, and a write on top is a double displacement in one tic, drawn as a
  stutter. The first record is the trigger rather than the thinker's creation, since legacy's
  `client_DoPlat` creates a plat at join, which would then never take the plane. An at-rest
  correction is about one tic of delivery lag times the mover's speed (1 unit at SPEED(8), 8 at
  SPEED(64)), the size the client's `[snap] movers` line's at-rest corrections should read.
- **A plane and its TexZ move together**, `ChangePlaneTexZ` by the same delta, as every writer in
  the tree does. GL's flat vertex buffer is re-uploaded only when TexZ stops matching it, and only
  for a sector with no thinker, so a plane moved alone draws at its old height while collision uses
  the new one.
- **One question, one call.** A thinker asks this module rather than testing the gate itself, so a
  gate-off build takes the stock branch at every site and no site can drift from its neighbours.

## Engine hooks

Each site carries a `[Kaori-Chani]` line. Fifteen stock files call this header:
- `p_spec.h`: includes `movers.h`; declares each class's `SnapCapture` and `SnapApply`, and calls
  the six per-class ownership tests.
- `p_plats.cpp`, `p_ceiling.cpp`, `p_floor.cpp`, `p_doors.cpp`, `p_pillar.cpp`: `SnapCapture` in
  each `Tick` and destroy, and the questions above -- the plat's crush and wait counts, the door's
  wait and sound tests, the ceiling's reversal test, and each class's hold and stall folds.
- `cl_main.cpp`: the legacy mover handlers' gate and counts, ownership on `SetSectorFloorPlane`,
  `SetSectorCeilingPlane` and `SetLineTextureOffset`, the reset window and count, the server gate,
  waggle eviction, the scroller and pusher counts, the join state and the full-update clear.
- `g_game.cpp`: `MoverResetBegin` in `GAME_ResetMap`.
- `cl_pred.cpp`: `MoverInstallSinks` beside the local block's install.
- `cl_demo.cpp`: `MoverForgetResetCount` when a demo starts.
- `p_map.cpp`, `dsectoreffect.cpp`: the vetoes a client overrides.
- `p_spec.cpp`: `SpecialResolveSource` in the point pusher's `Tick`.
- `po_man.cpp`: `MoverPlaneStalled` for a polyobject door's plane.
- `sv_main.cpp`: `LinkWriteOrder`, `LinkCensus` and `SpecialSendJoinState` in the join's passes.

`P_LinkPlaneBits` runs the other way: stock `p_linkedsectors.cpp` defines it and this file calls it.

## Tests

`computation/mover_compute_test.cpp` is a GoogleTest file under the 100% line gate and takes all of
the unit's lines and branches: link order (no links, a chain, a diamond with a long side, a link
driving both planes, a cycle with a sector behind it, and a type driving neither) and mover records
(every class's name and one past them, each plane set's flags, each action type, and the compare
key's cleared and kept fields).

The bodies' codec is `format_test.cpp`'s ([format.md](format.md)). Behaviour is checked live:
`fua_snap_planedump` diffed across server and clients with the movers stopped (the Phase 5
acceptance in the design doc's [Migration plan](design.md#9-migration-plan-dual-stack)),
`fua_snap_debug_resetsplit` to hold a reset window open, and the client's `[snap] movers` line,
whose outcomes add up to the records received.

The `[snap] movers` line accounts for every record: received plus replayed is applied, gone, off, no
level, unusable, stale, deferred and from before a reset; deferred is replayed, dropped and
coalesced plus the change in what is held. `noSink` above 0 means records arrived with no sink
installed, not a feature that is off, and `crushes survived` must read 0. The two veto counts span
every mover class and the crush count only `DPlat`, so neither is taken from the other. On the
scroller line, `unresolved` is tics a point pusher went without its source (expected before the
first snapshot and while its ghost is out of view), `again` a new mirror, `gone` a source dropped
because another class holds its id, and `replaced` is nonzero only after a repeated full update or
for a scroller a CLIENTSIDE script made before the first. Scrollers arrive at join, so their counts
show in the first report only. On the gate line, `refused` needs a reordered frame landing after the
gate turned off, `refused with legacy live` counts the sector records the sink turned away below the
gate, and `kept for a gate-on` the newest of those per plane. A keep from before a gate-off is below
the floor and is dropped at the next gate-on rather than applied, which leaves the keep for what it
is there for: a re-send that beat the reliable `SnapMoverGate(true)`.
