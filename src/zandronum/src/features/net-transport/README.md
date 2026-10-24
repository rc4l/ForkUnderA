# net-transport

The game's UDP socket as tests drive it: a simulated ping and packet loss, set per instance, so netgame scenarios
(`tools/fuactl` `netplay --lag/--loss`, `compareNetplay`) run at a real link's latency on loopback.

- `fua_net_fakeping` adds that many milliseconds to the instance's round trip: half on each packet it sends,
  half on each it receives (`ComputeFakeDelayMs`). Packets keep their order, as a link with a steady ping does.
- `fua_net_fakeloss` drops that percent of packets each way, from a generator seeded the same every run
  (`ComputeFakeDrop`).
- `fua_net_conditions` prints what is held each way and when the oldest is due; `net/held`,
  `net/fake_dropped`, `net/held_released` and `net/held_send_failed` count them.
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
- `netconditions.{h,cpp}`: the cvars, the two queues and `fua_net_conditions`.
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
- `network.cpp` decodes every datagram through `NETWORK_DecodeIntoMessage` (`networkshared.cpp`), which
  never writes past the message: the raw buffer's size as the limit let a crafted datagram decode to 58 KB
  in a 21 KB buffer (`networkshared_decode_nettest.cpp`).
