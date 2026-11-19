# net-snapshot

The snapshot netcode: a server sends each client the world as a diff of entity state per frame over
its own unreliable datagrams, and a client draws mirrors of what it has not simulated. It is a sixth
netcode module beside `net-transport`, `net-protocol`, `net-replication`, `net-client` and
`net-session`.

Ported from the `snapshot` branch of Kaori-Chani/ZanZan, from its `snap-ref-premerge` tag at
`b8a72a262`, where it was written against a 32-bit 16.16 `fixed_t`. The quantizers here are rewritten
for `zx::Fixed`; what that found is in the port's own notes, and the shape to watch for is an
unsigned value widened into a `fixed_t` without saying which sign it carries.

**The format layer and the server layer are here so far**, with every header the rest will need.
The client, the movers and the recorder follow as their own commits, and the hooks that call them
after that. **Nothing calls into this module from stock code yet**, so the game plays exactly as it
did without it.

What is in the engine's build, precisely, because "nothing" was not true:

- The twelve `computation/*_compute.cpp` units are compiled into the game, and have been since they
  landed. `src/CMakeLists.txt` globs `*_compute.cpp` recursively so a new computation needs no CMake
  edit, and that glob reaches here too. They register no classes and nothing calls them, so they are
  dead weight in the binary until the layers arrive, not behaviour.
- `capture.cpp` and `clienttrack.cpp` are compiled by the `netsnapshot_layers` object library and
  **not linked into the game**. The layers call each other, so none of them links until all of them
  are here; compiling each as it lands is what gives it a compiler's opinion rather than a reading.
  Step 3 links what has already compiled.
- The format layer is also reached by the test target, which is what holds it to the references.

## Owns

No vendored file. The module is its own folder, and each hook in a stock file is one call into it
with any role test or send behind that call, so no stock file's crossings grow and
`tools/boundary-allow.json` needs no raised allowance. The module's README joins the `owners` of the
netcode rules in `tools/boundary-rules.json` when those hooks land, not before.

## Public header

`format.h` is the wire: every record and event, the position frame, the quantizers and the
checksum. `stream.h` reads a `.zsnap`, the server's recording, whose records are zstd frames.
`record.h` is the `.csnap`, the client's recording, with its reader here and its writer to come.

## Files

- `format.h`: the format itself, header-only. Positions are quantized into a frame per map (an
  origin in whole units and a width per axis), so a map past 65,535 units a side sends two or three
  bytes an axis instead of wrapping. Its own wire stays 32-bit on purpose: a 48.16 `fixed_t` is read
  through `ReadFixed32`, which sign-extends, and mover planes, distances and polyobject speeds stay
  absolute 32-bit 16.16 like ForkUnderA's own sector commands.
- `stream.{h,cpp}`: the `.zsnap` container's reader, which needs the vendored zstd.
- `record.h`, `recordread.cpp`: the `.csnap` container and its engine-free reader, which the fuzzer
  and the replay both read through.
- `fuzzgen.h`: the generator the fuzzer builds inputs with, shared with the in-engine fuzzer to come.
- `capture.{h,cpp}`: the server's snapshot tick — the world capture and its censuses, legacy
  suppression, mover capture, cosmetic events, snapshot sounds and the stats report. Was
  `sv_snapshot`.
- `clienttrack.{h,cpp}`: the per-client delta — what each client has been told, its ack ledger and
  the interest scheduling that decides what its next datagram carries. Was `sv_snapclient`.
