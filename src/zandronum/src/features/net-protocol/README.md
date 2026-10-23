# net-protocol

What a server writes for its clients, seen and, for tests, held back.

- `fua_netcap_start <file>` records every command a server writes for each client, one line each
  (`tic client r|u id ext size hex`, `ComputeCaptureLine`), to `<file>.netcap` until `fua_netcap_stop`.
  `tools/fuactl` `netcap record` and `netcap diff` read and compare captures. It is an unsafe command, since
  it writes where it is told (`features/command-safety`).
- `fua_debug_netdrop "id,id/ext"` leaves the named commands unsent, and `fua_debug_unlagged_ticoffset N`
  makes a client report a server tic N behind the one it holds: faults a netgame scenario switches on to
  prove it fails (`ComputeNetDropMatches`, `ComputeReportedServerTic`).

## Owns

`network_enums.h`, `network/netcommand.cpp`, `network/netcommand.h`, `network/servercommands.cpp` and
`network/servercommands.h`, generated with the wire format from `src/zandronum/protocolspec/` and pinned by
`tools/protocol-snapshot.py`.

## Public header

`network_enums.h` (the command ids) and `commandcapture.h` and `testfaults.h`.

## Files

- `computation/commandcapture_compute.{h,cpp}` and `computation/testfaults_compute.{h,cpp}`, each with its
  `_test.cpp`.
- `commandcapture.{h,cpp}` (the capture and its commands) and `testfaults.{h,cpp}` (the two cvars).

## In-engine hooks

- `network/netcommand.cpp` `NetCommand::sendCommandToOneClient`: `DroppedForTest` first, then
  `CaptureCommand` before the command is written into the client's buffer. Commands written raw into a
  buffer (`SVCC_AUTHENTICATE`, `SVCC_MAPLOAD`, `SVCC_ERROR`) are not seen.
- `cl_commands.cpp` `clientcommand_CreateMoveCommand`: the server tic a client reports passes through
  `ReportedServerTic`.
