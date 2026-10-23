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

## Files

- `computation/packetheader_compute.{h,cpp}` with its `_test.cpp`.

## In-engine hooks

- `cl_main.cpp` `CLIENT_ReadPacketHeader` (the header kind, the ring slot, the overwritten slots, the jump warning) and
  `CLIENT_CheckForMissingPackets` (the quit's sequences).
- `cl_main.cpp` `CLIENT_GetPackets`: `SERVER_LAUNCHER_IGNORING` is among the launcher replies routed to the
  browser.
