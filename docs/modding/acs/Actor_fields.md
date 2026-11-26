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

## Implementation notes

[`src/zandronum/src/features/acs-fields/`](../../../src/zandronum/src/features/acs-fields/README.md).
