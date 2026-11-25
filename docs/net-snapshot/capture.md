# capture: the server's snapshot tick

Covers: src/zandronum/src/features/net-snapshot/computation/proptell_compute.h, src/zandronum/src/features/net-snapshot/computation/proptell_compute.cpp, src/zandronum/src/features/net-snapshot/computation/proptell_compute_test.cpp, src/zandronum/src/features/net-snapshot/computation/propcensus_compute.h, src/zandronum/src/features/net-snapshot/computation/propcensus_compute.cpp, src/zandronum/src/features/net-snapshot/computation/propcensus_compute_test.cpp, src/zandronum/src/features/net-snapshot/capture.h, src/zandronum/src/features/net-snapshot/capture.cpp, src/zandronum/src/features/net-snapshot/report.h, src/zandronum/src/features/net-snapshot/report.cpp, src/zandronum/src/features/net-snapshot/computation/budget_compute.h, src/zandronum/src/features/net-snapshot/computation/budget_compute.cpp, src/zandronum/src/features/net-snapshot/computation/budget_compute_test.cpp, src/zandronum/src/features/net-snapshot/computation/law_compute.h, src/zandronum/src/features/net-snapshot/computation/law_compute.cpp, src/zandronum/src/features/net-snapshot/computation/law_compute_test.cpp, src/zandronum/src/features/net-snapshot/computation/catchup_compute.h, src/zandronum/src/features/net-snapshot/computation/catchup_compute.cpp, src/zandronum/src/features/net-snapshot/computation/catchup_compute_test.cpp

## Responsibility

