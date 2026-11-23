# apply: the client's snapshot commands

Covers: src/zandronum/src/features/net-snapshot/apply.h, src/zandronum/src/features/net-snapshot/apply.cpp, src/zandronum/src/features/net-snapshot/computation/sound_compute.h, src/zandronum/src/features/net-snapshot/computation/sound_compute.cpp, src/zandronum/src/features/net-snapshot/computation/sound_compute_test.cpp

## Responsibility

The client's handlers for the snapshot server commands, kept out of `cl_main.cpp`, whose dispatch
holds a one-line call for each: the snapshot datagram and its ack, the door, mover and floor sounds,
the sound table, and a snapshot sound. Each reads its command's body from the stream. Decoding the
payload itself is `entities.cpp`'s ([entities.md](entities.md)); the cosmetic events, the seventh
command in that dispatch, are read in `ghostactor.cpp` ([ghostactor.md](ghostactor.md)).

## A snapshot datagram

`OnSnapshot`, in order:
1. Reads the header (frame, game tic, datagram sequence, map stamp, reset count, ack lag, newest
   reliable sequence, payload size), and refuses an implausible or truncated payload without acking
   it. The bounds are checked here, since `ReadBuffer`'s overflow path leaves the previous packet's
   bytes in the buffer, which would decode cleanly against the wrong frame.
2. Passes the reliable sequence to `CLIENT_NoteReliableSequence`, ahead of the map stamp test, so a
   lost reliable tail is requested even from an old map's datagram; not while playing a demo
   (`CLIENTDEMO_IsPlaying`), whose announcements belong to the recorded server, and not before this
   connection has joined (`ConnectionJoined`), which a map change does not clear.
3. Drops a datagram for any map but the joined one (`AcceptMapStamp`), neither applied nor acked:
   one before `SnapJoin`, or a torn-down map's. After a new `SnapJoin` (`JoinCount`), starts the ack
   state again: a new stream begins there and nowhere else.
4. Refuses a datagram too late to count (`ComputeAckTooLate`): once the client has acked
   `SNAP_REORDER_WINDOW` past it while it was missing, or it is past the ack history, the server
   has declared it lost and repairs it as never delivered, so it is neither applied nor acked and
   its bit is never set. `TooLate` and `PastHistory` count the two for the frame order line;
   `fua_csnap_debug_applylate` applies them as before, to compare. `SNAP_ACK_HISTORY` refusals past
   the history in a row, with nothing accepted between them, can only mean the newest sequence came
   from another stream (an old server's datagram landing after a restarted one's `SnapJoin` with the
   same map stamp), so the ack state starts again there, with a warning (`AckRecoveries`).
5. Finds how far back the newest missing datagram is within the ack history's 32, and tells the
   decoder whether the server may not yet have repaired it when it built this one
   (`ComputeAckSettled` on the echoed ack lag; `SetDatagramGapBehind`), which the checksum reads as
   unsettled. It errs cautious: the missing datagram may belong to an unrelated frame, so a few
   settled records read as unsettled.
6. Applies the payload with `ApplyDelta`, between `MoverBeginDatagram` and `MoverEndDatagram`, and
   records it if `fua_csnap_record` is running (`RecorderDelta`), with the same bytes and arrival
   time the decoder saw.
7. Hides the legacy copies of ids that just entered, on the packet clock so that a low frame rate
   cannot slow it, and shows the ghosts.
8. Folds the sequence into the ack state (`ComputeAckFold`) and sends `CLIENTCOMMANDS_SnapshotAck`:
   the newest sequence, a bitmask of the 32 before it, and the sound-table answer. Acked
   unconditionally, since a rejected payload still arrived, though not a datagram too late to count.

## Public functions

- `OnSnapshot`: one `SVC2_SNAPSHOT` datagram, above.
- `TooLate`, `PastHistory`, `AckResets`, `AckRecoveries`: the datagrams refused as too late to count
  inside the ack history and past it, and the times the ack state started again, at a `SnapJoin` and
  after a run of refusals past the history, for the frame order line. The reset is counted on the
  first datagram after a join, which is where the state is begun again.
- `OnDoorSound`, `OnMoverSound`, `OnFloorSound`: `SVC2_DOORSOUND`, `SVC2_MOVERSOUND` and
  `SVC2_FLOORSOUND`, each played only while that class's legacy handlers are gated
  (`MoverSuppressesLegacy`); otherwise they play their own ([movers.md](movers.md)).
- `OnSoundTable`: `SVC2_SNAPSOUNDTABLE`, the server's table size and hash for this map; this client
  hashes its own first names and records whether they match (`SetSoundTable`), which its acks then
  answer.