- `computation/`: twelve pure units — the ack ledger, the animation census, the budget and chunk
  cuts, the clock model, the interest tiers, the join's frame and map stamp, the gravity law and run
  steps, the mover write order, the replay's histogram, the ghost sampler, the smoothing, and the
  wall sweep — each with its colocated `*_test.cpp`.
  - `ComputeAnchorStart`, in the smoothing unit, is the mirror interpolation rule's start pose, and
    both the live frame and the replay's analysis call it so that `fua_csnap_analyze ... anchor`
    measures what a client actually draws. Two things about it are deliberate and easy to undo by
    accident. It takes the offset from the end and rounds it, rather than dividing the pose as
    `(h - f*x) / (1 - f)`: the truncated form drew a resting monster a unit low under this build's
    floating-point settings, because a fold that did not move has to come back on the end exactly,
    not near it. And `here` arrives in raw units, as the sampler leaves it, with `frac` the same
    `fixed_t` both callers already hold, so neither converts before the division — a comparison
    against the reference tag must not be able to fail on a conversion alone.
- `ghostactor.{h,cpp}`: the client's mirrors — the ghost table sampled into actors the render
  copy carries, their skins and states, the legacy copies they stand in front of, and the
  mirror interpolation rule's live half. Was `cl_ghostactor`.
  - The far rotor refreshes one slice of distant mirrors at a time, and its phase is keyed
    on the game tic rather than advanced per call, because this fork's render copy refills
    on a tic *or* a packet: advancing per call moved the phase twice in a tic that carried
    two packets, leaving a far mirror's turn to the traffic rather than to the clock. The
    phase count is two, not the reference's eight, because the reference advanced per drawn
    frame at around 140 fps and so refreshed a far mirror about 18 times a second; eight
    phases off a 35 Hz tic clock would give 4.4, where two give 17.5.
- `localpred.{h,cpp}`: the local player's block read against the prediction it replaces —
  the shadow of each block, the comparison at the slot the replay is about to read, the
  reconciliation error and correction arrival censuses, and their report. `cl_pred.cpp` and
  `cl_main.cpp` hold only one-line calls into it.
  - `CLIENT_ApplyLocalBlock` lives in `cl_main.cpp`, beside the command it replaces, because
    two of the side effects it reproduces read file statics there (`g_ulFirstSpawnedTic`,
    `g_ulEndFullUpdateTic`). Its ordering guard is a live case rather than a formality:
    `ApplyDelta` applies a reordered payload's records rather than dropping them, keeping
    only its own clock monotonic, so a block can arrive behind the latest server gametic.
    It has no spectator branch of the kind the command above has, because the server
    excludes spectators from the capture exactly as `sv_main.cpp` excludes them from
    `MoveLocalPlayer`: a block for a spectator is never sent, and the refusal is counted
    rather than ignored because "never sent" is a claim this client can check.
  - The applier reaches `CLIENT_PREDICT_SetPosition`, the same setter the legacy periodic
    correction reaches, so the module ignores its own write while the applier runs. The
    reference distinguished the two with a `bPeriodic` argument on that stock setter; the
    port keeps the stock signature and raises a module flag instead, which also keeps a
    slot from being left marked after an authoritative block.
- `movers.{h,cpp}` also carries `SnapSpecialState`, the join's scroller and pusher state. The
  reference put a control scroller's `lastHeight`, an accelerative one's `vdx`/`vdy` and a point
  pusher's `x`/`y`/`radius` into the legacy `DoScroller` and `DoPusher` commands. This port must
  leave the gate-off fallback byte-identical, so the same state rides a command of its own, sent
  only while the gate is on, at the end of `SERVER_UpdateMovers` after both of its loops.
  - A record names its mover by **ordinal within the join**, with its `type` and `affectee` as an
    echo. That works because the client destroys every scroller and pusher at `BeginSnapshot`
    (`SpecialClearForFullUpdate`) and rebuilds them from the server's stream alone, and because
    thinkers are appended (`FThinkerList::AddTail`) while `FThinkerIterator` walks a stat's list
    and then its fresh list, so iteration order is creation order. A refused `DoScroller` would
    shift every later ordinal, which is what the echo catches: on a mismatch the module takes the
    first unclaimed mover with that echo instead, and counts both. A record that matches nothing
    leaves its mover at zero, as the gate-off fallback does.
  - The arrays are chunked at 48 records so a command fits `sv_maxpacketsize`, and because the
    generated array length is a byte. Each chunk carries the join ordinal it starts at.
  - **A known limit of the gate-off fallback**: with the gate off, a mid-game joiner still builds
    control and accelerative scrollers, and point pushers, from zero, exactly as stock does. The
    fix reaches snapshot clients only, until legacy is removed.