Everything the server does once per pump that is the same for every client: the world capture, the
decisions about what the legacy protocol stops sending, mover capture and mover sounds, cosmetic
events, snapshot sounds, the property slots, the `.zsnap` recorder and the `[snap]` report. What
each client is sent, and when, is the per-client pass in `clienttrack.cpp`
([clienttrack.md](clienttrack.md)), which this file calls once per client per pump. The design
behind it: the design doc's [Snapshot protocol](design.md#4-snapshot-protocol), [Interest management
and bandwidth](design.md#5-interest-management-and-bandwidth) and [Cvar and dead-code
triage](design.md#101-cvar-and-dead-code-triage).

## The pump

`ServerTick` runs at the end of every `SERVER_Tick`, after the game tics that pump simulated.
In order:

1. A delayed reset end (`fua_snap_debug_resetsplit`) and the mover gate's latch, both ahead of the
   `fua_snap` test so that turning snapshots off still clears them.
2. The psprite capture: every player slot's two weapon layers, once for all clients.
3. Initialization on first use, the delayed acks of `fua_snap_debug_ackdelay`, and `MoverTick`,
   which sweeps gone mover slots before any thinker can capture into one.
4. The capture pass: every actor with a NetID, apart from spectators' pawns and owned inventory, is
   quantized into a `SnapEntityState` and diffed against the previous capture; enters, dirty records
   and leaves are written to the world diff with `WriteRecord`. Branch detection, the straightness
   tracker that sets the missile bits, the priority hints and the property capture run here, and the
   property sweep closes behind the pass.
5. The recorder, if `fua_snap_record` is running, and then the transmission: for each client in the
   map, `BuildDelta` builds that client's frame, the payload is cut into chunks, and each chunk goes
   out as its own unreliable `SVC2_SNAPSHOT` datagram, with the cosmetic events command appended to
   the last one when it fits. Unreliable, since the ack history and repair recover a loss and a
   retransmit would only delay superseded data. Each chunk is flushed, since
   `SERVER_CheckClientBuffer` launches only when the next command does not fit and would otherwise
   pack it with the traffic after it. `fua_snap_debug_loss` rolls once per datagram, and a dropped
   one is counted as sent, since to the server a drop must look like a datagram that left and never
   arrived. The header carries the frame, which counts pumps, and the absolute game tic, so a lost
   frame cannot desync the client's clock.
6. The report: `snap_MaybeReport` builds the window's `[snap]` lines into a queue, and
   `snap_DrainOneLine` prints one of them a pump. Both are `report.cpp`, which this file's counters
   feed through `report.h`, so an edit to a report line cannot move the capture loop's own code.
   Every figure it prints passes through `snap_RawToMS`, and it reads this file through
   `snap_CatchUpStats`, `snap_CosEnsure`, `snap_CosRow`, `snap_CosIsCosmetic`,
   `snap_CosCountersReset`, `snap_CosCountAlive`, `snap_ClientInMap`, `snap_PacketRoom`,
   `snap_EffectiveBudget` and `snap_ChunkLimit`.

The byte budgets are `budget_compute`, a pure unit: the snapshot command one datagram may carry, the
frame's budget and each chunk's limit, which this file reads from `sv_maxpacketsize` and
`fua_snap_maxbytes`; the chunk cut, which the per-client pass calls
([clienttrack.md](clienttrack.md)); and the cosmetic event bank, whose refill, order, fit, overflow
and append decide each client's events in `snap_CosClientPump`, which keeps the cull, the counters
and the copying.

The catch-up clamp is `catchup_compute`, also pure: `CatchUpTics` hands `ComputeCatchUp` the tics
the clock passed and takes back the tics to run, so a pump past `fua_snap_maxcatchuptics` drops the
rest and game time dilates rather than the world running behind its own clock ([Scale limits and
overload behaviour](design.md#54-scale-limits-and-overload-behaviour)). Gate off it caps nothing, so
a server without snapshots catches up as stock does. A run of dropping pumps that lasts a second
warns once every five, through the report's drain, skipping a level load's first seconds, and the
window's drops reach the `[snap] catch-up` line, which prints only when something was dropped.

The motion laws are `law_compute`: the gravity law's acceleration, which `LawGravity` takes from the
level, the sector and the class, and each tic's step of an id's straight and gravity runs, which set
the ballistic bits; `snap_NoteStraightness` keeps the runs per id, scores the bit and counts the run
lengths.

## Data structures

- `g_capture`: this pump's captured entities, in sweep order, each carrying its id;
  `g_capturePriority` holds the server-only priority hints beside it. The per-client pass reads both
  and never touches an engine type.
- `g_prevPresent`, `g_curPresent`: presence by id for the world diff, swapped at the end of the
  pump. `g_deltaBuf` holds the world diff, which feeds the recorder and the census, not the wire.
- `g_snapFrame`: the frame number, one per pump. It orders delivery and deadlines; elapsed time is
  measured in game tics, which ride beside it on the wire.
- `SnapMoverSlot`, `g_moverSlots`: the mover capture table, `SNAPMV_MAXSLOTS` slots keyed on the
  thinker (or the `FPolyObj`), each with its class, body, generation and a `seq` that only grows.
- `SnapPropSlot`, `g_propSlots`: the property table, `SNAPPROP_MAXSLOTS` slots keyed on NetID, each
  with the set a script left on that actor and the generation it last changed at. Marked by the
  capture pass and swept at its end, so a slot is freed the pump its actor leaves the world.
- `SnapCosMark`, `g_cosMarks`: per cosmetic copy, its row, its flags (flushed, held, a server
  script's activator) and when its hold began. `SnapCosEvent` is one event, made this pump
  (`g_ceEvents`) or carried one pump for a client (`g_ceCarry`).
- `g_branchedTic`, `g_straightRun`: per id, whether it branched this tic and how many tics it has
  held a heading.
- `SnapStats` and the window counters behind the report; `g_reportQueue`, the report waiting to
  print.
- `SnapCosPick`: one client's candidate event in a pump, its distance, length and whether it was
  carried; a capped pump's picks past the fit each get `SNAP_COSO_CARRY`, `SNAP_COSO_CARRIED_AGAIN`
  or `SNAP_COSO_NO_ROOM`.
- `SnapRunTrack`: one id's runs as the previous tic left them, the heading, velz and its change;
  `SnapRunStep`: what a tic's step found, whether it moved, compared, held, and the run a turn
  ended.
- `SnapDelayedAck`, `SnapOwnMissile`: the ack delay injector, and the ring of player-fired missiles
  the own-missile census reads.

## Public functions

The pump and the client lifecycle:
- `ServerTick`: the pump, above.
- `ClientConnected`, `ClientDisconnected`: per-slot state; both must be wired, since a slot whose
  state outlives its occupant is delta'd against the previous player's world.
- `ClientAcknowledged`: an ack of a datagram sequence, folded by the per-client pass.
- `MapChanged`: before a level loads, forgets presence on both sides, since `IDList::clear` hands
  the old map's ids to the new map's actors.
- `ResetEnd`, `MapStamp`, `MapResetCount*` (2): the map reset's count and stamp, sent with
  `DestroyAllSectorMovers` and `SnapJoin`.
- `ServerMapFrame`: the loaded map's position frame, which `SnapJoin` carries and every position
  written for the map is relative to. It is computed on first use in a level after `MapChanged`,
  from the map's vertices, the sector heights at each line's ends, the polyobjects' start spots and
  every actor (`ComputeExtentTake`, [entities.md](entities.md)), since the teardown hook runs before
  the next map exists. Clamps into it are counted, and the `[snap] frame` line reports the frame and
  the window's clamps when the origin is off 0, an axis is wide or something clamped.
- `GateOn`: whether the capture is running, which the legacy commands test before anything else.
- `ReportCvars`: every snapshot cvar not at its default, printed at the server's first snapshot tick
  and by the client when snapshot traffic starts.

A client's own missing-packet request for a sequence this connection never sent is skipped in
`sv_main.cpp`'s `server_MissingPacket`, against the archive's `NextSequence`, rather than here:
an archive numbering from the connect would otherwise read an old connection's request as a gap it
must kick for.

Legacy suppression (see [Gating legacy](design.md#gating-legacy)):
- `SuppressesLegacy`: whether the snapshot channel has taken over this update class
  (`SNAPLEGACY_MOTION` 1, `SNAPLEGACY_ANIM` 2, `SNAPLEGACY_EXISTENCE` 3) for an actor. False unless
  deltas are going out; then true for a routed cosmetic at any level, and otherwise only when
  `fua_snap_nolegacy` is at least the class, and never for a player or an actor without a NetID.
- `SuppressesLegacyPlayer`, `LegacyPlayerSkips`: the per-client gate on `MovePlayer` for other
  players, at level 3, and how many client-frames it skipped.
- `SuppressesLegacyLocal`: the gate on a client's own `MoveLocalPlayer` and
  `SetLocalPlayerJumpTics`, at level 3; never for a spectator, who gets no local block.
- `ClientEffect`: an effect actor the server spawns for its clients to draw -- a teleport fog, a
  morph flash, a respawn puff. It frees the NetID the spawn took, so the capture pass cannot mirror
  a three-tic actor, and routes the spawn through `SERVERCOMMANDS_SpawnThing` instead. Nine stock
  files call it, and `TeleportFogs` asks the same of a teleport's pair of fogs from the motion
  class.

Wire accounting:
- `NoteSnapshotQueued`, `ConsumeSnapshotQueued`, `NoteUnreliablePacket`: which datagrams carried a
  snapshot, and their payload against their size on the wire.
- `CountLegacy*` (4): legacy traffic by command, reliable flushes and resends, and the reliable send
  time, for Phase 6's reclassify order.

Capture helpers the per-client pass and the client use:
- `StateFromQState`, `QStateForState`: the qstate encoding against a class (depth and offset). The
  client's mirrors and the replay resolve through the same function.
- `WalkChain`: walks a state chain forward by exactly n tics and returns where it lands, with
  `SNAP_WALK_RNG`, `SNAP_WALK_SCALED`, `SNAP_WALK_HELD` and `SNAP_WALK_CAPPED` saying what it
  crossed. The one walker for everything that reproduces the server's animation advance.
- `AppearDerivable`: whether the client gets from what it was told to what the server holds by
  itself (`SNAP_APPEAR_DERIVABLE`, `SNAP_APPEAR_DIVERGED`, `SNAP_APPEAR_UNWALKABLE`), within
  `SNAP_APPEAR_MAX_TICS`. Takes wire values only.
- `BranchedThisTic`, `BranchCensus`, `BranchReset`: the branch detector's answer per id, its census,
  and its reset.
- `StraightReset`: clears the straightness tracker.
- `PSpriteCapture`, `PlayerReservedId`, `LocalCapture`: the psprite capture, a player's reserved id,
  and one client's local block (`SNAPLOCAL_CAP_OK` or the refusal: `SNAPLOCAL_CAP_NOPAWN`,
  `SNAPLOCAL_CAP_SPECTATOR`, `SNAPLOCAL_CAP_NOCLIENT`).
- `OwnMissile*` (2): the first enter of a player-fired missile to its shooter, and the ring's
  counters.
- `LawGravity`, `LawAt`: the gravity class's acceleration from the class defaults and the sector at
  a record's position, with the compat order. Both ends call `LawAt`.

The property slots:
- `PropSlotRead`, `PropSlots`: how the per-client pass reads the table -- a slot's NetID, its
  `SnapProps` and its generation.
- `PropForget`: the table at a level change.
- `PropStats`: live slots, changes seen, sets refused for want of a slot, material names cut
  to `SNAP_PROPS_MATNAME_MAX`, and the pass's own size and sweep time, for the report.
- `PropsToTell`, `PropsMarkedIds`: a set is told once to each client that holds the entity,
  rather than taking a slot, so the table carries what changes during a life and nothing else.
  The capture keeps one mark an id of what was told -- `ComputePropMark` over the bytes that
  travel, never 0 -- and `ComputePropTellOrSlot` says whether a pump marks a set to tell, takes
  a slot or does nothing: a slot once what the entity would send stops matching the mark, since
  the clients told already have no second telling coming. A return to the class defaults reads
  as a change too, a body naming nothing having a mark of its own. The mark is cleared with the
  id's leave and at `PropForget`, so the next entity on that id is told its own set. The
  per-client walk reads `PropsMarkedIds` -- every live marked entity, told or not, walked in
  full by every client every frame at about 4.5 ns an id, so a few thousand marked ids cost a
  client some 0.3 ms a second; an id cleared by a leave and re-marked before the sweep sits
  in the list twice, which costs one skipped iteration more -- and
  `PropsToTell` reads the live actor at send time, which is what a re-entry and a joiner need.
  It waits on no record of the entity's own, so a settled entity is told as well, and it sends
  only to a client whose `present` is set: that is acked belief, and an event for an id a
  client does not hold is dropped as an orphan. `SNAPPROP_NOSLOT` marks the row, which holds
  no generation, so the repair clears `propsTold` and the walk tells it again.
- `fua_snap_propclasses`: which classes hold the slots, run while the table is full. It reads
  the table as it stands and tallies it with `ComputePropClassCensus` -- most slots first, the
  lower class id breaking a tie -- naming each class, how many slots it holds and which of scale,
  roll, material and uniforms those slots carry. On demand, not kept per window, so a pump pays
  nothing for it; during a fight the table is at its peak anyway. A slot whose entity has gone
  since the capture is counted as `(gone)` rather than skipped, and `PropStats` is read without
  its reset so the report's window is left alone.

Mirror spawns (the client reads these too):
- `IsSpawnerState`, `ClassSpawns`, `OwnsSpecialExpression`: the four spawner actions, a class with a
  spawner state anywhere up its chain (cached by class index), and a class whose parameter
  expression runs a special.
- `CustomMissileNeedsTarget`, `EnterTarget`: whether a state's `A_CustomMissile` needs a target, and
  the shooter NetID the capture holds for a missile's enter.

The module's own files:
- `SnapFilePath`, `SnapFileFind`, `SnapFileNameBad`: a name resolved inside
  `M_GetFuaUserPath()/net-snapshot`, a name found there for a reader, and the check that refuses a
  path separator before anything is resolved ([tools.md](tools.md)).

The byte budgets (`computation/budget_compute.h`):
- `ComputePacketRoom`: the largest snapshot command one datagram may carry at a packet size, its
  overhead `SNAP_PACKET_OVERHEAD` taken off, never under 64.
- `ComputeEffectiveBudget`: the frame's budget across datagrams, `fua_snap_maxbytes` with a floor of
  64.
- `ComputeChunkLimit`: one datagram's share of the budget, the packet room. Only the budget may span
  datagrams: the assembler measures the uncompressed command, so a chunk past the packet size trips
  its guard.
- `ComputeChunkCuts`: the record that opens each chunk, cut at `chunkBytes - 1` over estimated
  lengths.
- `ComputeCosBankRefill`: the bank after a pump, refilled per tic simulated and capped at `max(4,
  tics)` tics' worth.
- `ComputeCosPickCmp`, `ComputeCosFit`: nearest first, a carried event winning ties (it ages out
  sooner), and the prefix of those that fit, so a nearer event is never passed over for a farther,
  smaller one.
- `ComputeCosOverflow`: past the fit, each pick carried one pump while one datagram's room lasts,
  and dropped if already carried or if it does not fit; the walk goes on past a drop, so a smaller
  pick behind it may still be carried.
- `ComputeCosAppends`: whether a cosmetic events command fits in the datagram carrying the last
  chunk.

The motion laws (`computation/law_compute.h`):
- `ComputeGravityAccel`: `AActor::GetGravity`'s arithmetic over the level's, sector's and class's
  gravity, truncated as `GetGravity` returns it.
- `ComputeRunStep`: one tic of an id's runs: the straight run grows while the heading holds within
  `SNAP_STRAIGHT_TOL` and velz within `SNAP_STRAIGHT_ZTOL`, the gravity run while velz's second
  difference stays within `SNAP_GRAV_ZTOL`; a run of `SNAP_STRAIGHT_MIN` is trusted. The first
  moving tic starts the straight run and leaves the gravity run and the velz change as they were.

Cosmetic classes and events (see [Cosmetic actors](design.md#63-cosmetic-actors)):
- `NoteSpawnNetID`: a NetID handed out, counted by what spawned it.
- `NoteCosmetic*` (3): a spawn before its NetID (true when it gets none, at mode 2), the NetID after
  it, and each entity record the snapshot emitted for it.
- `NoteFirstTick*` (2): the copy's first tick, where its event is flushed or its hold starts, and
  the end of that tick.
- `NoteScript*` (3): a server script started with the copy as activator, stopped, or changing the
  copy's state or properties; the hold follows them.
- `AcsSpawnScope`, `NoteAcsSpawn`, `NoteMapThing`: ACS spawns and map things.
- `NoteBloodSpawn`, `NoteRipperBlood`, `NoteBloodEvent`, `NotePuff`: blood, splatter and puffs;
  `NoteBloodEvent` is true when the event replaces stock's command.
- `CosmeticRouted`, `CosmeticMutesSound`: at mode 2, no legacy command may describe a routed copy,
  and its own action sounds are its client copy's to play.
- `ForgetCosmetic`: the copy is destroyed.
- `CatchUpHoldsSound`: declared here and defined in `ghostactor.cpp`. While a client copy is ticked
  forward by its age, a repeat of the same `CHAN_AUTO` sound from it does not start again.

Snapshot sounds (see [Sounds and the derivation
contract](design.md#61-sounds-and-the-derivation-contract)):
- `BroadcastActorSound`: asked by `S_Sound` about every actor sound, flagged for the clients or not,
  since a mirror runs no actions; false when the caller should fall back to the legacy broadcast.
  For an entity whose legacy replication is suppressed it sends per client, culled by rolloff. A
  sound id goes reliable, so it cannot arrive before the entity it names: attached once the client
  has believed the entity for `fua_snap_sound_entergrace` frames, and before that positional as
  `SVC2_SNAPSOUND` through the shared codec (`computation/sound_compute.h`), carrying the NetID for
  a client matched on the sound table, which moves it onto the mirror once one exists.
- `SoundUnplayable`: a sound no client can play (no sound, or no volume), counted and skipped.
- `GhostSoundHandled`, `SendPlayerStopSound`: whether a mirror's own copy has taken a sound, and a
  player's channel stop for the gated client.
- `SoundTableHash`, `TakeSoundTable`, `SendSoundTable`, `ClientSoundIds`: the sound table check: the
  hash of the first N names, taken once per map, sent after `SnapJoin`, and each client's answer
  from its ack.

Mover capture (see [Movers](design.md#71-movers)):
- `MoverCapture*` (8): one per class, called by the thinkers through `movers.cpp` and `polyobj.cpp`
  at both ends of `Tick`, and the capture counters (`MoverCaptureStats`). Each compares before it
  bumps the slot's `seq`, so sampling every tic and capturing at every transition give the same
  wire.
- `MoverTick`, `MoverReset`: once per pump ahead of the capture, and at a level change.
- `MoverSlotCount`, `MoverSlotRead`: how the per-client pass reads the table.
- `NewMoverGen`: a fresh generation, never 0, from the one counter every slot draws on.
- `MoverPolyStats`: polyobject slots reclaimed, entries over the body cap, rotator re-anchors.
- `MoversOn`, `MoverGate`, `MoverServerStops`, `ServerMoverGateStats`: the capture's gate, the
  legacy mover gate latched once a pump, the call in each stopped legacy mover command, and what it
  stopped. `ServerGatesLegacy` ([movers.md](movers.md)) exposes the same latch to the mover code.
- `MoverSend*` (6), one a class: the sounds a mover's own legacy command used to
  carry, sent by sector so a gated client still hears them (`SVC2_DOORSOUND`, `SVC2_FLOORSOUND`,
  `SVC2_MOVERSOUND`, read in [apply.md](apply.md)); `MoverSoundStats` and `MoverSoundStatsByKind`
  count them against the legacy path, and `SendMoverGateTo` tells one client the latch's state at
  its join.
- `TracePlaneWrite`: a plane write from outside a mover thinker, behind `fua_csnap_planetrace`.

## Settings and commands

Master switches, server only: `fua_snap` (the capture), `fua_snap_send` (send deltas) and
`fua_snap_nolegacy` (suppression level 0-3; test servers run 3, and 0-2 are retired in Phase 6).
The legacy mover gate also needs `fua_snap_movers`. Each level stops what the one below does and
more, by the class each command in `sv_commands.cpp` tests: 1 motion (the MoveThing commands and
SetThingAngle), 2 animation (SetThingFrame, SetThingState, deaths and missile explosions), 3
existence and the other per-actor commands (spawns, DestroyThing, the SetThing property commands).
At 1 and 2 the client keeps a real actor that has stopped moving, a marker of where the entity was;
at 3 the mirror is the only one.

Operating parameters (archived): `fua_snap_maxbytes` (per-frame payload, uncompressed, across
datagrams), `fua_snap_cull_dist`, `fua_snap_bandreserve` (the share reserved for the bands below
d0), `fua_snap_sound_entergrace`, `fua_snap_soundevents` (snapshot sound events at all),
`fua_snap_nonetid_classes`, `fua_snap_cosmetic_force` and `fua_snap_cosmetic_never`,
`fua_snap_movers`, `fua_snap_maxcatchuptics` (8: the tics one pump may run before the rest
are dropped; 0 uncapped).

Unarchived, so a restart returns them to their defaults: `fua_snap_cull_fov`,
`fua_snap_missile_dist`, `fua_snap_entergrade`, `fua_snap_soundids`, `fua_snap_zstd_level` (the
world diff's compression, for the recorder and the wire-cost line only), `fua_snap_cosmetic_events`
(0 off, 1 report, 2 act), `fua_snap_cosmetic_dist`, `fua_snap_cosmetic_bytes`.

Reports and tools: `fua_snap_stats` (report every N seconds, 0 off: the server's to the log file
alone, the console saying once where it goes, or that no log file is open or `msg` above 0 drops it;
a client's own to its console), `fua_snap_cosmetics` with an optional filter (the classified
cosmetic set), `fua_snap_record` with a file name and `fua_snap_stoprecord` (the `.zsnap` recorder,
server only), `fua_snap_planedump` (no argument, all, or a sector index), `fua_snap_sectorcheck`
with a tag, `fua_snap_soundcount`, `fua_snap_polycheck`, `fua_csnap_planetrace` (client only; a
sector index, -1 for every write) and `fua_snap_linkcensus` (the link graph at each join).
`fua_snap_planedump` walks `sectors[]` by index, since a linked lift is driven from a control sector
that is not the one on screen; run it on the server and each client at the same moment and diff the
output. `d` is the authoritative plane, so a sector at another `d` on one machine is the divergence;
`texz+d` (0 for an unsloped plane) and `vbo` (VALID when the flat is drawn from the cached buffer)
are local consistency checks; a sector with 3D midtextures adds the first attached line's mid y
offset per plane. `fua_csnap_planetrace` is keyed on the index too, since a control sector's tag
differs from those of the sectors it drives, and prints a signed delta per write with no threshold.

Reproducers: `fua_snap_debug_ackdelay` holds each client's acks N frames, `fua_snap_debug_loss`
drops N% of snapshot datagrams, `fua_snap_debug_holdback` holds N% of them back and sends each
`fua_snap_debug_holdbackgap` datagrams later (1 to 64; 12 by default, past the reorder window and
inside the ack history, a datagram arriving after it was declared lost; past 32, beyond the
history), and `fua_snap_debug_resetsplit` delays a reset's `DestroyAllSectorMovers` by N tics.
`fua_snap_debug_presencelog`, a number of seconds, appends at that interval each client's frame and
the ids it may hold to `snappresence-sv.txt` in the module's folder (`PresenceLog`), for
`tools/snapshot/presence_diff.py`. `fua_snap_debug_refcheck` (in `clienttrack.cpp`) checks every
client's references after every ack and prints `[snap] reference check`.

## Rules that must hold

- **The gravity law stays `GetGravity`'s.** The server checks the law against what its missile
  actually did, so `ComputeGravityAccel` changes only with `AActor::GetGravity`.
- **Due west is not "not moving".** `ComputeRunStep` scales `atan2` with a shortened pi, which puts
  due west at 65534 rather than on the 0xFFFF that means no heading; the run step's test pins it.
- **The packet overhead counts the snapshot command's header.** `SNAP_PACKET_OVERHEAD` (28) is
  `SVC_UNRELIABLEPACKET` (1), the extended command and `SVC2_SNAPSHOT` (2), the header's frame,
  game tic and datagram sequence (longs), map stamp, reset count and ack lag (bytes), newest
  reliable sequence and payload size (shorts), 19 in all, and the 5 bytes of slack
  `SERVER_CheckClientBuffer` keeps, rounded up from 27: a byte over makes a datagram larger than
  the operator asked for. A header change changes it.
- **The chunk cut's bound is the id encoding's.** `ComputeChunkCuts` cuts at `chunkBytes - 1`
  because a record opening a chunk pays a raw id, at most one byte over its estimated gap id
  (`format.h`); a change to the id encoding changes that byte.
- **Every per-client lifecycle call is wired.** `ClientConnected`, `ClientDisconnected` and
  `MapChanged` reset what a slot or a map's ids mean; a missed one is a correctness failure, not a
  leak.
- **Suppress only what a snapshot record replaces.** No NetID, a player, or capture without sending
  means the legacy command still goes out.
- **An effect actor the clients draw themselves keeps no NetID.** `ClientEffect` frees it before the
  capture pass can see it, since the pass takes every actor with a NetID and a three-tic fog would
  enter and leave in one breath.
- **A set is told once a client, and only a second change takes a slot.** The telling waits on
  no record of the entity's own and goes only to a client that has acked holding it, so nothing
  is dropped as an orphan and a lost telling is repaired by telling again. An older client
  reads the same event it always did.
- **A property slot is marked by the capture pass and swept at its end**, and its generation comes
  from one counter that never repeats and never reaches 0, so a reused slot cannot read as the
  previous occupant's set.
- **A file name the module writes carries no path separator**, and every path is resolved inside the
  module's own folder, so a command cannot be made to write elsewhere.
- **The report never stalls a tic.** Every line goes through the queue, one line per pump:
  `snap_Say` for a report line, printed at `PRINT_LOW` to the log file alone, and `snap_Warn` for a
  line the operator must see on the console too. A report due while the last is still draining is
  skipped and counted. Anything printing from inside the tic loop goes through that drain.
- **Count per game tic, not per pump.** Rates divide by the window's simulated tics; under catch-up
  one pump spans several.
- **A print and its reset share one condition**, and a counter read by two lines is read once.
- **One walker, one encoding, one resolver.** Appearance suppression and its census walk with
  `WalkChain`, the capture encodes with `QStateForState`, and both ends resolve with
  `StateFromQState`, the client's mirrors included. Two that disagreed would suppress a record
  nothing then corrects.
- **Mover capture compares before it bumps `seq`, and each class leaves out what its `Tick` changes
  every tic**: the planes, the countdowns, a door's floor distances (`botDist`, `oldFloorDist`), a
  polyobject's position and angle and its actions' speeds. On a plat that is `m_Count`; on a floor,
  the reset, pause and step counters. The constants a countdown reloads from (a plat's `m_Wait`, a
  floor's `m_Delay` and `m_PerStepTime`) are compared, and all of it rides the record, so a client
  joining mid-wait gets the live values.
- **A gone mover slot is held until every started client has acked its GONE**, with a backstop in
  frames for clients that never acked.
- **One recording, one map.** A map change forgets presence without writing leaves, and the next map
  may take another frame than the one the header carries, so `MapChanged` stops the recording and
  the stop line says why (`stopped (map changed)`). For the same reason `fua_snap_record` refuses to
  start with no map loaded. The recorder also stops at `fua_snap_stoprecord` and on a failed write,
  but not at quit, so a recording running when the process ends has a cut-off tail
  ([stream.md](stream.md)).
- **Server cvars are not set from a client.** Both ends register every server setting; set on a
  client it echoes a value and does nothing.

## Engine hooks

Each site carries a `[Kaori-Chani]` line. Twenty-eight stock files call this header:
- `sv_main.cpp`: `SERVER_Tick` calls `ServerTick`; the client connect, disconnect and ack paths; the
  sound table after `SnapJoin` and each ack's answer; the remote `MovePlayer` gate; the unreliable
  send's accounting; the reliable flush and its send time (`CountLegacyFlush`,
  `CountLegacySendRaw`); the mover gate at a join (`SendMoverGateTo`); and the catch-up clamp
  (`CatchUpTics`).
- `sv_commands.cpp`: the legacy suppression tests in the actor and local-player commands
  (`SuppressesLegacy`, `SuppressesLegacyLocal`), `GateOn`, the mover gate's stops, the reset count,
  map stamp and frame on `DestroyAllSectorMovers` and `SnapJoin`.
- `p_mobj.cpp`: spawns (`NoteCosmeticSpawn`, `NoteSpawnNetID`, `NoteCosmeticNetID`), blood, splatter
  and puffs, routed-cosmetic tests, `ClientEffect`, and `ForgetCosmetic` from `AActor::Destroy`.
- `dthinker.cpp`: the first-tick hooks around a thinker's first `Tick`.
- `p_acs.cpp`: script start, stop and change on an activator, the ACS spawn scope, and the
  routed-cosmetic test.
- `p_setup.cpp`: `MapChanged`, `TakeSoundTable`, `ServerMapFrame` and map things.
- `s_sound.cpp`: `BroadcastActorSound`, `SoundUnplayable`, `GhostSoundHandled`,
  `SendPlayerStopSound`, and on the client `CatchUpHoldsSound`.
- `g_game.cpp`: `ResetEnd` at the end of a map reset.
- `p_plats.cpp`, `p_ceiling.cpp`, `p_floor.cpp`, `p_doors.cpp`, `p_pillar.cpp`: `MoverServerStops`
  in the legacy mover commands and the sound sender each one's own sound moved to;
  `po_man.cpp`: `MoverServerStops` alone.
- `p_linkedsectors.cpp`, `cl_main.cpp`: `TracePlaneWrite` on plane writes.
- `p_teleport.cpp`: `TeleportFogs` for a teleport's pair.
- `a_pickups.cpp`, `p_things.cpp`, `p_map.cpp`, `invasion.cpp`, `a_morph.cpp`, `a_bossbrain.cpp`,
  `a_dsparil.cpp`, `a_rebels.cpp`, `thingdef_codeptr.cpp`: `ClientEffect` for an effect the clients
  draw, and `CosmeticRouted` where a legacy command would describe a routed copy.
- `netcommand.cpp`, `packetarchive.cpp`: legacy command and resend accounting.

## Tests

The three units are GoogleTest files under the 100% line gate, and each takes all of its unit's
lines and branches. `computation/law_compute_test.cpp` takes the gravity law at `GetGravity`'s
values, truncation included, and the run step through a stop, a first moving tic, held headings,
turns either way, velz changing, constant acceleration, each ended run's bucket, the caps, the wrap
at west and due west. `computation/budget_compute_test.cpp` takes the packet room, the frame budget
and the chunk limit at their floors, the bank's refill, the pick order and the fit, a capped pump's
overflow, and the chunk cut over fixed and random frames with the events command's append.
`computation/catchup_compute_test.cpp` takes the clamp inside the cap, past it and uncapped, the
run's start, its break on a clean pump and its dilation, and the warning's three conditions -- the
run's length, a level load's grace and the rate limit -- with the millisecond counter wrapping under
it.

No self-test block covers the rest of this file: the capture needs a running server. What checks it:
- the `[snap]` report, whose lines each count what a mechanism did, so "on" and "ran" read apart;
- `fua_snap_record` with `fua_snap_verify` on the result ([selftest.md](selftest.md)), which decodes
  the world diff through the client's decoder, and `tools/snapshot/zsnap_info.py --decode` as an
  independent reader ([tools.md](tools.md));
- `fua_snap_planedump` diffed across server and clients for movers;
- the client's checksum, which tests each record against the state the server believes the client
  holds, and reports DIVERGED when they disagree.
