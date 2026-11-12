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

- An actor the server places stands on the floor under its box, but never above the height the server gave it
  (`ComputeServerHeldFloorZ`). When the box floor read above that height (a position a fraction of a unit off put the box
  over one more step), the client fell back to the floor under the actor's centre, and gravity dropped it there: on
  narrow stairs an Arachnotron the server held at -64 lay dead at -128 on every client (`netmonsterstairs`). A spawn
  the server sends, as a late joiner gets every actor, reads the box floor the same way, where it read the centre's (after
  the 3D-floor pass in `CLIENT_SpawnThing`, which set the centre's floor again and undid it when it ran first).
- `P_CheckPosition` stops at the first actor it touches, before it reads a line, so an actor the server places
  against another kept the floor under its centre: a Mancubus touching an Arachnotron on narrow steps sank 48 on a
  client. Its floor is then read from the map's lines alone (`ComputeRereadFloorFromLines`, `P_FindFloorCeiling`),
  except for the console player, whose prediction keeps its own.

## Owns

`cl_main.cpp`, `cl_main.h`, `cl_commands.cpp`, `cl_commands.h`, `cl_pred.cpp`, `cl_demo.cpp`, `cl_demo.h`,
`cl_statistics.cpp` and `cl_statistics.h`.

## Public header

`cl_main.h` (the connection, the server tic it holds), `cl_commands.h` (what a client sends) and `netclient.h`
(`ParsedPackets`, the server packets applied, which the render copy refills on).

## Files

- `computation/packetheader_compute.{h,cpp}` with its `_test.cpp`.
- `computation/rejoin_compute.{h,cpp}` with its `_test.cpp`.
- `computation/netstats_compute.{h,cpp}` (`ComputeSharePercent`, the percentage behind `stat nettraffic`'s
  compression factor and zstd share from Zandronum 3.3, called by `cl_statistics.cpp`'s tracker getters)
  with its `_test.cpp`.
- `computation/serverfloor_compute.{h,cpp}` (`ComputeServerHeldFloorZ`, `ComputeRereadFloorFromLines`) with its `_test.cpp`.
- `computation/netgraph_compute.{h,cpp}` (where `cl_netgraph`'s graphs, gridlines, points and legends land, from
  Zandronum 3.3; `cl_statistics.cpp`'s `NetGraph::Draw` only draws them) with its `_test.cpp`.
- `computation/movedrain_compute.{h,cpp}` (`ComputeConsistentArrivals`, `ComputeMoveDrainOrder` on the server's own
  `ComputeRunsSecondMoveCmd`, `ComputeTickSavesPrevPosition`) with its `_test.cpp`.
- `netclient.{h,cpp}` (the packets applied) and `rejoin.{h,cpp}` (the map command's reconnect).

## In-engine hooks

- `cl_main.cpp` `ServerCommands::MovePlayer` and `MoveLocalPlayer` go into the player's move buffer, and
  `UpdatePlayerExtraData` into its extra-data buffer, while `CLIENT_UsingCommandBuffers` says so (`cl_buffercommands`,
  or in a clientside demo's playback what its recorder used, Zandronum 3.3), and `CLIENT_TickCommandBuffers`
  drains each kind by `ComputeMoveDrainOrder`, from `CLIENT_Tick` and from `g_game.cpp` `G_Ticker` after a playback's
  packet; `p_mobj.cpp` `AActor::Tick` leaves a buffered player's previous position to that drain
  (`tools/fuactl/scenarios/netmovebuffer.mjs`, `tools/fuactl/scenarios/clientdemo.mjs`).
- `stats.cpp` `FStat::PrintStat` draws the net graph above the stats while `nettraffic` is one
  (`CLIENTSTATISTICS_DrawNetGraph`, `tools/fuactl/scenarios/netgraph.mjs`).
- `cl_main.cpp` `CLIENT_ParsePacket` is the seam `net/parse`, owned by the main thread, and counts the packet
  (`NoteParsedPacket`) for `features/gamecopy`'s fill.
- `cl_main.cpp` `CLIENT_ReadPacketHeader` (the header kind, the ring slot, the overwritten slots, the jump warning) and
  `CLIENT_CheckForMissingPackets` (the quit's sequences).
- `cl_main.cpp` `CLIENT_GetPackets`: `SERVER_LAUNCHER_IGNORING` is among the launcher replies routed to the
  browser.
- `cl_main.cpp` `MapNew::Execute` (`NoteMapNew`), `CLIENT_AttemptConnection` (`ConnectFlags`), and
  `EndSnapshot::Execute` and `CLIENT_QuitNetworkGame` (`ForgetRejoin`).
- `cl_main.cpp` `CLIENT_MoveThing` (the floor read again from the lines when blocked, then `ComputeServerHeldFloorZ`) and
  `CLIENT_SpawnThing` (`ComputeServerHeldFloorZ` after the 3D-floor pass).
