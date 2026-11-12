# net-transport

The game's UDP socket as tests drive it: a simulated ping and packet loss, set per instance, so netgame scenarios
(`tools/fuactl` `netplay --lag/--loss`, `compareNetplay`) run at a real link's latency on loopback.

- `fua_net_fakeping` adds that many milliseconds to the instance's round trip: half on each packet it sends,
  half on each it receives (`ComputeFakeDelayMs`). Packets keep their order, as a link with a steady ping does.
- `fua_net_fakeloss` drops that percent of packets each way, from a generator seeded the same every run
  (`ComputeFakeDrop`).
- `fua_net_fakejitter` holds each packet up to that many milliseconds more each way (`ComputeFakeJitterMs`); one
  held longer holds those behind it, so they keep their order and arrive in bursts, as on a jittery link.
- `net/held`, `net/fake_dropped`, `net/held_released` and `net/held_send_failed` count what is held each way.
- A server reads its game socket on a thread of its own (`sv_fua_receivethread`, on by default and switched in a
  running server): the thread waits in `select`, stamps each datagram's arrival on `I_MSTime`'s clock and queues
  it (`ComputeAdmitDatagram` drops the newest past 4096, counted as `net/receive_dropped`), and the tick's wait
  for its next tic wakes as one arrives (`WaitForDatagram`) where it slept a millisecond at a time. Ping counts
  from arrival (`NETWORK_GetArrivalMS`), so a slow tick is not read as a client's lag (`netstall`). Parsing,
  sending and the simulated conditions stay on the main thread; a client reads its socket inline, as before.
- The held packets keep their own monotonic clock: `I_MSTime` counts from a base set only once the timer is
  chosen, so a packet held at startup came due nine days later and every one behind it waited.

## Owns

`network.cpp`, `network.h`, `networkshared.cpp`, `networkshared.h`, `networkheaders.h`, `i_net.cpp`, `i_net.h`,
`huffman/`, `network/nettraffic.cpp` and `network/nettraffic.h`.

## Public header

`network.h` (sending, receiving, addresses) and `netconditions.h`. Packets launched or sockets touched outside
the netcode are counted by `net-sockets-through-transport` (`tools/boundary-rules.json`) and may only shrink.

## Files

- `computation/netconditions_compute.{h,cpp}`: the delay, the drop and the held-packet queue, tested in
  `computation/netconditions_compute_test.cpp`.
- `computation/zstdwire_compute.{h,cpp}`: Zandronum 3.3's zstd wire decisions — the level clamp, the try-zstd
  gate, the smart Huffman fallback and the frame check with its one-in-2^32 misread — tested in
  `computation/zstdwire_compute_test.cpp`.
- `netconditions.{h,cpp}`: the cvars and the two queues.
- `recvthread.{h,cpp}`: a server's receive thread and its queue.

## In-engine hooks

- `network.cpp` `NETWORK_GetPackets` and `NETWORK_LaunchPacket` are the seams `net/receive` and `net/send`
  (`features/handoff/seam.h`), owned by the main thread, where a server's tick and a client's inline tic run.
- `network.cpp` `network_ReceiveRaw`: one datagram from the receive thread's queue when it runs, else off the
  socket; `NETWORK_SetReceiveThread` starts and stops it, and `NETWORK_Destruct` stops it.
- `sv_main.cpp` `SERVER_Tick`: follows `sv_fua_receivethread` and waits with `WaitForDatagram`;
  `server_UpdateClientPing` takes `NETWORK_GetArrivalMS`.
- `network.cpp` `NETWORK_LaunchPacket`: an encoded packet is handed to `HoldOutbound` before `sendto`, and
  `network_SendHeldPackets` sends those due.
- `network.cpp` `NETWORK_GetPackets`: under conditions, `network_TakeHeldInbound` holds every waiting datagram
  and returns the oldest due one through the ordinary decode.
- `network.cpp` decodes every datagram through `network_DecodeDatagram`: a zstd frame (by its first four
  bytes) is decompressed with `g_NetworkMessage.ulMaxSize` as the hard limit, anything else goes through
  `NETWORK_DecodeIntoMessage` (`networkshared.cpp`), which never writes past the message: the raw buffer's
  size as the limit let a crafted datagram decode to 58 KB in a 21 KB buffer
  (`networkshared_decode_nettest.cpp`).

## zstd packet compression (Zandronum 3.3)

Ported from `zandronum-stable`, the 15 zstd rows of `commit-tracker/zandronum/` from `32ff6e13bd13`
to `caae2b04b3ba`. The mechanism as 3.3 wrote it: a caller that passes `bUseZStd` and a packet of at least `net_zstd_threshold` bytes goes as zstd,
compressed with the `zstddict` lump's dictionary when one loaded (the connect handshake refuses a
mismatched dictionary id with `NETWORK_ERRORCODE_WRONGZSTDDICTIONARY`); everything else, everything before
connection, and every launcher reply stays Huffman, so Doomseeker keeps querying servers. Receive detects
zstd by the frame's first four bytes.

Deviations from 3.3, each marked at its line:

- No auth-server branch anywhere: ForkUnderA removed the auth server, so no raw-bytes path exists.
- `bUseZStd` defaults to `false` in `network.h`, so the launcher and infrastructure callers needed no edit
  and an unaware caller gets the always-readable encoding.
- `NETWORK_ERRORCODE_WRONGZSTDDICTIONARY` is appended after our own error codes, not at 3.3's position, so
  none of ours renumber; a foreign peer reading the new code sees "Unknown error code", which 3.3 accepts.
- Version identity needs no change: `GAMEVER_STRING` is already the fork's own release tag and the server
  compares the full revision, so stock 3.2.1 and 3.3 peers are refused at connect, before the dictionary id
  is read, with a Huffman-readable message.
- The library is the vendored single-file zstd in `deps/zstd` (`fua_zstd`), not 3.3's `find_package`,
  so the Emscripten build compiles the same code with no `#ifdef`s.
- Upstream's `docs/zandronum-history.txt` lines are not carried: the fork does not maintain that doc past
  3.2.1.
- A packet held by `fua_net_fakeping`/`fua_net_fakeloss` is compressed before it is held, so the queues
  hold wire bytes; a server's receive thread queues raw datagrams and decompression stays on the main
  thread, so 3.3's single contexts need no lock.