- `movers.{h,cpp}` also carries the reset count, which decides what a mover record arriving
  around a map reset is worth. Two counts, the two sides of one comparison: the datagram's,
  stamped on every snapshot (`MoverBeginDatagram`), says which generation the records describe,
  and the client's own, set by `MoverSetResetCount`, says which reset it has carried out. The
  sink subtracts them as a signed byte: behind is dropped, ahead is held, equal applies.
  - The client's side needs a count at the moment its reset window closes, and the legacy
    `DestroyAllSectorMovers` carries no fields. The reference added one to it; this port sends
    `SnapMapReset` instead, gate-on only, from `snap_SendResetEnd()` straight after that command
    on the same reliable channel — after it, because the count releases held records and nothing
    must be left to destroy them.
  - The datagram's count cannot stand in for it. Feeding one count into both sides makes the
    difference always zero and removes the hold, which exists because a datagram can overtake
    the reliable destroy; and with no count at the close the client's side never advances, so
    every later record reads ahead and the hold is cleared only by a stream epoch change — the
    planes freeze until a reconnect. Borrowing the last datagram's count at the close is wrong
    whenever none has arrived yet, which `fua_snap_debug_resetsplit` widens deliberately.
  - `MoverResetBegin` returns at its first test unless this client knows the server's
    generation, which only a gate-on join gives it, so with the gate off the legacy destroy is
    the whole reset exactly as it is today.
- `format_test.cpp`: the format layer's own tests, narrow frame and wide.
- `refdump_test.cpp`: the reference decode, below. The dumper it decodes with is shared, in
  `tests/snapdump/`, with the `snap_dump` command line beside it.

## In-engine hooks

Each hook is one call from a stock file into the module, listed here as it lands.

- `sv_main.cpp`, end of `SERVER_UpdateMovers`: `SpecialSendJoinState( ulClient )`, after the
  scroller and pusher loops, so every mover its records name has already been built on the
  client. The role and gate tests are inside the module.
- `cl_main.cpp`, `ServerCommands::SnapSpecialState::Execute`: `SpecialApplyJoinState( ... )`,
  which hands over the command's four members because they are protected.
- `cl_main.cpp`, `ServerCommands::DoScroller::Execute` and `client_DoPusher`:
  `SpecialCountScroller( ... )` and `SpecialCountPointPusher( ... )`, the census of what the
  legacy commands did build, which the applied-record counts are read against.
- `p_spec.cpp`, `DPusher::Tick`: `SpecialResolveSource( m_SourceNetID, m_bSourceSeen )`, inside
  the point branch only, for a source actor that had not arrived when the join named it. With no
  record applied the id is zero and the module returns at its first test.
- `cl_main.cpp`, the seven legacy mover handlers — `client_DoPlat`, `client_DoFloor`,
  `client_BuildStair`, `client_DoCeiling`, `client_DoDoor`, `client_DoElevator`,
  `client_DoPillar`: `MoverMadeByLegacy( pMover )` on what each builds, so a mover gate-on
  drops them and the records rebuild them rather than both owning the plane. The flag lives on
  `DMover` and only `snapmv_GateDrop` reads it, which runs only when the gate changes, so with
  the gate off the mark is never consulted.
  - The timed-door spawners are deliberately not marked: records adopt a client's own spawned
    door, and marking it would have the join's gate word destroy that door moments after it
    was built. Waggles are not marked either — they are not a record class, so `GateDrop`
    skips them and nothing would rebuild one.
- `g_game.cpp`, `G_ResetMap`'s client branch: `MoverResetBegin( )`, ahead of the server's reset
  writes, so planes the module owns take them; the test for whether it owns any is inside.
