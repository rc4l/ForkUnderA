# Values stored on an actor in ACS

## Availability

> **ZandroX behaviour.** These functions exist only on ZandroX; elsewhere they return 0. They take an
> [actor handle](Actor_handles.md).

| Number | Function | Returns |
|---|---|---|
| 24032 | `fua_SetActorData(int h, str key, int value)` | 1 once stored, 0 for no actor or past 64 keys |
| 24033 | `fua_GetActorData(int h, str key [, int default])` | the value, or `default` (0 if left out) when unset |
| 24034 | `fua_HasActorData(int h, str key)` | 1 when the key is set |
| 24035 | `fua_ClearActorData(int h [, str key])` | nothing; removes the key, or every key when left out |

## Usage

```
int h = fua_GetActorHandle( 0 );
fua_SetActorData( h, "combo", fua_GetActorData( h, "combo" ) + 1 );
fua_SetActorData( h, "owner", fua_GetActorHandle( 0, AAPTR_TARGET ) ); // a handle is a value too
if( fua_HasActorData( h, "marked" ) )
    fua_ClearActorData( h, "marked" );
```

- **Any actor, no DECORATE change:** unlike `user_` variables, a key needs no declaration, so scripts can
  tag monsters of any mod.
- **Integers and fixed point only,** up to **64 keys an actor.** A 65th key isn't stored and the console
  warns once a map; an existing key can still be changed. Keys ignore case.
- **Values live as long as their actor:** removing the actor removes them, and a new actor never inherits
  them. They come back with a savegame and with a hub map the player returns to.
- **Each machine keeps its own,** as with handles: a server's values don't reach clients, and a
  `CLIENTSIDE` script's stay on its client.

## Examples

Each example is a whole library: compile it with zt-bcc, add it to `LOADACS` and run its scripts with
`pukename`.

A count kept on any monster, of any mod, with nothing added to its DECORATE:

```bcs
#library "fuatag"
#import "zcommon.bcs"

script "TagImp" ( int tid ) {
   int h = fua_GetActorHandle( tid );
   int n = fua_GetActorData( h, "tagged" ) + 1;
   fua_SetActorData( h, "tagged", n );
   Log( s: "tagged ", d: n, s: " times" );
}

script "UntagImp" ( int tid ) {
   fua_ClearActorData( fua_GetActorHandle( tid ), "tagged" );
}
```

<!-- example
pukename TagImp 31 => /tagged 1 times/
pukename TagImp 31 => /tagged 2 times/
pukename TagImp 32 => /tagged 1 times/
pukename UntagImp 31
pukename TagImp 31 => /tagged 1 times/
-->

A handle stored as a value links one actor to another: here, who claimed a monster, so the kill can be
credited to them later:

```bcs
#library "fuaclaim"
#import "zcommon.bcs"

script "Claim" ( int tid ) {
   fua_SetActorData( fua_GetActorHandle( tid ), "owner", fua_GetActorHandle( 0 ) );
}

script "WhoOwns" ( int tid ) {
   int owner = fua_GetActorData( fua_GetActorHandle( tid ), "owner" );
   if ( fua_SetActivatorHandle( owner ) ) {
      Log( s: "owned by player ", d: PlayerNumber() );
   }
   else {
      Log( s: "unclaimed" );
   }
}
```

<!-- example
pukename WhoOwns 31 => /unclaimed/
pukename Claim 31
pukename WhoOwns 31 => /owned by player 0/
-->

## Best practices

- **Use a fixed set of keys.** Keys are names in the engine's name table, which never shrinks, so a key
  built at run time (`StrParam( s: "hit", d: i )`) grows it for the rest of the session. Keep per-index
  values in an array the key names instead.
- **A default says "unset".** `fua_GetActorData( h, key, -1 )` tells an unset key from a stored 0 without
  a `fua_HasActorData` call.
- **Values outlive nothing.** They go with their actor, so there's nothing to clean up when a monster is
  removed, and nothing to find on the next actor that takes its place.
- **Mind the 64 keys** an actor holds: a mod tagging monsters with many keys should pack small values into
  one key's bits.

## Online

Values are per machine. A server script's values stay on the server, a clientside script's on its client,
and a late joiner starts with none. Keep anything players must all agree on (scores, ownership a kill
credit depends on) in server scripts, and send only what a client must show.

## Implementation notes

[`src/zandronum/src/features/acs-handles/`](../../../src/zandronum/src/features/acs-handles/README.md).