- `OnSnapSound`: `SVC2_SNAPSOUND`, one sound by table id, at a position in the map's frame, read
  through the shared codec and dropped when its map stamp is not the current one -- that map's table
  named the id and its mirrors are gone, so there is nothing the sound could mean
  ([entities.md](entities.md)).
- `CLIENT_NoteReliableSequence`: declared here, defined in `cl_main.cpp` beside the packet sequence
  state it reads. A snapshot's newest reliable sequence, decoded against the highest received, so a
  reliable tail lost while the reliable stream is quiet is requested. One at or below the highest
  clears the announcement, so the next real snapshot overwrites a stray one; one more than
  `SNAP_TAIL_WINDOW` past it is clamped, and a longer run is requested in parts as the highest
  rises.

## The sound codec

`SVC2_SNAPSOUND`'s body is written and read in one place, `computation/sound_compute.h`, so the
server's `BroadcastActorSound` ([capture.md](capture.md)) and `OnSnapSound` cannot drift apart:
- `SnapSoundWire`: one body: the map stamp, whether it is a point (x, y and z travel, in the map's
  frame) or on an actor, the loop bit, the NetID (a point carries it too, so the client can move
  the sound onto a mirror that appears late), the channel's low byte, the table id, volume and
  attenuation.
- `SnapSoundFlags`: the flags byte those fields imply. A point drops the loop, since a loop with no
  actor could never be stopped.
- `WriteSnapSound`, `ReadSnapSound`: the body into a buffer of `SNAP_SOUND_MAX_BYTES`, and back
  through the codec's cursor. A volume of `SNAP_SOUND_VOLUME_NORM` and an attenuation of
  `SNAP_SOUND_ATTEN_NORM` are left out. The reader takes each axis's travelling width, so an axis
  this build cannot read is skipped and the rest stay in step.

## Data structures

The ack state: the newest datagram sequence and the 32-bit history behind it, statics here because a
rejected payload still updates them. The payload buffer the decoder reads.

## Rules that must hold

- **Ack datagrams, never frames.** A frame carried by several datagrams would otherwise tell the
  server that all of them arrived when one did.
- **Ack everything that arrived for this map**, applied or rejected; only a datagram for another map
  is neither applied nor acked.
- **A new stream begins only at `SnapJoin`**, which follows every new connection and map load; a
  datagram past the ack history is refused as too late, never read as a new stream. The one other
  restart is the guard above, which a held-back datagram cannot trip, since it is refused alone
  among datagrams accepted on time. A duplicate sets no bit.
- **Never ack a truncated payload**: its bytes were not received.
- **The recorder gets what the decoder got**, the same buffer and arrival time, or a replay
  disagrees with what the client saw.
- **A sound body is read with the cursor, and the stream follows it.** `SnapCosCursor` reads past
  its end the way the stream does, so a short packet is caught by comparing the cursor with the
  stream's end rather than by trusting a length.

## Engine hooks

- `cl_main.cpp`: the `SVC2_SNAPSHOT`, `SVC2_DOORSOUND`, `SVC2_FLOORSOUND`, `SVC2_MOVERSOUND`,
  `SVC2_SNAPSOUNDTABLE` and `SVC2_SNAPSOUND` cases in the extended command dispatch, under one
  `[Kaori-Chani]` comment with `SVC2_SNAPCOSMETIC`; `CLIENT_NoteReliableSequence` is defined there.

## Tests

None in the self-tests: a datagram needs a server. `computation/sound_compute_test.cpp` is a
GoogleTest file under the 100% line gate: the stamp leading every body, the flags leaving out every
default, an actor's sound at seven bytes, a point's three axes in the frame's width, every body
reading back as written, a short body, and an unknown width skipped. The client's `[snap]` lines
check the rest live: the datagram and ack counts, the checksum's verified, unsettled and DIVERGED
counts, the ack-state resets at each join, and the reliable tail requests. `fua_snap_debug_loss` and
`fua_snap_debug_ackdelay` on the server exercise the ack history under loss and reordering, and
`fua_snap_debug_holdback` past the reorder window or the history.

`fua_csnap_debug_presencelog`, a number of seconds, appends at that interval the newest frame
applied and the ids this client holds to `snappresence-cl<N>.txt` in the module's folder, N its
player number. Beside the server's log, `tools/snapshot/presence_diff.py` counts the mirrors a
client holds that the server holds absent, which the checksum cannot see: a ghost the server holds
absent is sent no records ([tools.md](tools.md)).
