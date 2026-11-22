# FUA in a browser: what the snapshot netcode needs

For whoever builds online play for the web port (`web-port-scope.md`, `web-port-design.md`). The
snapshot netcode (`features/net-snapshot/`) replaces actor replication with server snapshots, and a
browser that joins a server decodes them like any other client. None of this matters while the web
build is single player (`-offline`); all of it matters once a browser joins a server. Paths are
relative to `src/zandronum/src/` unless noted. Written 2026-11-22, when the snapshot port merged.

## 1. Carry datagrams as UDP does: unreliable and unordered

- The snapshot stream (`SVC2_SNAPSHOT`) is built for loss. Each frame is a delta against what the
  client last acknowledged (`CLC_SNAPSHOTACK`), so a lost datagram is never resent: later frames
  carry what is still owed.
- Reordering is handled: a datagram arriving up to 8 behind the newest still counts as delivered
  (`SNAP_REORDER_WINDOW`), and one older than the 32-entry ack history is refused
  (`SNAP_ACK_HISTORY`).
- The reliable commands keep their own sequence numbers and ask for what is missing
  (`CLIENT_CheckForMissingPackets`), a lost last packet included, which a snapshot announces.
- So open the data channel with `ordered: false` and `maxRetransmits: 0`, and carry each datagram
  as one message, as UDP would. The engine needs no change for it.
- A reliable, ordered channel (WebRTC's default) still works, but one lost packet then holds back
  every snapshot behind it until it is resent: a stall of a round trip or more on every loss, which
  is what the snapshot design exists to avoid.

## 2. Datagram size

- A datagram is bounded by `sv_maxpacketsize`, 1550 bytes by default, measured before zstd. The
  measured peak on the wire is about 1,212 bytes, on Chillax MAP13 at the default budget
  (`fua_snap_maxbytes` 4500, about three datagrams a frame).
- A data channel splits a message larger than the path allows (implementations commonly assume
  about 1,200 bytes), and an unreliable message is lost if any piece of it is. The largest
  datagrams come when a frame is fullest, so those would be lost most often.
- `sv_maxpacketsize` is server-wide; there is no per-client ceiling. Measure the peak over the real
  transport first, then choose between a per-client ceiling for browser clients and a lower
  server-wide value (more, smaller datagrams a frame).

## 3. Bandwidth and phones

- On a Chillax MAP13 fight (about 1,000 monsters hunting) a client receives 87-100 kB/s at the default
  budget and 39-40 kB/s at a one-datagram budget, with the reliable channel 2-3% on top. Stock
  replication of the same scene measured 97 kB/s.
- 100 ms of latency with 5% loss passed: no decode errors and the whole world kept, with monsters
  updating about half as often.
- The budget is server-wide (`fua_snap_maxbytes`); a per-client budget for a slow link isn't
  built. How the server rations a client that can't keep up is planned work in the snapshot
  module, and a mobile profile should be measured there rather than guessed.
- The client's own cost is on its `[snap] ghostframe:` line. Measure it in a phone's browser, the
  slowest client there will be.

## 4. Hidden tabs and sleeping phones

- A hidden tab's frame loop is paused or throttled, so the engine stops running tics and stops
  reading the network.
- On the server, a client silent for a second is marked lagging, and one silent for 40 seconds is
  dropped (`CLIENT_TIMEOUT`). The server keeps 2048 reliable packets for each client; a client
  further behind than that can't recover and quits.
- On return, whatever the browser queued arrives at once, and snapshot datagrams outside the
  32-entry ack history are refused. Test it: hide the tab for 5 and for 30 seconds mid-game, and
  sleep a phone, then read the client's `[snap] frame order:` line and the server's
  `[snap] datagram delivery:` line. Dropping snapshot messages in the transport while the tab is
  hidden, rather than queuing them, may be the simpler answer.

## 5. The decode check on wasm

- `fua_snap_selftest`'s decode check hashes one fixed decode and compares it with
  `SNAP_DECODE_HASH`, pinned on Linux, macOS and Windows. Web isn't pinned yet. wasm32 is the only
  32-bit target, so it's the one platform that could differ in ways the others can't show.
- The check needs no network, so it runs in the offline build too. Add `+fua_snap_selftest` to the
  browser smoke test (`web-port-design.md`, 13d) and require `fua_snap_selftest: PASS` in its
  output; with the constant pinned, a PASS means the hash matched.

## 6. Smaller points

- Recordings and logs (`.zsnap`, `.csnap`, the presence logs and the replay's CSV) go to
  `M_GetFuaUserPath()/net-snapshot`. In a browser that's Emscripten's filesystem, so a recording a
  player is asked for needs persisting or a download.
- Mirrors, the client's copies of the server's entities, are drawn on the tic clock. With the
  browser driving the frame loop (`web-port-design.md`, 4g), check by eye that monsters move
  smoothly at the display's rate rather than stepping at 35 Hz.
- The module's README (`features/net-snapshot/README.md`) lists every hook, command and setting.
