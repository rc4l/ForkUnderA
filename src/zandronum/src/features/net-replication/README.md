# net-replication

What a server sends its clients about the world: the `SERVERCOMMANDS_*` calls made where something changes,
and the store of sent packets a client asks to have resent.

## Owns

`sv_commands.cpp`, `sv_commands.h`, `network/packetarchive.cpp` and `network/packetarchive.h`.

## Public header

`sv_commands.h`. Calls to it from game code are counted by `net-sends-through-replication`
(`tools/boundary-rules.json`, allowances in `tools/boundary-allow.json`), which may only shrink, so a state
family moves to being sent from one place rather than gaining one more call where it changes.

## In-engine hooks

None of its own yet; the generated commands go out through `features/net-protocol`'s
`NetCommand::sendCommandToOneClient`.
