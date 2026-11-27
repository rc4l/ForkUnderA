# Actor fields by name in ACS

## Availability

> **ZandroX behaviour.** These functions exist only on ZandroX; elsewhere they return 0. They take an
> [actor handle](Actor_handles.md).

| Number | Function | Returns |
|---|---|---|
| 24030 | `fua_GetField(int h, str name [, int index])` | the field's value, or 0 for an unknown field, index or handle |
| 24031 | `fua_SetField(int h, str name, int value [, int index])` | 1 after writing a `user_` variable, 0 for anything else |

## Usage

```
int h = fua_GetActorHandle( 0 );
int health = fua_GetField( h, "health" );
int special = fua_GetField( h, "args", 2 ); // the third special argument
fua_SetField( h, "user_combo", fua_GetField( h, "user_combo" ) + 1 );
```

- **Any field DECORATE knows:** the native ones `actor.txt` declares (`health`, `x`, `y`, `z`, `velx`,
  `angle`, `pitch`, `args`, `tid`, `special`, `radius`, `height`, `scaleX` and the rest) and every
  `user_` variable and array. Names ignore case.
- **Units as the existing getters give them.** Integers come back as they are, positions and sizes as
  fixed point, angles with a full turn as 1.0, as `GetActorAngle` gives them.
- **Past ±32,767 map units** a position keeps only its low 32 bits, exactly as `GetActorX` does.
- **Writes reach `user_` variables only,** as `SetUserVariable` allows. Like `SetUserVariable`, a write
  stays on the machine that made it: a server's write doesn't reach clients. Native fields are changed
  through the existing functions after `fua_SetActivatorHandle`.

## Examples

Each example is a whole library: compile it with zt-bcc, add it to `LOADACS` and run its scripts with
`pukename`.

Reading what `GetActorProperty` doesn't reach, with positions and sizes in fixed point:

```bcs
#library "fuainsp"
#import "zcommon.bcs"

script "Inspect" ( int tid ) {
   int h = fua_GetActorHandle( tid );
   Log( s: "health ", d: fua_GetField( h, "health" ), s: ", radius ", d: fua_GetField( h, "radius" ) >> 16,
      s: ", height ", d: fua_GetField( h, "height" ) >> 16, s: ", special arg 0 ", d: fua_GetField( h, "args", 0 ) );
}
```

<!-- example
pukename Inspect 31 => /health 60, radius 20, height 56, special arg 0 0/
-->

A `user_` counter, kept on actors whose DECORATE declares `var int user_kills;`, and refused on any other:

```bcs
#library "fuakills"
#import "zcommon.bcs"

script "CountKill" ( int tid ) {
   int h = fua_GetActorHandle( tid );
   if ( fua_SetField( h, "user_kills", fua_GetField( h, "user_kills" ) + 1 ) ) {
      Log( s: "kills ", d: fua_GetField( h, "user_kills" ) );
   }
   else {
      Log( s: "no user_kills here" );
   }
}
```

<!-- example
pukename CountKill 21 => /kills 1/
pukename CountKill 21 => /kills 2/
pukename CountKill 31 => /no user_kills here/
-->

## Best practices

- **Name fields as DECORATE does,** in any case. An unknown name reads 0, so check a name once against a
  value you know when a field might not exist in every engine version.
- **Whole units are `>> 16`.** Positions and sizes are fixed point; outside a `strict namespace`,
  `( int ) x` keeps their bits (see [Vectors](Vectors.md#best-practices)).
- **Write only your own `user_` variables,** and change native fields through their setters
  (`SetActorProperty`, `SetActorPosition`, ...) after `fua_SetActivatorHandle`, which send the change to
  clients where the engine does.
- **For values on actors you don't own,** such as another mod's monsters, use
  [actor data](Actor_data.md): it needs no DECORATE change.

## Online

Reads give each machine's own copy of the actor. A `user_` write stays on the machine that made it, as
`SetUserVariable`'s does, so a server's write doesn't reach clients: send the value to a clientside
script, or keep it on the server.

## Implementation notes

[`src/zandronum/src/features/acs-fields/`](../../../src/zandronum/src/features/acs-fields/README.md).
