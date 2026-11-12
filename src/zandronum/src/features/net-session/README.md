# net-session

A netgame's session as tests measure it: where unlagged put each player for a hitscan, against where the
shooter's own client held them.

- `fua_unlagged_report [reset]` prints the shots kept as JSON (`ComputeShotLogJson`). On a server each shot
  holds the tic it ran in, the tic unlagged rewound to, whom it hit and every other player where the trace
  met them (rewound) and where they were (`restorePos`); on a client, its own shots with the other players
  where it held them. Shots are numbered from the last reset on each side, so a scenario matches them one by
  one (`tools/fuactl/scenarios/unlaggedhit.mjs`). Each shot also says whether that side traced it and the network id
  of the actor its trace hit (`traced`, `hitNetId`): a server always traces, a client only to predict puffs
  (`cl_clientsidepuffs`), so `farhitscan` can set a client's trace against its server's at 6,400 units and more.
- Rails and projectiles are not recorded: unlagged rewinds hitscan, rails and autoaim traces, and only
  `P_LineAttack`'s are noted.

## Owns

`sv_main.cpp`, `sv_main.h`, `sv_ban.cpp`, `sv_ban.h`, `sv_rcon.cpp`, `sv_rcon.h`, `sv_save.cpp`, `sv_save.h`,
`unlagged.cpp`, `unlagged.h`, `joinqueue.cpp` and `joinqueue.h`.

## Public header

`netrole.h`: `zx::net::IsServer`, `IsClient`, `IsSinglePlayer`, `IsLocalMultiplayer` and `InClientMode`, so one
header decides what each role means. Asking `NETWORK_GetState` or `NETWORK_InClientMode` outside the netcode is
counted by `net-roles-through-the-session` (`tools/boundary-rules.json`) and may only shrink; features/ already asks here.

## Files

- `computation/unlaggedreport_compute.{h,cpp}` (the shot log and its JSON) with its `_test.cpp`.
- `computation/joinupdate_compute.{h,cpp}` (`ComputeSpawnRoundsPosition`) with its `_test.cpp`.
- `computation/levelauth_compute.{h,cpp}` (`ComputeLevelAuthVerdict`) with its `_test.cpp`.
- `computation/dumpfilter_compute.{h,cpp}` (the `sv_dumppackets` command filters, counter and file name,
  ported from Zandronum 3.3's zstd dictionary-training capture; the cvar reads stay in `sv_main.cpp`'s
  `SERVER_DumpPacket` and `SERVER_ShouldDump*Command` glue, hooked from `SERVER_ParsePacket` and
  `network/netcommand.cpp`'s `sendCommandToClients`) with its `_test.cpp`.
- `computation/movebuffer_compute.{h,cpp}` (`ComputeRunsSecondMoveCmd`, which `p_tick.cpp`'s server loop asks
  before a second buffered move command, and `ComputeMoveArrival`, which `SERVER_GetPackets` counts missed and
  steady arrivals by) with its `_test.cpp`.
- `computation/floodcheck_compute.{h,cpp}` (`ComputeTimeInstancesFlood`, the chat and userinfo flood rule
  `SERVER_CheckTimeInstancesBufferForFlood` asks) with its `_test.cpp`.
- `unlaggedreport.{h,cpp}` (`NoteServerShot`, `NoteClientShot`, the command).
- `netrole.h` (the role queries, inline).
- `syncedcvars.{h,cpp}` (`SyncedCVar`, `SendSyncedCVar`, `SendSyncedCVarsTo`): a server cvar a client's own simulation
  reads registers here and goes out through `SERVERCOMMANDS_SetCVar` when it changes and to each joiner, so game
  code never sends it (`sv_fua_monsterstairs`, `features/monster-stairs`).

## In-engine hooks

- `sv_main.cpp` `SERVER_Tick` is the seam `net/server_tick` and `features/federated-server-registry`'s
  `SERVER_SERVERREGISTRY_SendServerInfo` the seam `net/launcher_reply`, both owned by the main thread.
- `sv_main.cpp` `SERVER_ConnectNewPlayer` sends the joiner every `SyncedCVar` after `sv_hostname`.
- `sv_main.cpp` `SERVER_SetupNewConnection` compares the whole revision a client sends with its own
  (`cl_main.cpp` `CLIENT_AttemptConnection` writes it), so a different build is refused every time.
- `sv_main.cpp` `SERVER_RequestClientToAuthenticate` records the map it names in `CLIENT_s::authenticatingMap`,
  and `SERVER_AuthenticateClientLevel` judges the answer with `ComputeLevelAuthVerdict`: a map changed since
  asking (an empty server holds a puked `map` until the first connection wakes its playsim, as DnD's
  `setsoftcore` does) asks the client again instead of refusing it for level authentication
  (`tools/fuactl/scenarios/netjoinmapchange.mjs`).
- `p_map.cpp` `P_LineAttack`: `NoteClientShot` before a client returns, `NoteServerShot` and `NoteClientTrace` after
  `Trace` and before `UNLAGGED_Restore`.
- `sv_main.cpp` `SERVER_SendFullUpdate` sends a joiner each hurt or dead actor's health, which it held at
  spawn health, and the exact place of each thing resting between whole units, where its spawn had rounded
  it (`netsync`'s late joiner and a dropped clip).
- `sv_commands.cpp` `SERVERCOMMANDS_SpawnThing` sends the exact spawn for a thing whole units would move
  (`ComputeSpawnRoundsPosition`), where each dropped clip sat a unit off on every client (`netmapchange`).
- `p_interaction.cpp` `AActor::Die`: a monster's death goes out with where it died and which way it faced,
  which a client simulating its last steps until a late death arrives kept wrong (`netsync`'s lossy observer).