- `p_plats.cpp`, `p_pillar.cpp`, `p_floor.cpp`, `p_ceiling.cpp` and `p_doors.cpp`, each
  mover's `Tick` and `Destroy`: `SnapCapture( )` at both
  ends of the tic and `SnapCapture( true )` in `Destroy`, which is the only hook that catches a
  mover destroyed outside `Tick` — the map reset's loop does exactly that. Beside them
  `MoverHoldsPlane( )` (and `MoverHoldsFloor`/`MoverHoldsCeiling` for a pillar's two planes),
  `MoverPlaneStalled( )`/`MoverPlanesStalled( )`, `MoverServerStops( )` around each legacy plane
  send, the crush and plat-wait counters, and the client-side predicates:
  `MoverClientSkipsWait( m_SnapSeen )` on a door's and a plat's countdown,
  `MoverPlaysWaitSound( )` on the door's and the plat's local wait-end sounds, and
  `MoverClientDerivesReversal( m_SnapSeen )` on a crusher's turn.
  - The reference wrote the role tests into these files — `NETWORK_InClientMode( ) && m_SnapBlocked`,
    and a server test around the stall write. This port keeps them inside the module, as
    `SpecialResolveSource` does, so the stock sites stay at one call and the boundary counts do
    not move. The stall flag is therefore passed and returned by value rather than assigned under
    a role test.
  - An elevator holds as one mover, not per plane: `SnapMoverElevator` carries no blocked
    field, only the capture call's single flag, where `SnapMoverPillar` carries a `BYTE
    blocked`. That follows the movers themselves — an elevator drives both planes with one
    speed and reverses one when the other is crushed, while a pillar drives its two toward
    separate targets and can stall on one, which `MoverCountHeldOnePlane` counts. Its stall is
    written straight after its moves rather than at the tail, which the `pastdest` branch can
    reach with the elevator already destroyed.
  - A door's wait is the one client-side change taken here, because it is gate-off-identical by
    construction: `MoverClientSkipsWait` answers true for every client until a record has
    reached that door, which with the gate off never happens, so the stock break still takes
    them all. The test lives in the module, so `p_doors.cpp` holds one fewer role crossing.
  - A crusher's reversal is derived on the same terms. The reference derives it on every client,
    which would have a gate-off client turn at its own top or bottom and then be snapped back by
    the server's plane set from that same moment — not legacy behaviour. Here
    `MoverClientDerivesReversal( m_SnapSeen )` restricts it to a ceiling a record has reached, so
    gate off derives none, measured at 0 with records flowing and the mover gate off. It sets
    `m_Direction` and `m_Speed` only: at nolegacy 3 the server still sends every
    `SVC_PLAYCEILINGSOUND`, and `SN_StartSequence` stops the sector's existing ceiling sound
    first, so a local `PlayCeilingSound()` would restart the sound rather than layer it. The call
    belongs here again if the sound unit later stops the server sending it under the gate.
  - A gated client still spawns its own timed doors, and the records adopt them: measured on a
    join, where the flag is false at spawn time anyway, and the same on a map change. The
    reference suppressed the spawn instead, which a stale flag can turn into a door missing for
    a whole map when the gate goes off across a map change — the fallback case the gate exists
    for. `MoverSuppressesLegacy` keeps its inside role test for its other callers.
  - `DWaggleBase::EndPlane` is the write `DoWaggle` makes when a waggle ends, named so that a
    client ending one by record can make the same write. It takes its plane from the class
    rather than an argument, since a caller outside `DoWaggle` has no such flag, and it is
    otherwise the old write exactly — including the `P_ChangeSector` call on a plane already at
    `m_OriginalDist`, which is not a no-op: that walks the sector's touching things through
    `PIT_FloorRaise`/`PIT_CeilingRaise` and recalculates the 3D floors. A caller that wants to
    skip a waggle already home tests that itself.
  - The test map is driven in **cooperative** for door and ceiling measurements. In survival the
    level does not tick until a round starts, and that round's own reset destroys the map's
    door, so a survival window catches the reset rather than the mover.
  - A plat's wait is the door's case again. The reference drops the stock
    `NETWORK_InClientMode( )` break from the `waiting` case outright, which would change
    gate-off client behaviour, so here `MoverClientSkipsWait( m_SnapSeen )` takes the break for
    every client until a record has reached that plat. `MoverPlaysWaitSound( )` guards both
    local sound calls in the countdown block, since the server replicates them. The waiting-tic
    counter sits below the break, as the door's is, so it counts only a plat whose record set
    `m_Count`.
  - `DPlat::DPlat( sector_t * )` initialises `m_Count` and `m_OldStatus`, which nothing else
    sets before they are read. `DPlat::Tick` assigns `m_Count` from `m_Wait` on reaching an end,
    and neither `EV_DoPlat` nor `client_DoPlat` sets it: `client_DoPlat`'s `SetDelay( 0 )` writes
    `m_Wait`. `m_OldStatus` is written only by `DPlat::Stop` and the `platToggle` branch, so on
    most plats nothing ever writes it. The client's wait, `SnapApply`'s wait-ended census and the
    capture's `oldStatus` byte all read memory nobody set. Stock behaviour is unchanged, stock
    code writing both fields before reading them, and the deserialising `DPlat::DPlat( )` taking
    them straight from the archive.
  - While the legacy plat handlers are live the client-run wait is inert, and deliberately so:
    the client's countdown starts at or below the server's count, the record carrying what the
    server had when it was sent, so on a clean link `SVC_CHANGEPLATSTATUS` ends the wait first
    and `plat waits: ... ran out` reads 0. A delayed status command lets the client end the wait
    itself, which the plat suppression will rely on. What the countdown does run is visible in
    the client's `m_Count`: against HEAD it sits on a plateau between records, here it falls
    every tic.
- `cl_main.cpp`, `client_DoDoor`, `client_DestroyDoor`, `client_ChangeDoorDirection`:
  `MoverLegacyStandsAside( SNAPMV_DOOR )`, which is `MoverSuppressesLegacy` counting the call it
  turns away, each after that handler's last read so the stream is consumed either way. The first
  of the handler classes; the sounds are not among them, `SVC2_DOORSOUND` carrying them to
  `OnDoorSound`, which plays them only while these three are suppressed. Suppression is
  client-side alone, so the server's sends are unchanged. What ends a client's wait moves from
  the command to the record, measured as `door waits: 1 early (worst 17)` where the same window
  at HEAD counts none: the door's dirty compare leaves `topCountdown` out, so no record goes out
  merely because it ticked and the client's countdown is stale by however long since the last
  one. `ran out` therefore stays 0 on a clean link here too, the record reaching the door before
  its own counter does.
  - That sound had no sender here. The enum, `OnDoorSound` and `P_DoorSoundSector` were ported,
    but nothing sent `SVC2_DOORSOUND`, measured as none of them on the wire in six windows.
    Suppressing `client_ChangeDoorDirection` takes away the client's only wait-end sound, and it
    does not reach its own, the record ending the wait first, so this class needs the sender to
    behave at all. `DDoor::DoorSound` ends with `MoverSendDoorSound( )`, which sends only on a
    server with the legacy mover gate on, so a `nolegacy 0` server sends nothing for
    `OnDoorSound` to discard. The module holds the send, leaving `p_doors.cpp` no replication
    crossing to allow, and the sender is written against `OnDoorSound`'s own hand parse, so
    there is no protocolspec entry and the golden does not move.
- `cl_main.cpp`, `client_DoFloor`, `client_DestroyFloor`, `client_ChangeFloorDirection`,
  `client_ChangeFloorType`, `client_ChangeFloorDestDist`, `client_BuildStair`:
  `MoverLegacyStandsAside( SNAPMV_FLOOR )`, each after that handler's last read. `client_DoFloor`
  and `client_BuildStair` cannot carry `m_OrgDist`, which the record does under `SNAPMVF_STAIR`.
  `client_DestroyFloor` also stops the sector's sound sequence, which the server sends
  `StopSectorSequence` for beside the destroy, so that side is covered either way.
- `sv_commands.cpp`, the same six senders: `MoverServerStops( SNAPMV_FLOOR )` opening each one. The
  reference carries this in all 28 of its mover state senders and this port had none of them, so
  until here a gated server still sent every state command for the client to ignore. The join's
  resend goes through the same senders, so a client connecting mid-cycle gets the floor from records
  alone; the gate-off resend still goes out, because `snap_MoverGatePump` clears `g_moverGate`
  before `SERVER_ResendTrackedMovers` runs and `MoverServerStops` reads that same latched flag.
  - Once a class's senders stop, `legacy calls suppressed` goes quiet for it: the commands never
    arrive, so the client's handlers are never reached and have nothing to turn away. It read 6
    with only the client half in and 4 -- the door's -- with both. The byte table is the evidence
    for a class from here on, not that counter.
- `sv_commands.cpp`, `SERVERCOMMANDS_DoDoor`, `DestroyDoor`, `ChangeDoorDirection`:
  `MoverServerStops( SNAPMV_DOOR )` opening each one, which is what the door class left undone --
  its handlers stood aside while a gated server still sent all three for them to turn away. With
  the floor already quiet, `legacy calls suppressed` now reads 0 across a `nolegacy 3` window,
  which retires it as a class's evidence and leaves it a tripwire: non-zero there means a sender
  has escaped its guard. `SVC2_DOORSOUND` keeps flowing, being an event.
  - A guard inside an existing sender cannot move the recipients, which is the hazard the floor
    sound's helper had: `DDoor::UpdateToClient` still passes its own client and
    `SVCF_ONLYTHISCLIENT` to `SERVERCOMMANDS_DoDoor`, and the guard only decides whether that
    send happens. A moved send is what needs its audience checked, not a gated one.
  - Plane agreement is the checksum's job, not a sampled comparison. `fua_snap_sectorcheck`
    costs a bridge round trip per side, so sampling a moving plane on server and clients reads
    three different instants on three processes, and a progressive gap appears that says nothing
    about the mover. What a join window can show is that the joiner was sent nothing and ran the
    door anyway: at `nolegacy 3`, with no door command in the capture at all, a client connecting
    mid-close saw its ceiling descend across every sample and verified its planes with none
    diverged.
- `p_floor.cpp`, `DFloor::UpdateToClient` and the four `EV_` creation sites:
  `MoverSendFloorSound( sector, floorID, client, flags )`, which sends exactly one command --
  `SVC2_FLOORSOUND` keyed on the sector while the gate is on, and the legacy ID-keyed
  `SVC_STARTFLOORSOUND` otherwise, unchanged. `OnFloorSound` plays it only under
  `MoverSuppressesLegacy( SNAPMV_FLOOR )`, through `P_FloorSoundSector`, which is the old
  file-static `StartFloorSound( sector_t * )` exported; `DFloor` and `DElevator` call it as they
  always did. The five sends leaving drops `p_floor.cpp` to 27 replication crossings.
  - The sound is an event, not one of the 28, so it goes out under the gate while the client's
    floor is still waiting on a record. It cannot be keyed on the floor: the reliable buffer is
    sent before the unreliable one carrying the snapshot (`SERVER_SendOutPackets`), so the sound
    lands first and `P_GetFloorByID` finds nothing. Measured at nolegacy 3 on the commit before
    this one, five floor starts and five `client_StartFloorSound: Couldn't find floor`, against
    none at nolegacy 0; the sector-keyed command needs no mover, and the same window here sends
    four and logs none.
  - What that costs is a silent floor, not just a failed lookup. `fua_snap_sectorcheck` now
    reports `SN_CheckSequence` on both of a sector's channels: `SN_StartSequence` makes its
    `DSeqSectorNode` whatever the sound device is, so the node is there under `-nosound` exactly
    when the sound would be playing. Sampled through one movement at nolegacy 3, the server's
    floor channel plays and the client's reads `none` for the whole travel with the ID-keyed
    send, and plays, a sample behind the server, with the sector-keyed one.
  - `cl_showwarnings` is off by default and `CLIENT_PrintWarning` is a no-op without it, so a
    window that does not set it says nothing about a missing lookup. Three runs of mine said
    nothing before this was noticed, which is what the sequence probe is for: it answers whether
    the sound played, where the warning only answers whether one lookup failed.
  - A joiner has the gate word before any of this: `SendMoverGateTo` is at `sv_main.cpp:1573`
    and `SERVER_UpdateMovers` at 1725, so `OnFloorSound`'s test is already true for it.
- `cl_main.cpp`, `client_DoCeiling`, `client_DestroyCeiling`, `client_ChangeCeilingDirection`,
  `client_ChangeCeilingSpeed`, and the same four senders in `sv_commands.cpp`:
  `MoverLegacyStandsAside( SNAPMV_CEILING )` after each handler's last read, and
  `MoverServerStops( SNAPMV_CEILING )` opening each sender. `client_DestroyCeiling` also stops
  the sector's sequence, which the server's `StopSectorSequence` covers beside the destroy.
  - The crush slowdown is why `ChangeCeilingSpeed` mattered: `DCeiling::Tick` drops `m_Speed` to
    `FRACUNIT/8` while crushing, and that command was its only carrier. The record's `speed`
    field carries it instead, measured identically on both trees, and the command's own traffic
    goes from 399 sends in one window to none -- a crushing server re-sends the speed every tic.
  - `Ceiling_CrushAndRaise` cannot exercise that: it passes `SPEED(arg1)` and `SPEED(arg1)/2`,
    while the slowdown tests for both speeds at `FRACUNIT`. `Ceiling_LowerAndCrush` passes the
    same speed twice, so MAP34 carries one at speed 8 on tag 3, the start room, where the player
    can cross its line and step back under the ceiling. A Doom crusher damages and keeps going
    rather than stalling, so the proof of a crush is `FRACUNIT/8` in the record, not a held plane.
- `p_ceiling.cpp`, the four `PlayCeilingSound` sites: `MoverSendCeilingSound( sector, id, silent )`,
  which sends `SVC2_MOVERSOUND` keyed on the sector while the gate is on and the legacy
  `SVC_PLAYCEILINGSOUND` otherwise. All four broadcast and none is in an `UpdateToClient`, so the
  legacy branch takes no client or flags. `OnMoverSound` plays it under
  `MoverSuppressesLegacy( SNAPMV_CEILING )` through `P_CeilingSoundSector( sec, silent )`, which
  is `DCeiling::PlayCeilingSound`'s body with its two reads as arguments.
  - One command for both classes that need one, `SNAPSND_PACK` putting the class's own choice in
    the low two bits and the kind above them, as the door sound packs its own. The plat's kind is
    defined here and wired in that class, so there is one command rather than two.
  - Keyed on the mover, this sound misses: a gated client has no ceiling until a record makes one,
    and the reliable buffer goes out before the unreliable one carrying the snapshot. Measured at
    `nolegacy 3`, two of seventeen sounds found no ceiling before this, and none after, with the
    client's `CHAN_CEILING` sounding in every sample of a later window.
- A window that wants the server's own counters needs `fua_snap_stats 1` on it: the whole
  `[snap]` block, the `legacy movers` gate line among it, is behind that cvar and an interval.
  Without it the line never prints and every counter in it reads 0, which looks like a figure.
- `cl_main.cpp`, `ServerCommands::DestroyAllSectorMovers::Execute`: `MoverResetEnd( )`, ahead of
  the stock loop, which is left as it is so the gate-off reset behaves as today.
- `cl_main.cpp`, `ServerCommands::SnapMapReset::Execute`: `MoverSetResetCount( resetCount )`,
  which releases the records held for that reset.

- `cl_main.cpp`, `ServerCommands::SnapMoverGate::Execute`: `MoverSetServerGate( legacyOff, frame )`,
  taken ahead of every legacy mover command under it. The first hook, and the only kind a
  protocol-spec command brings: a spec command is four parts — the `protocolspec` entry, the
  server's sender, this `Execute`, and `tools/data/protocol-snapshot.txt` written again — and the
  `Execute` is where it reaches the module, so no spec command can land before the module links.
  The golden is the part a compiler cannot catch; `tools/protocol-snapshot.py --check` does.

## The reference recordings

Three recordings made on the source tree at its `snap-ref-premerge` tag pin what the format layer
decodes to, byte for byte, through the dumper that wrote their shipped dumps. That dumper is
`snap_dump`'s own body, shared from `tests/snapdump/`: `refdump_test.cpp` holds the three
references to their digests through it, and the `snap_dump` target is a command line over the same
call, so a dump taken by hand is the dump the test takes.

`testdata/ref-wide.csnap` is committed, at 67 KB, and so is checked on every build: it is the only
reference carrying wide frames and non-zero origins, where 48.16 is likeliest to go wrong. The two
large recordings, `ref.zsnap` and `ref.csnap`, are 61 MB with their dumps and stay out of the repo;
set `ZX_SNAP_REFDIR` to the folder holding them and their `.dump.txt` files to run those two:

```
ZX_SNAP_REFDIR=/path/to/references ctest --test-dir build-tests -R SnapshotRefDump
```

Unset, those two report that they did not run, and say that a skip is not a pass. Set but wrong,
they fail: each recording's own SHA-256 is checked before it is decoded, so a damaged or mistaken
file fails as the wrong file rather than as a decode difference. A mismatching dump is written into
the build folder and the first differing line is named.

## The dumper

`snap_dump <recording> [out.txt]` writes any `.zsnap` or `.csnap` as text, telling the container
from the file's own magic. It is the tool that made the shipped reference dumps, and it is how a
new reference dump is made after a deliberate format change, or a recording read when a later
layer goes wrong. Its body is `tests/snapdump/snapdump.cpp`, which the reference test calls too;
the standalone `formattest` tree it came from is not ported, since step 1 replaced all three of
its jobs inside this build. Unlike the fuzzer it needs no sanitizer, so it builds everywhere,
MSVC included.

What it does with a recording it cannot read is pinned by `tests/snapdump/snapdump_test.cpp`: a
file that is no container, one shorter than the magic, one whose cvar block is cut, a recording that
stops mid-record, and a payload whose id gap cannot be read. Each is held to the exact line the dumper
writes, since those refusals are part of what decoding byte for byte means.

Its exit status is its answer rather than a mapping beside it: `0` the recording read whole with no
error, `1` the bytes could not be got at all or the arguments were wrong, `2` the bytes were read and
are wrong — no container this build knows, a header that would not open, or a record that did not
decode, with the dump written so far naming which. `SnapDumpWrite` returns those three values and
`main` returns what it was handed, so the status cannot drift from the result; one CTest case runs
the tool for each of the three.

## The fuzzer

`snap_fuzz` lives at `tests/snapfuzz/fuzz.cpp` rather than here: a non-engine program with its own
`main`, whose sanitizers are build flags that must not reach the rest of the test binary. It is
built behind `ZANDROX_SNAP_FUZZ` on GCC and Clang, with `ZANDROX_SNAP_FUZZ_LIBFUZZER` for the
libFuzzer mode, and CTest runs 20,000 inputs at its fixed seed plus the wide-reference mode on every
build. The long runs on both widths are a gate someone runs by hand.

`--wide-reference` writes a fresh wide reference and reads it back. It is not compared with the
committed `ref-wide.csnap` and cannot be: the generator fills its structs a byte at a time from one
deterministic stream, and `SnapMoverPlat` is 48 bytes here where a 32-bit `fixed_t` made it 32, so
every draw from the first mover record on differs. The committed copy stays the reference.
