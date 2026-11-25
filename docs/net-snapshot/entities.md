# entities: the ghost table, sampler and clock model

Covers: src/zandronum/src/features/net-snapshot/entities.h, src/zandronum/src/features/net-snapshot/entities.cpp, src/zandronum/src/features/net-snapshot/computation/clock_compute.h, src/zandronum/src/features/net-snapshot/computation/clock_compute.cpp, src/zandronum/src/features/net-snapshot/computation/clock_compute_test.cpp, src/zandronum/src/features/net-snapshot/computation/sample_compute.h, src/zandronum/src/features/net-snapshot/computation/sample_compute.cpp, src/zandronum/src/features/net-snapshot/computation/sample_compute_test.cpp, src/zandronum/src/features/net-snapshot/computation/join_compute.h, src/zandronum/src/features/net-snapshot/computation/join_compute.cpp, src/zandronum/src/features/net-snapshot/computation/join_compute_test.cpp

## Responsibility

The client's half of the entity stream that needs no engine type: the decoder that applies a
snapshot payload to the ghost table, the sampler that turns a ghost into a pose for a render time,
and the model of the server's clock that dates records. Everything the engine must do with a record
(a mirror, a psprite, a property set, the prediction ring, a mover thinker) is behind an installed
hook, so the same code runs against live datagrams, a `.zsnap` recording (`fua_snap_verify`,
`fua_csnap_analyze`) and the fuzzer. The mirrors are `ghostactor.cpp`
([ghostactor.md](ghostactor.md)); the datagram around the payload is `apply.cpp`
([apply.md](apply.md)); the local player's block and its census are `localpred.cpp`
([localpred.md](localpred.md)), which this port keeps in the module rather than in stock
`cl_pred.cpp`.

