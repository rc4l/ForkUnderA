# net-session

A netgame's session as tests measure it: where unlagged put each player for a hitscan, against where the
shooter's own client held them.

- `fua_unlagged_report [reset]` prints the shots kept as JSON (`ComputeShotLogJson`). On a server each shot
  holds the tic it ran in, the tic unlagged rewound to, whom it hit and every other player where the trace
  met them (rewound) and where they were (`restorePos`); on a client, its own shots with the other players
  where it held them. Shots are numbered from the last reset on each side, so a scenario matches them one by
  one (`tools/fuactl/scenarios/unlaggedhit.mjs`).
- Rails and projectiles are not recorded: unlagged rewinds hitscan, rails and autoaim traces, and only
  `P_LineAttack`'s are noted.

## Files

- `computation/unlaggedreport_compute.{h,cpp}` (the shot log and its JSON) with its `_test.cpp`.
- `unlaggedreport.{h,cpp}` (`NoteServerShot`, `NoteClientShot`, the command).

## In-engine hooks

- `p_map.cpp` `P_LineAttack`: `NoteClientShot` before a client returns, `NoteServerShot` after `Trace` and
  before `UNLAGGED_Restore`.
- `sv_main.cpp` `SERVER_SendFullUpdate` sends a joiner each hurt or dead actor's health, which it held at
  spawn health (`netsync`'s late joiner).
- `p_interaction.cpp` `AActor::Die`: a monster's death goes out with where it died and which way it faced,
  which a client simulating its last steps until a late death arrives kept wrong (`netsync`'s lossy observer).
