# latency

How long input and server packets take to reach the screen, as `features/telemetry` timing counters.

- `latency/input_to_frame_ms`: from the first input event no frame has shown yet to the end of the first
  frame after a tic's command carried it.
- `latency/packet_to_frame_ms`: on a client, from a server packet applied to the end of the frame that
  draws it.
- Each is one sample a frame, so read its p99 and max (`stats latency/`); frames with nothing to show count
  as 0 in the mean.

## Files

- `computation/latency_compute.{h,cpp}` (the stamp: noted, armed, taken) with its `_test.cpp`.
- `latency.{h,cpp}` (the counters and the four notes).

## In-engine hooks

- `d_main.cpp` `D_ProcessEvents` (`NoteInput`) and the main loop's frame boundary (`FrameShown`).
- `g_game.cpp` `G_BuildTiccmd` (`NoteTicBuilt`).
- `cl_main.cpp` `CLIENT_ParsePacket` (`NotePacketApplied`).
