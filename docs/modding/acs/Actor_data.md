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
fua_SetActorData( h, "owner", fua_GetActorHandle( AAPTR_TARGET ) ); // a handle is a value too
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

## Implementation notes

[`src/zandronum/src/features/acs-handles/`](../../../src/zandronum/src/features/acs-handles/README.md).
