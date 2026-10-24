# net-client

What a client does with what arrives from its server.

- A packet from the server is unreliable (parsed at once), reliable (sequenced and stored) or stray, with
  neither header (`ComputePacketHeaderKind`). A stray one is dropped with a warning naming its first byte:
  reading a sequence from its body set the highest received sequence to garbage, and the client quit with
  "Missing more than 2048 packets".
- The stray packet was the server's `SERVER_LAUNCHER_IGNORING`: a client's server browser queries the server
  it plays on, and a second client from the same address within ten seconds is told it is ignored. That reply
  now goes to the browser (`features/server-browser` `ShouldRouteToBrowser`). Two players behind one address
  (a household, a LAN party) hit it; `netsync`'s observer hit it on every other join.
- A sequence that jumps by more than 256 is warned about, and the quit names both sequences.
- The packet ring uses all its `PACKET_BUFFER_SIZE` slots (`ComputeNextPacketSlot`); its index was a byte, so it
  kept 256. With every slot live the stored bytes can wrap over a packet still waiting for a gap to fill, so a
  slot whose bytes a new packet lands on is forgotten (`ComputeOverwritesSlot`) and asked for again.

- A player the server's `map` command disconnects (`SERVER_ReconnectNewLevel`) reconnects without
  `CCF_STARTASSPECTATOR` (`ComputeRejoinsAfterMapNew`, `ComputeConnectFlags`), so it comes back playing where
  every client used to come back spectating (`netmapchange`). The server's own rules (join password, teams, join
  queue) still decide.

## Owns

`cl_main.cpp`, `cl_main.h`, `cl_commands.cpp`, `cl_commands.h`, `cl_pred.cpp`, `cl_demo.cpp`, `cl_demo.h`,
`cl_statistics.cpp` and `cl_statistics.h`.

## Public header

`cl_main.h` (the connection, the server tic it holds), `cl_commands.h` (what a client sends) and `netclient.h`
(`ParsedPackets`, the server packets applied, which the render copy refills on).

## Files

- `computation/packetheader_compute.{h,cpp}` with its `_test.cpp`.
- `computation/rejoin_compute.{h,cpp}` with its `_test.cpp`.
- `netclient.{h,cpp}` (the packets applied) and `rejoin.{h,cpp}` (the map command's reconnect).

## In-engine hooks

- `cl_main.cpp` `CLIENT_ParsePacket` is the seam `net/parse`, owned by the main thread, and counts the packet
  (`NoteParsedPacket`) for `features/gamecopy`'s fill.
- `cl_main.cpp` `CLIENT_ReadPacketHeader` (the header kind, the ring slot, the overwritten slots, the jump warning) and
  `CLIENT_CheckForMissingPackets` (the quit's sequences).
- `cl_main.cpp` `CLIENT_GetPackets`: `SERVER_LAUNCHER_IGNORING` is among the launcher replies routed to the
  browser.
- `cl_main.cpp` `MapNew::Execute` (`NoteMapNew`), `CLIENT_AttemptConnection` (`ConnectFlags`), and
  `EndSnapshot::Execute` and `CLIENT_QuitNetworkGame` (`ForgetRejoin`).