The design: [Client: the ghost table and sampler](design.md#6-client-the-ghost-table-and-sampler),
[Extrapolation](design.md#extrapolation), [Time base and tick
synchronization](design.md#8-time-base-and-tick-synchronization) and [The local
player](design.md#42-the-local-player).

## The model and the drawn sample

Each ghost keeps two views. The **model** (`nextState`) is the in-order fold of every record, the
state the server's checksums describe. The **drawn sample** is the newest entry of a ring of
`CLENT_GHOST_HISTORY` samples; it starts as the model and may carry a branch event's state and
position on top. Later records merge onto the model, never onto the overlay.

Every group has a frame stamp (`groupFrame`). A record older than a group's stamp leaves that group
alone, so arrival order cannot undo the fold: a late record goes into the model only for the groups
no newer frame wrote, into the drawn sample only where the game clock allows, and pushes no history.
A leave stamps all six groups, so a re-sent enter or update that the leave overtook is refused,
including a leave for an id the client does not hold: an enter it overtook is refused when it lands
and counted apart (`LateEntersUnheld`). Each late record also counts in `SnapApplyStats.late`, which
`VerifyFixture` fails on, since a recording's tics only go forward.

## Applying a payload

`ApplyDelta`, one call per datagram, with the frame number, the game tic and the arrival time:
1. Notes the payload's frame and game tic (`ApplyingServerTic`), counts an older or repeated frame
   number, advances the newest frame and game tic only forward, and feeds the clock model.
2. Restarts the id cursor (`IdCursorReset`): one payload is one datagram, decodable alone.
3. For each record, reads the id and the mask byte, then:
   - **an event** (the event bit is tested first): its length comes from `EventRecordBytes`. A
     self-describing type this build does not know is skipped; an unknown fixed type or a truncated
     one abandons the rest of the payload. The rest go to the event applier below.
   - **a leave** (mask 0): marks the ghost absent and forgets its acceleration and last event,
     unless a group is stamped after it; one for an absent ghost is counted as redundant and still
     stamps it, unless a newer stamp is held.
   - **a record for an absent ghost** that is not an enter, or that the leave overtook: read past by
     its length and not applied. One at or after the leave is an error, since the server believes in
     an id this client does not hold.
   - **an enter** onto an absent ghost: seeds the ring with the state, notes the id for
     `EnteredIds`, and records the shooter NetID. A different shooter on a present id is a new
     missile in a reused slot.
   - **an update**: pushes a new sample built from the model, keeping position and appearance from
     the previous sample where the record did not carry them, and measures Z acceleration when
     velocity arrives (adopted only when two readings agree).
4. Verifies the checksum of a record carrying VELMISC against the merged model, counting it as
   unsettled when `SetDatagramGapBehind` said a datagram missing before it may not yet be repaired
   in the server's model.

A malformed id, a truncated record or an unknown fixed event type ends the payload with an error
count; the caller still acks the datagram.

## Events and sinks

The event applier handles the five types:
- **Mover**: decodes the envelope, checks the body's length with `MoverRecordBodyBytes`, and hands
  it to the mover sink. A mover names no entity, so this runs ahead of the id and presence tests.
- **Local block**: to the local sink, with the frame's game tic, ahead of the presence test, since
  the local player has no mirror. The tic is the frame being applied, not the newest seen: an
  out-of-order payload's records are still applied, and a stale block stamped with the newer clock
  would defeat the sink's ordering guard. Branch events take the newest tic instead, as the epoch of
  their 16-bit field.
- **Psprite**: to the psprite sink, for a present ghost.
- **Properties**: to the property sink, for a present ghost; a body that does not hold what its
  flags claimed is refused and counted (`PropsEventStats`). The payload is whole rather than a diff,
  so a client that missed one never holds half a set ([format.md](format.md)).
- **Branch**: dropped when the mirror does not animate (the animates hook) or when it repeats the
  last event applied. Otherwise its state and position are written into the drawn sample, each only
  where it is newer on the game clock than what the sample holds, and `eventSeq` is bumped so the
  mirror resyncs.

Each sink and lookup is an installer hook that returns the one it replaces:
- `SetMoverSink`: installed by `movers.cpp` (`MoverInstallSinks`).
- `SetLocalSink`: installed by `localpred.cpp`.
- `SetPSpriteSink`, `SetPropsSink`, `SetAnimatesLookup`, `SetLawGravity` (the server's gravity law,
  `LawAt`): installed by `ghostactor.cpp`.
- `csnap_replay.cpp` clears the sinks for a replay, installs its own animates lookup, and installs
  the law only on a map whose checksum matches the loaded level's; `fuzz.cpp` clears them all,
  keeping the mover sink in its "sinks" mode. Both put back what they replaced.

A record that reaches a missing sink is counted (`noSink`), which separates "feature off" from
"installer never ran".

## Sampling

`SampleGhost` draws the newest state, extrapolated where the motion is predictable, with no render
delay. Ages are measured from each record's date on the clock model when `fua_csnap_age_servertime`
is on and `ComputeAgeSource` trusts the model, otherwise from arrival. A ghost is not drawn before
its enter arrived. The exits, in order:
1. **ahead**: a ballistic missile whose position is dated after now is drawn between its two
   positions bracketing now (`ComputeAheadFraction`, `ComputeHermite`), unless `ComputeFlightJoins`
   says the two were not one flight.
2. **extrap**: a ballistic missile is carried along its velocity for at most `MissileCapMS`, by the
   gravity law when the hook answers and by the measured acceleration otherwise (`ExtrapLaw`,
   `ExtrapBallistic`, the same code the server's suppression predicts with).
3. **extrapvel**: a non-player ghost with a missile bit and a wire velocity (a missile not yet
   proven, or a monster's charge) is carried for at most `CLENT_VELEXTRAP_MS`; when the newest
   record lacks x and y, they run on from the record that carried them, clamped to its rounding
   cell. A body (no missile bit) is never carried, and falls through to the held exits.
4. **heldnew**, **heldmove**, **held**: everything else is drawn at its newest record. A ghost with
   one sample cannot have a velocity; one whose last two positions differ froze mid-stride, which
   the census counts apart.

The pose carries what the mirror needs beyond position: the path an extrapolation took (for the wall
sweep), the age in tics since the newest record (`ageTics`) and since appearance was last carried
(`appearAgeTics`), and the tic gap between the last two positions.

The sampler is `sample_compute`, a pure unit. `clent_SampleInner` gathers the ghost's ring and
fields, its dated ages and the frame's shared inputs (`SnapSampleGhost`, `SnapSampleContext`), calls
`ComputeSample`, and counts what it returns (`SnapSampleOut`): the exit, how a dated-ahead attempt
ended, whether the missile cap held it, whether the gravity law ran, and the extrapvel exit's cap
and cell. The exit's name, the missile, gravity and held censuses and `SampleExit` stay here.

## The clock model

A windowed least-squares fit of game tic to arrival time over `CLENT_CLOCK_WINDOW` samples, fed once
per frame from its first datagram. It is ready after `CLENT_CLOCK_MIN_READY` samples and dates ages
only after `CLENT_CLOCK_TRUST_SAMPLES`, by when a map load's burst of frames (which reads 22-28 ms a
tic) has left the window. Outliers are rejected against a multiple of the window's own residual with
a floor (`CLENT_CLOCK_OUTLIER_FLOOR_MS`), permissively: a saturated server's pump swings by hundreds
of milliseconds, and slow pumps make large positive residuals that fast ones cannot match, so tight
rejection biases the estimate fast. A run of `CLENT_CLOCK_MAX_SKIP_RUN` rejections re-seeds it; a
tic or arrival time going back, or a gap past `CLENT_CLOCK_STEP_MS`, resets it. The date of a tic
is `ComputeClockDate`.

The model is `clock_compute`, a pure unit: `SnapClockModel` holds the samples and the fit, and
`entities.cpp` owns one (`g_clock`), which `ClockSample` and the other clock functions use.
`ComputeClockSample` returns what a sample did (`SnapClockSampled`: accepted, rejected, re-seeded
after a run of rejections, or reset by a discontinuity, with the residual of an accepted sample),
and the caller counts it.

The report's window counters (the interval's range, residuals, rejections, resets and the ages
counted by source) are separate from the model and outlive `ClockReset`: only `ClockStats` with
`reset` clears them. The client's report line and each replay segment read them that way; the replay
drains them when it starts and the self-tests when they end, so one run's counts never land in
another's report.

## The map stamp, frame and sound table

`SetMapFrame`, from `SnapJoin` and before the anchor so the recorder's map record carries it,
installs the map's position frame, which every codec call on the client reads through
`ClientMapFrame`. `UseMapFrame` installs a frame from a file instead, for the replay and
`fua_snap_verify`. A width with no encoding refuses the map, since no reader could size it:
`AcceptMapStamp` and `MapStampCurrent` turn its payloads and cosmetic events away, and
`SoundIdUsable` its snapshot sounds, until the next SnapJoin, so a mismatched build shows an empty
world and a console warning rather than misplacing everything. `ClientFrameWire` hands a reader the
bytes each axis travels in, left at what the server announced for a frame this build cannot read, so
a body still reads in step; `ReadFramePos` reads a whole-unit position relative to the origin, in
its axis's width, as a `fixed_t`. `ResetMapStamp` returns the frame to the narrow one at origin 0.

`AnchorMapStamp`, from `SnapJoin`, joins one map; `AcceptMapStamp` then accepts only that map's
stamp and counts the rest as stale, ahead or before the join, by the stamp's distance from the
joined one (`ComputeMapStampAhead`, in `join_compute`). `ConnectionJoined` says whether this
connection has had a SnapJoin at all, which a map change does not clear, so a previous connection's
datagram is told apart from this one's. `SetSoundTable` records the server's sound-table answer for
the joined map, which `SoundIdsAnswer` returns in the ack and `SoundIdUsable` applies to each sound
id; `CountSoundOtherMap` and `SoundOtherMap` count the sounds dropped for naming another map's
table. `ResetMapStamp` clears the stamp and the table at a connect or a demo start, and arms the
configuration line (`ReportCvars`) for the next payload.

## Data structures

- `SnapGhost`: one per id, for the whole id space: presence, the sample ring, the model, the group
  stamps, the acceleration measurement, the last event, the shooter NetID and the last rendered
  pose.
- `SnapGhostSample`: one sample with its arrival time, frame and game tic, and separate clocks for
  when position (`posTic`, `posMS`) and appearance (`appearTic`) were last carried.
- `SnapGhostPose`: the sampler's output, including whether its ages were dated by the clock model,
  which the smoothing census reads.
- `SnapApplyStats`, `SnapGhostDebug`: one payload's record counts, and a ghost's raw state for
  diagnostics. `SnapGhostDebug` is also what the decode check hashes.
- The census structures: `SnapBlindPredict`, `SnapMissileCensus`, `SnapSampleCensus`,
  `SnapHeldMoveClass`, `SnapJumpInfo`, `SnapClockStats`.

## Public functions

The table and the stream:
- `ClientReset`: clears the table, the stream version and the clock model. `ForgetWorld`: the same
  at level teardown, and drops the join, without allocating a table that does not exist.
- `ApplyDelta`, `SetStreamVersion`, `SetDatagramGapBehind`: above.
- `ApplyingServerTic`, `StreamEpoch`: the payload being applied, and the counter bumped wherever
  frame numbering restarts, for a sink ordering its own records.
- `NewestGameTic`, `ExportBaseline`: the newest game tic, and every present ghost as an enter block
  for a recording's baseline.
- `PresentIds`: the present ids and the newest frame applied, for the presence log
  ([apply.md](apply.md)).
- `TouchedIds`: the ids the latest `ApplyDelta` applied a record to, which the mirrors' fold walks
  once per packet.

The map stamp, frame and sound table:
- `AcceptMapStamp`, `MapStampCurrent` (the same test, uncounted), `ResetMapStamp`, `AnchorMapStamp`,
  `ConnectionJoined`, `JoinCount` (the joins so far, where `apply.cpp` starts its ack state again).
- `SetMapFrame`, `UseMapFrame`, `ClientMapFrame`, `ClientFrameWire`, `ReadFramePos`: the map's
  frame, above.
- `JoinedStamp`, `SetJoinedStamp`: read and put back the join, for the fuzzer's offline join.
- `SetSoundTable`, `SoundIdsAnswer`, `SoundIdUsable`, `CountSoundId`, `CountSoundOtherMap`,
  `SoundOtherMap`.

Ghost lookups:
- `PresentCount`, `ListPresent`, `PresentIds`, `GhostPresent`, `EnteredIds`.
- `LeftWithin`, `DepartedOrigin`: whether an id left recently by arrival time, and where it was, for
  a sound addressed to it.
- `GhostPosition`, `GhostClassId`, `GhostDebug`, `GhostsWithEffects`. `GhostPosition` gives the
  newest record's position, the model later records merge onto, without a branch event's; where the
  sampler holds a ghost, event included, is `SnapGhostDebug`'s `posX`, `posY`, `posZ`.
- `GhostTarget`, `GhostTargetResolved`: the shooter NetID the enter carried, kept after the leave.

Sampling and the clock:
- `SampleGhost`, `MissileCapMS` (`fua_csnap_missile_extrap`, raised to the server's bridge), and
  `SampleExit`, the exit the last sample took, for the replay's reports.
- `SampleGhostQuiet`: the same sample without the census, for a pose other than the one a mirror is
  set to.
- `SetViewPoint`, `SetLocalPlayerId`: the camera for the missile census, and the local pawn's NetID,
  which splits the populations.
- `ClockReset`, `ClockSample`, `ClockReady`, `ClockMsPerTic`, `ClockTicAt`.

Installer hooks: `SetMoverSink`, `SetLocalSink`, `SetPSpriteSink`, `SetPropsSink`,
`SetAnimatesLookup`, `SetLawGravity`, above.

Report readings (each feeds a `[snap]` line or the replay's):
- `ClientEventStats`, `EventAgeStats`, `EventCarryStats`, `ClientEventStatsReset`: events applied,
  refused and late, by age and state duration, and records that kept an event's overlay.
- `MissileStats`, `MissileStatsReset`, `MissileCensus`, `BlindPredict`, `GravStats`,
  `ClientLawStats`: how missiles were drawn, how far blind flight would have strayed, and the
  gravity class. The blind-flight census is taken at each position record on a missile flagged
  ballistic across the whole gap, by the tics it would have flown blind, and sizes the extrapolation
  cap and so the record rate missile prediction can drop to; `turned` counts the missiles whose bit
  cleared in the gap, which prediction gets wrong outright.
- `SampleCensus`, `HeldMoveCensus`, `TakeJumpLog`, `JumpGapStats`, `IntervalStats`: the sampler's
  exits, frozen ghosts by class, the largest jumps, and position gaps per population.
- `ChecksumStats`, `StampStats`, `LateEntersUnheld`, `FrameOrderStats`: divergence, late records
  (enters refused by a leave for an id not held counted apart) and frame order.
- `MapStampStale`, `MapStampAhead`, `BeforeJoin`, `SoundIdStats`.
- `ClientMoverStats`, `ClientLocalStats`, `ClientPSpriteStats`, `PropsEventStats`: records routed to
  each sink, and those with none installed.
- `NoteLocalFire`, `LocalFireStats`: fire to the first sight of the player's own shot.
- `ClockStats`, `ClockDescribe`.

The clock model (`computation/clock_compute.h`):
- `ComputeClockReset`, `ComputeClockRefit`, `ComputeClockSample`: forget the fit, refit the window,
  take one sample.
- `ComputeClockReady`, `ComputeClockMsPerTic`, `ComputeClockFeeds`, `ComputeClockTrusted`,
  `ComputeClockTicAt`: readings of the model.
- `ComputeClockDate`, `ComputeAgeSource`: a tic's date on the fit, and what dates a sample's ages
  (`SNAP_AGE_UNTRUSTED`, `SNAP_AGE_JITTERY`, `SNAP_AGE_MODEL`).

The sampler (`computation/sample_compute.h`):
- `ComputeSample`: a ghost's pose at a render time and the exit that drew it (`SNAP_SAMPLE_ABSENT`
  to `SNAP_SAMPLE_HELD`). `ComputePoseFromState`: the newest state's pose, unextrapolated.
- `ComputeAheadPose`: the dated-ahead exit's pose between two records, or why it held
  (`SNAP_AHEAD_INTERP`, `SNAP_AHEAD_JUMP`, `SNAP_AHEAD_FIRST`).
- `ComputeAheadFraction`, `ComputeHermite`, `ComputeFlightJoins`: how far between two records now
  falls, the curve through them, and whether flight can join them. `ComputeHermite` also draws the
  replay's ballistic reference ([csnap_replay.md](csnap_replay.md)).
- `ComputeTicBucket`: the tic-gap buckets the interval and jump censuses share.

Which map a stream belongs to (`computation/join_compute.h`):
- `ComputeMapStampAhead`: how many map loads a stamp is past the joined map's, -128 to 127 through
  the wrap at 256.
- `ComputeExtentTake`: widens one axis of the server's map extent to take a position's whole units,
  floored and ceiled ([capture.md](capture.md)'s `ServerMapFrame`).

## Settings

Client only, all archived: `fua_csnap_missile_extrap` (500 ms; 0 off; a value below the server's
ballistic bridge is raised to it), `fua_csnap_age_servertime` (on), `fua_csnap_age_maxjitter` (50
ms; 0 never falls back to arrival).

## Rules that must hold

- **No engine types.** What needs one is a hook; that is what lets recordings, the replay and the
  fuzzer drive this code.
- **The model is the fold; the drawn sample is the model plus at most one event.** A record merges
  onto `nextState`, never onto the drawn sample, or an event becomes the base the server's checksum
  is compared against.
- **Late is per group.** A record older than a group's stamp leaves that group alone; refusing the
  whole record would lose the groups it alone carries.
- **Elapsed time is game tics; delivery is frame numbers.** They differ under catch-up, so the clock
  model, ages and acceleration take game tics, and ordering takes frames.
- **The table spans the id space**, asserted at compile time, so a wire id indexes it directly.
- **A length unknown is a stop.** A fixed event type this build does not know cannot be skipped;
  only a self-describing one can.
- **The extrapolators are shared** with the server's suppression (`format.h`), so what the server
  withholds is what the sampler draws.

## Engine hooks

The stock call sites, each a `[Kaori-Chani]` line:
- `cl_main.cpp`: `ResetMapStamp` in `client_ResetValuesOnConnection`; `SetMapFrame` and
  `AnchorMapStamp` in `SnapJoin`.
- `cl_demo.cpp`: `ResetMapStamp` when playback starts.
- `p_pspr.cpp`: `NoteLocalFire` in `P_FireWeapon`.

The rest of what the reference tree hooked into stock files is inside the module here: the snapshot
sound's reads (`ReadFramePos`, `SoundIdUsable`, `CountSoundId`) are `apply.cpp`'s handler, the
lost-sound lookups (`LeftWithin`, `DepartedOrigin`, `GhostPosition`) are the mirrors', and the local
block's sink and census are `localpred.cpp`'s, which stock `cl_pred.cpp` installs and calls, and
which calls stock `CLIENT_ApplyLocalBlock` ([localpred.md](localpred.md)).

## Tests

`RunSelfTests`, run by `fua_snap_selftest`, names each block as it runs. It covers the position
codec, the sampler on a synthetic ghost through the decoder (its velocity exit on an unproven
missile, and a body with a wire velocity held), the leave's stamp on an id not held (after a leave
for an id never held, and for one whose earlier occupant had left, an older enter is refused and
counted apart and a newer one applies, and a table cleared after a high frame takes an enter at a
low one), the property event, and **the decode check**: a payload fixed in the source, wide on every
axis at a nonzero origin, through `ApplyDelta`, hashed over the poses it leaves and held to
`SNAP_DECODE_HASH`, the figure the three native platforms agreed on. The ghost-table blocks refuse a
live table and say so, so the selftest is run before connecting.

The computation units are GoogleTest files under the 100% line gate:
- `computation/clock_compute_test.cpp`: the clock date, the model's convergence through noise,
  recovery from a step, the ready and trusted counts, and every reset, rejection and re-seed.
- `computation/sample_compute_test.cpp`: dated ahead (the fraction, the curve, the jump test and the
  tic buckets), held (ages, tics, the gap and the dating stamped on every sample, and the held
  exits), extrapvel (an unproven missile's cap and rounding cell, and a body held), ballistic (the
  missile cap and the gravity law) and ahead (between two records, a jump, nothing dated before
  now).
- `computation/join_compute_test.cpp`: the map stamp (the joined map, behind and ahead, the wrap and
  the halves' edges) and the map extent (whole units, fractions either side of 0, the `fixed_t`
  limits, and only widening).

`fua_snap_verify` plays a recording through the decoder ([selftest.md](selftest.md)), and the fuzzer
drives it with mutated payloads ([fuzz.md](fuzz.md)). Live: `[snap] checksum` reads 0 DIVERGED and
`[snap] movers` no records without a sink.
