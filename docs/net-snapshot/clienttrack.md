# clienttrack: the per-client pass

Covers: src/zandronum/src/features/net-snapshot/clienttrack.h, src/zandronum/src/features/net-snapshot/clienttrack.cpp, src/zandronum/src/features/net-snapshot/computation/interest_compute.h, src/zandronum/src/features/net-snapshot/computation/interest_compute.cpp, src/zandronum/src/features/net-snapshot/computation/interest_compute_test.cpp, src/zandronum/src/features/net-snapshot/computation/ack_compute.h, src/zandronum/src/features/net-snapshot/computation/ack_compute.cpp, src/zandronum/src/features/net-snapshot/computation/ack_compute_test.cpp

## Responsibility

For one client and one pump: what that client holds, what its acks prove, and which records go out
in which datagram. `capture.cpp` captures the world once and calls `BuildDelta` for each client in
the map; this file decides the rest. It reads only wire values and the capture's priority hints, and
never an engine type, so the scheduling is testable apart from the game. The design: [Snapshot
protocol](design.md#4-snapshot-protocol) and [Rate
tiering](design.md#52-rate-tiering-where-the-budget-actually-goes).

## The model

Each client has three views of the world:
- **believed**: what the client is known to hold, as of its newest folded ack;
- **sent**: believed plus every group emitted since. Deltas are taken against this (optimistic
  referencing), so a change is sent once rather than for a whole round trip;
- **pending**: the datagrams sent and not yet resolved, each holding exactly the records it carried.

An ack (`OnAck`) names a datagram sequence and the 32 before it. Pending datagrams are resolved in
sequence order: delivered (the acked one, or its bit set), lost (its bit clear and at least
`SNAP_REORDER_WINDOW` behind), or unknown, where the walk stops so that `believed` stays the
in-order fold of what arrived. A delivered datagram folds only the groups it actually wrote. A lost
one is repaired: every entity, psprite, mover and property slot it carried reverts its `sent` to
`believed`, whole entity rather than group, so the next delta re-sends it, and each repaired entity
and psprite is then rebuilt over the datagrams still in flight (`snapcl_RebuildRepaired`):
`believed` with every pending record for it merged over in order, what the client will hold once
they land. Nothing is retransmitted as such.

With `fua_snap_debug_refcheck` on (off by default, costing nothing then), every ack also checks each
client's references apart: every id with a record in flight against `believed` with those records
merged over, every present id with none against `believed`, and every psprite against its newest
record in flight or `psprBelieved`. `[snap] reference check` counts those that differ. It checks the
invariant optimistic referencing rests on directly, where the client's checksum loses most of its
coverage under heavy loss by design, since a record settles only once every loss behind it is
repaired.

The reading is `ack_compute`, a pure unit: `ComputeAckRead` says whether an ack is stale, ahead of
what was sent (naming a datagram not yet sent, from another stream or a bad client, which would
make every true ack after it stale; ignored and counted, `AcksAhead`), wider than its history
(`SNAP_ACK_HISTORY` datagrams) or readable, and `ComputeAckVerdict` gives each pending datagram's
verdict through `ComputeProvenDelivered`: `SNAP_ACK_AHEAD`, `SNAP_ACK_DELIVERED`, `SNAP_ACK_WAIT`,
`SNAP_ACK_LOST` or `SNAP_ACK_LOST_UNPROVEN`. `OnAck` walks the pending datagrams, stopping at AHEAD
or WAIT, and folds, repairs and counts.

Each client keeps `lostMask`, the datagrams declared lost within 32 of its newest processed ack
(`ComputeLostMaskAdvance`, `ComputeLostMaskMark`), and a later ack setting one of their bits is
counted once on the datagram delivery line as "set after declared lost" (`ComputeSetAfterLost`). The
client refuses a datagram that arrives after the server would declare it lost, or past the history
(`ComputeAckTooLate`, [apply.md](apply.md)), so the count reads 0 bar datagrams declared lost
without proof, behind an ack gap wider than the history, that had arrived.

The ack and every loss test count datagrams (`seq`); ages and deadlines count frames. A frame of
several datagrams has several sequences, and a frame-keyed ack would fold records from datagrams
that never arrived.

## Building a frame

`BuildDelta`, in order:
1. A client whose oldest pending frame is `SNAPCL_MAX_PENDING` frames old is reset to a baseline.
2. **Departures**: leaves for ids gone from the world, and for ids whose enter may have arrived
   unconfirmed (`maybe`). Ahead of everything, so a dead projectile never lingers.
3. **Movers**, on a pass capped at `SNAPCL_MOVER_MAX` records, from the capture table by slot `seq`.
4. **Property sets**, which only a script's change makes: each slot whose generation this client has
   not been sent, as a whole `SNAPEV_PROPS` payload rather than a diff, since a client that missed
   one must not hold half a set. The slot's generation is the dirty test, folded on the ack that
   proves its datagram and reverted when one is proven lost.
5. **Psprites**, whole-state against what the client will hold, capped at
   `SNAPCL_PSPRITE_MAX_REPORT`. Not suppressed: a walker cannot predict a trigger pull.
6. **The local block**, unconditional while the client has a pawn and never rationed: a frame
   without it stalls the client's reconciliation cursor.
7. **Branch events**, queued as the entities are classified and written ahead of the drain on at
   most `SNAPCL_EVENT_SHARE_PCT` of the budget. Delivery is blind copies, then drop:
   `fua_snap_event_copies` on consecutive frames, a death `SNAPCL_EVENT_TERMINAL_COPIES` times
   `SNAPCL_EVENT_TERMINAL_SPACING` frames apart. Not retried on loss, since the ack history proves
   a loss only after `SNAP_REORDER_WINDOW`, when a death's sequence has ended, and not reliable,
   whose backlog grows without bound for a stalled client: a stale flinch is worth nothing, and the
   entity's next record supersedes it. Deaths go in a pass of their own first.
8. **Entity updates**: each dirty entity is placed in a band, then the bands are drained until
   `fua_snap_maxbytes` is spent, in four passes: the refresh bands on their own quotas; overdue
   through missile in strict order, with overdue, top and enter capped together at half the budget,
   as they are again in the mop-up; the bands below d0, each to a slice of `fua_snap_bandreserve`
   percent in proportion to its population; then every band but the refresh bands again, otherwise
   unrestricted, which is where d0 is served. Each band is walked from a cursor that persists and
   advances by the entries consumed.
9. **Layout**: the selected entity records are sorted by id, every record's length is estimated,
   `ComputeChunkCuts` ([capture.md](capture.md)) cuts the payload at `chunkBytes - 1`, and the
   records are encoded chunk by chunk. Each chunk gets its own sequence and pending entry, so a lost
   datagram costs exactly its records.

## The bands

`SNAPCL_TIER_OVERDUE` (past the starvation bound), `SNAPCL_TIER_TOP` (players and the aging guard),
`SNAPCL_TIER_ENTER` (enters, deaths, promoted terminal records), `SNAPCL_TIER_MISSILE` (projectiles
inside `fua_snap_missile_dist`, capped at `fua_snap_missile_share` percent), the distance bands from
`SNAPCL_TIER_DIST` to `SNAPCL_TIER_DISTLAST` (far and behind), `SNAPCL_TIER_REFRESH` (held records
owing position, on the `SNAPCL_AGE_COSMETIC` clock), `SNAPCL_TIER_ANIMREFRESH` (held records owing
only appearance, quota derived from `fua_snap_animref_interval` and capped at `SNAPCL_ANIMREF_MAX`),
and `SNAPCL_TIER_COSMETIC` (distant animation frames, withheld). `SNAPCL_TIERS_REPORT` sizes the
report.

The classifier is `interest_compute`, a pure unit, with the bands, the age bounds
(`SNAPCL_AGE_PROMOTE`, `SNAPCL_AGE_FORCE`, `SNAPCL_AGE_COSMETIC`), the distance rings
(`SNAPCL_DIST_RINGS`, `SNAPCL_DIST_BUCKETS`), the priority hints the capture sets (`SNAPPRI_PLAYER`,
`SNAPPRI_MISSILE`, `SNAPPRI_HOMING`, `SNAPPRI_TERMINAL`) and the settled scheduler constants.
`snapcl_Tier` fills `SnapTierIn` from the record, the view and the settings (`fua_snap_entergrade`,
`fua_snap_missile_dist`, `fua_snap_cull_dist`, the cone), calls `ComputeTier`, and counts the
terminal sequence it reports (`SnapTierOut`: seen, promoted inside the frame's
`SNAPCL_TERMINAL_MAX`, or capped). Event delivery is `ComputeEventDelivery`.

Two suppressions shrink records rather than move them: appearance suppression withholds APPEAR when
`AppearDerivable` says the client gets there itself (`fua_snap_appear_predict`, with one id in
`SNAP_APPEAR_EXEMPT_STRIDE` exempt as the control group, `fua_snap_appear_exempt`), and missile
prediction withholds position while the client's own extrapolation stays within
`fua_snap_missile_predict` units, bridging at most `SNAP_BALLISTIC_MAX_TICS`. The gravity class is
bridged by the law ([Interest management and
bandwidth](design.md#5-interest-management-and-bandwidth)).

## Data structures

- `SnapClientState`, one per slot: `believed`, `sent` and `present` by id; `unackedMask`, the groups
  emitted since the newest ack (each ack clears it, each emit sets it), which are re-sent even when
  the value matches, but only once the pending depth plus one frame has passed since the entity was
  last sent; `pending`; `believedIds`; the leave markers, set from a leave's send until it or a
  later enter folds, during which an entity back in the world is sent enters; `maybe`, the ids
  whose enter rode a datagram lost without proof, each sent a leave once out of the world;
  `enterFrame`, the frame each id's enter went out; the psprite and mover sent and believed pairs
  (`moverBelievedSeq`); `propSentGen` and `propBelievedGen`, one generation a property slot
  (`SNAPPROP_MAXSLOTS`, a kilobyte a client); the per-band cursors; and `lastAckedSeq` and
  `nextSeq`, which starts at 1 and is never rewound.
- `SnapPendingFrame`: one datagram in flight, with its `seq`, its frame and the rows it carried
  (`SnapSentEntity`, `SnapSentPSprite`, `SnapSentMover`, `SnapSentProps`).
- `SnapPendingEvent`: one branch event queued for one client.
- `SnapChunk`, `SnapViewpoint`: one datagram's slice of the payload, and where the client looks.
- `SnapEmitRec`: one staged record or event, before layout; `s_bounds` and `s_cuts` hold the
  estimated starts and the cut.
- The census structures: `SnapLocalEmitStats`, `SnapWireCensusOut`, `SnapClientHealth`,
  `SnapVisibilityCensus`.

## Public functions

Lifecycle and acks:
- `ServerInit`, `ResetClient`, `ResetAllClients`, `ForgetMovers`.
- `OnAck`: the fold and the repair, above.
- `BuildDelta`: one client's frame, above.
- `QueueEvent`: queues a branch event for one client; the build calls it for each entity
  `BranchedThisTic` flags.
- `Believes`, `BelievedFrames`: whether a client holds an id, and for how many frames; the sound
  path's grace reads the second, with `g_snapFrame` as now.
- `MoverAcked`, `StreamStarted`: the mover sweep's release test, and whether this connection has
  acked anything.
- `AckLag`, `NewestSentSeq`: the ack lag a datagram to this client echoes (`ComputeAckLag`), and
  the newest sequence it has been given, which a report takes that lag against.
- `UpdateCullFov`: the `fua_snap_cull_fov` callback's cached cosine.

The scoring (`computation/interest_compute.h`):
- `ComputeTier` (`SnapTierIn`, `SnapTierOut`, `SNAP_TERMINAL_NONE` to `SNAP_TERMINAL_CAPPED`): the
  band a dirty record goes in, and the census observations beside it.
- `ComputeRefreshBand`: `SNAPCL_TIER_REFRESH` for a held record owing position,
  `SNAPCL_TIER_ANIMREFRESH` for one owing only appearance.
- `ComputeEventDelivery`: a branch event's copies, spacing and deadline (`SNAPCL_EVENT_DEADLINE`,
  `SNAPCL_EVENT_TERMINAL_COPIES`, `SNAPCL_EVENT_TERMINAL_SPACING`).
- `ComputeOwnEnterBucket`: the own-missile census's wait buckets.
- `ComputeBandQuota` (`SnapBandQuota`): a refresh band's quota for one client, cycling its
  population in the band's interval and cut to its ceiling, and whether that client has just
  become clamped, from the clamp memory the caller keeps per client.

The ack reading (`computation/ack_compute.h`):
- `ComputeAckRead` (`SnapAckRead`): whether an ack is stale, ahead of `nextSeq`, wide, and has
  readable history.
- `ComputeProvenDelivered`: whether an ack proves a pending datagram arrived.
- `ComputeAckVerdict`: an ack's verdict on one pending datagram, above.
- `ComputeAckLag` (`SNAP_ACK_LAG_NONE`, `SNAP_ACK_LAG_FAR`): how far behind a datagram the newest
  ack processed is, which its header echoes.
- `ComputeAckSettled`: the client's test of whether the server had repaired every loss before a
  datagram when it built it, from that echo; the checksum counts a record only then.
- `ComputeAckTooLate`: the client's test of whether a datagram arriving now is one the server
  declares lost, inside the history or past it, which it then refuses.
- `ComputeAckFold`: the client's ack state after a datagram arrives, the newest sequence and the
  history behind it.
- `ComputeLostMaskAdvance`, `ComputeLostMaskMark`, `ComputeSetAfterLost`: the server's record of the
  datagrams it declared lost, and the bits a later ack sets for them.

Report readings, per frame or per window (each feeds a `[snap]` line):
- `LastTier*` (2), `LastReserve*` (4), `LastResend*` (2), `LastDeferred`, `LastFresh`, `LastForced`,
  `LastCosmetic`: the last frame's records by band, the reserve's spend, re-sends, and what was
  deferred, new, forced and held.
- `ServerEventStats`, `EventEmitStats`, `EventTerminals`, `EventSkips` and `ServerEventStatsReset`:
  queued, sent, expired and dropped events, the emitter's share, deaths apart, the skips, and their
  reset.
- `AcksAhead`: acks ignored for naming a datagram not yet sent, on the datagram delivery line.
- `RepairStats`, `AckLossStats`, `MaybeStats`, `PresenceStats`: what repair reverted -- frames,
  entities, movers and property slots -- frames resolved and lost, unconfirmed enters, and enters
  forced by a leave in flight.
- `PresenceLog`: one line of the presence log (`fua_snap_debug_presencelog`,
  [capture.md](capture.md)): the frame, the client and every id it may hold, present, leaving,
  `maybe` or entered in a pending datagram.
- `AppearStats` (by `SNAPCL_AppearPopNames`), `PredictStats`, `ServerLawStats`: the two suppressions
  and the gravity bridge.
- `AnimrefQuota`, `TerminalStats`, `ReentryStats`, `BandComposition`, `OwedStats`: the appearance
  quota, terminal promotions, returns from a hold, band contents and what the contested set owes.
  The quota is one client's, the worst: the largest held population, which is both the
  longest interval and the first the ceiling cuts, and the report names it. Each client's own
  clamp memory also decides its `refresh clamped` and `animref clamped` lines, so one client
  being clamped neither silences nor repeats another's.
- `ServerPSpriteStats`, `ServerLocalStats`, `ServerMoverStats`, `TargetStats`, `OwnEnterStats`,
  `WireCensus`: per-stream counts and the encoding census.
- `PropWalkStats`: property sets sent from a slot, and the walks a client's byte budget cut
  short, both on the properties line.
- `PendingFrames`, `BelievedCount`, `LastAckedSeq`, `AggregateHealth`, `GetCensus`,
  `ResetWindowStats`: client health and the visibility census.
- `ReferenceCheckStats`: the reference check's ids in flight, ids at rest and psprites, and those
  that differ.

## Settings

All server only. Archived: `fua_snap_event_copies` (1), `fua_snap_missile_share` (20). Unarchived:
`fua_snap_missile_predict` (16 units; 0 off), `fua_snap_appear_predict` (on),
`fua_snap_appear_exempt` (on; off only to price the control group), `fua_snap_animref_interval` (115
tics), `fua_snap_refresh_max` (40 records a frame; 0 uncapped), `fua_snap_wirecensus` (the encoding
census). The settled scheduler values are constants in the header: `SNAPCL_HOMING_FLOOR`,
`SNAPCL_BALLISTIC_FLOOR`, `SNAPCL_CONE_NEAR`, `SNAPCL_TERMINAL_MAX`.

## Rules that must hold

- **Delta against `sent`, fold into `believed`, repair whole entities.** A fold writes only the
  groups a record carried; copying the whole state would mark withheld groups as held, and they
  would never be sent.
- **Resolve in sequence order and stop at the first unknown datagram.** Folding a newer datagram
  before a late older one leaves the older values in `believed`.
- **Every client has its own sequence**, never rewound: a shared counter would shrink each client's
  32-datagram ack window, and a rewound one would read as too late on the client, where a new
  stream begins only at `SnapJoin`.
- **A frame with no bytes is never recorded as pending**: nothing would ever ack it, and the
  `SNAPCL_MAX_PENDING` escape would re-baseline the world every 64 frames.
- **A property slot's generation is the whole dirty test.** A reused slot takes a generation from
  one global counter that never repeats and never reaches 0, so a client cannot read a new
  occupant's set as the previous one's.
- **Reserved passes for what travels no other way**: departures, movers, property sets, psprites and
  the local block go before the ranked drain, each with a ceiling that means only "stop this pass".
- **Each cap says what it stops.** The refresh allowance stops the refresh pass; the aging cap on
  the absence bands and a band's reserved slice move on to the next band; the missile ceiling stops
  the missile band; only the budget stops everything. A cap falling through to "the packet is full"
  would starve every band below it.
- **Walk a band from a persistent cursor.** Band membership is in NetID order, so a walk that
  restarts at the front serves the same entries forever and late spawns wait.
- **A band ranked last on a saturated budget never sends**, so every band that must be served has a
  quota or a reserve, and the starvation guard runs after classification, so it rescues only records
  that competed and lost.
- **Cuts fall on records.** The cut is drawn over estimated lengths, at most one record a chunk
  opens with a raw id, and the encoded chunk is bounded by `chunkBytes`.
- **No engine types.** What needs one (the walker, the local capture, a player's reserved id) is
  asked of `capture.cpp`.

## Engine hooks

None. Every call comes from `capture.cpp`.

## Tests

The cut's tests are `budget_compute`'s ([capture.md](capture.md)).
`computation/interest_compute_test.cpp` takes every path through the classifier, what it observes
beside the band, the refresh bands and their quotas, event delivery and own-missile buckets: all of
the unit's lines, and all but two branches, neither reachable -- a terminal record is always floored
below the appearance test's bands, and the spacing clamp sees only its two constants.
`computation/ack_compute_test.cpp` takes an ack's standing at each edge, an ack of the newest sent
datagram read and one of the next ignored, every verdict at the edges of the history and the reorder
window, a stream of datagrams lost, late and reordered read back through its acks, the ack lag, the
settled test at its edges and a lossy stream with a two-frame round trip in which nothing the client
settles was built before the server repaired a loss behind it, and the too-late test and the record
of declared losses at their edges, the client's fold, and a stream holding one datagram in seven
back by 12 and by 40, in which the client's drop leaves no bit set for a datagram the server
declared lost, and applying them sets one each by 12 and none by 40, past the history: all of the
unit's lines and branches. Both run in the GoogleTest suite, under the 100% line gate.

The rest needs a server: the `[snap]` report's priority, band, reserve, ack, repair and event lines,
`fua_snap_debug_loss` and `fua_snap_debug_ackdelay` for loss and reordering, the client's checksum,
which reads DIVERGED if `believed` ever differs from what the client holds, and
`fua_snap_debug_refcheck`'s reference check, which still works under heavy loss.
