# Actor handles in ACS

## Availability

> **ZandroX behaviour.** These functions exist only on ZandroX. Zandronum 3.3 and UZDoom don't have
> them: there every call returns 0.

A handle names one actor without a TID. It is an `int` a script can keep in any variable, pass to
functions and compare, and it stays tied to that actor for the actor's whole life.

| Number | Function | Returns |
|---|---|---|
| 24020 | `fua_GetActorHandle(int tid [, int aaptr])` | the actor's handle, or 0 for no actor. `tid` 0 is the activator; `aaptr` follows a pointer first, as `SetActivator` does |
| 24021 | `fua_HandleValid(int h)` | whether the handle's actor still exists |
| 24022 | `fua_HandlePointer(int h, int aaptr)` | the handle of the actor that `aaptr` (`AAPTR_TARGET`, `AAPTR_MASTER`, `AAPTR_TRACER`, ...) points to from `h`'s actor, or 0 |
| 24023 | `fua_SetActivatorHandle(int h)` | 1 after making `h`'s actor the activator; 0 for a stale handle, keeping the old activator |

## Usage

```
int boss; // a handle kept between scripts

script "MarkBoss" ( void )
{
   boss = fua_GetActorHandle( 0 ); // the activator
}

script "BossTaunt" ( void )
{
   if ( fua_SetActivatorHandle( boss ) )
      PlaySound( 0, "boss/taunt" );
}
```

- **One handle per actor.** An actor gets its handle the first time a script asks, and asking again
  returns the same number. Actors no script asks about cost nothing.
- **A stale handle is 0 everywhere.** Once its actor is removed from the world (a corpse cleaned up,
  `Thing_Remove`, a projectile exploding), `fua_HandleValid` is false and every function treats the
  handle as no actor.
- **Death is not removal.** A corpse keeps its handle; check its health or flags to know whether it
  is alive. Morphing and a player respawning make a new actor, with a new handle.
- **A handle never names the wrong actor.** It carries a generation, and a slot is retired before a
  generation could repeat, so an old handle can't come to name a newer actor.
- **Map changes.** Leaving a map removes its actors, so a handle kept in a global variable reads 0 on
  the next map; a player's pawn travels with the player and keeps its handle. A new game (the `map`
  command, which also clears global variables) starts the handles over.
- **Hub maps.** Leaving a map for another of its hub keeps its actors' handles: they read 0 while you
  are away and resolve again when you come back. Leaving the hub frees them.
- **Savegames** keep every handle.
- **Limits.** Up to 1,048,575 handles can be held at once. If that is ever reached, the handles of the
  hub map visited longest ago are freed first, with a console warning; with none to free, new requests
  return 0 and print an error.

## How to use handles in clientside scripts

Each machine keeps its own handles. The server's scripts get handles from the server, and a
`CLIENTSIDE` script on a client gets handles from that client. A handle number never means anything on
another machine, so never send one across.

A client knows an actor only once the server names it, the same as with a TID. Send the actor's net
ID, which the server and its clients share, and let the client take its own handle:

```
// On the server: name a barrel to every client.
script "MarkBarrel" ( int tid )
{
   ACS_ExecuteAlways( 2, 0, GetNetID( tid, 0 ) );
}

// On each client: the barrel becomes the activator, then the client takes its own handle to it.
script 2 ( int netid ) clientside
{
   if ( !SetActivatorByNetID( netid ) )
      terminate;
   int barrel = fua_GetActorHandle( 0 );
   // Keep `barrel` in this client's variables and use it here, on this client.
}
```

- A player who joins later builds their own table; run the clientside script again for them.
- An actor that exists only on a client (a clientside effect) has no net ID, but a clientside script
  that can reach it by TID or as its activator takes a handle to it directly.

## Examples

Each example is a whole library: compile it with zt-bcc, add it to `LOADACS` and run its scripts with
`pukename`.

A boss kept by handle, with no TID to hand out or clean up. The handle reads as no actor once the boss is
removed:

```bcs
#library "fuaboss"
#import "zcommon.bcs"

int boss;

// Run as the boss itself, from its spawn state with ACS_NamedExecuteWithResult, or name it by tid.
script "TrackBoss" ( int tid ) {
   boss = fua_GetActorHandle( tid );
   Log( s: "tracking ", d: fua_HandleValid( boss ) );
}

script "BossStatus" ( void ) {
   if ( !fua_HandleValid( boss ) ) {
      Log( s: "the boss is gone" );
      terminate;
   }
   Log( s: "boss health ", d: fua_GetField( boss, "health" ) );
}
```

<!-- example
pukename TrackBoss 31 => /tracking 1/
pukename BossStatus => /boss health 60/
pukename Remove 31
pukename BossStatus => /the boss is gone/
-->

Every function that works on the activator works on a handle's actor too. Keep your own handle first, so
the script can come back to its own activator:

```bcs
#library "fuahurt"
#import "zcommon.bcs"

void Hurt( int h, int damage ) {
   int me = fua_GetActorHandle( 0 );
   if ( fua_SetActivatorHandle( h ) ) {
      Thing_Damage( 0, damage, MOD_UNKNOWN );
      fua_SetActivatorHandle( me );
   }
}

script "HurtImp" ( int tid ) {
   Hurt( fua_GetActorHandle( tid ), 10 );
   Log( s: "imp health ", d: GetActorProperty( tid, APROP_Health ), s: ", back on player ", d: PlayerNumber() );
}
```

<!-- example
pukename HurtImp 31 => /imp health 50, back on player 0/
-->

A server script naming an actor to every client, which then takes a handle of its own:

```bcs
#library "fuabarrl"
#import "zcommon.bcs"

int barrel;   // on each client, that client's own handle

script "MarkBarrel" ( int tid ) {
   ACS_NamedExecuteAlways( "TakeBarrel", 0, GetNetID( tid, 0 ) );
}

script "TakeBarrel" ( int netid ) clientside {
   if ( SetActivatorByNetID( netid ) ) {
      barrel = fua_GetActorHandle( 0 );
      Log( s: "this machine holds a handle to a ", s: GetActorClass( 0 ) );
   }
}
```

<!-- example
pukename MarkBarrel 11 => /this machine holds a handle to a ExplosiveBarrel/
-->

## Best practices

- **A handle instead of a unique TID.** There's no TID to pick, clash with another mod's or clear once the
  actor dies; keep the `int` in any variable or array.
- **Check before acting, or let 0 do it.** A stale handle makes every function do nothing and return 0, so
  `fua_HandleValid` is needed only when the script must know.
- **Dead isn't gone.** A corpse keeps its handle until it is removed: read its health (or `FQ_ALIVE` in a
  [query](Actor_queries.md)) to know whether it lives.
- **Give the activator back.** Take `fua_GetActorHandle( 0 )` before `fua_SetActivatorHandle`, and set it
  back after, as `Hurt` does, when later calls mean the script's own activator.
- **Expect 0 after a map change.** A handle kept in a global variable names nothing on the next map unless
  it's a player's pawn or the map is in the same hub.

## Online

- **Each machine has its own handles.** A number from the server means nothing on a client, so send the
  actor's net id and let the clientside script take its own handle, as above.
- **A client's handle follows the server.** When the server removes an actor, every client's handle to it
  goes stale too, once the removal reaches it.
- **A player who joins later** builds their own handles: run the clientside script again for them, such as
  from an `ENTER` script.

## Implementation notes

[`src/zandronum/src/features/acs-handles/`](../../../src/zandronum/src/features/acs-handles/README.md).
