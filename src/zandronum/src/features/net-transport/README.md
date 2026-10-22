# net-transport

The game's UDP socket as tests drive it: a simulated ping and packet loss, set per instance, so netgame scenarios
(`tools/fuactl` `netplay --lag/--loss`, `compareNetplay`) run at a real link's latency on loopback.

- `fua_net_fakeping` adds that many milliseconds to the instance's round trip: half on each packet it sends,
  half on each it receives (`ComputeFakeDelayMs`). Packets keep their order, as a link with a steady ping does.
- `fua_net_fakeloss` drops that percent of packets each way, from a generator seeded the same every run
  (`ComputeFakeDrop`).
- `fua_net_conditions` prints what is held each way and when the oldest is due; `net/held`,
  `net/fake_dropped`, `net/held_released` and `net/held_send_failed` count them.
- The held packets keep their own monotonic clock: `I_MSTime` counts from a base set only once the timer is
  chosen, so a packet held at startup came due nine days later and every one behind it waited.

## Files

- `computation/netconditions_compute.{h,cpp}`: the delay, the drop and the held-packet queue, tested in
  `computation/netconditions_compute_test.cpp`.
- `netconditions.{h,cpp}`: the cvars, the two queues and `fua_net_conditions`.

## In-engine hooks

- `network.cpp` `NETWORK_LaunchPacket`: an encoded packet is handed to `HoldOutbound` before `sendto`, and
  `network_SendHeldPackets` sends those due.
- `network.cpp` `NETWORK_GetPackets`: under conditions, `network_TakeHeldInbound` holds every waiting datagram
  and returns the oldest due one through the ordinary decode.
- `network.cpp` decodes every datagram through `NETWORK_DecodeIntoMessage` (`networkshared.cpp`), which
  never writes past the message: the raw buffer's size as the limit let a crafted datagram decode to 58 KB
  in a 21 KB buffer (`networkshared_decode_nettest.cpp`).
