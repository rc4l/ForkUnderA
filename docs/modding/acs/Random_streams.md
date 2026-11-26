# Named random streams in ACS

## Availability

> **ZandroX behaviour.** These functions exist only on ZandroX; elsewhere they return 0. They are ACS's
> answer to ZScript's `random[Name](min, max)` and `SetRandomSeed[Name](seed)`.

| Number | Function | Returns |
|---|---|---|
| 24070 | `fua_Random(str stream, int min, int max)` | a whole number from `min` to `max`, both included |
| 24071 | `fua_FRandom(str stream, fixed min, fixed max)` | a fixed-point number from `min` to `max`, both included |
| 24072 | `fua_SetRandomSeed(str stream, int seed)` | 1 once the stream starts over from `seed` |

## Usage

`Random()` draws from one stream the whole game shares, so every number one script takes moves what every
other script, and the next map's, will get. A named stream is a sequence of its own:

```
fua_SetRandomSeed( "dungeon", 1234 );      // the same dungeon every time for seed 1234
int rooms = fua_Random( "dungeon", 4, 9 );
fixed jitter = fua_FRandom( "sparks", -0.5, 0.5 );
```

- **Draws from a stream move no other.** `Random()`'s sequence, DECORATE's `random[...]` streams and the
  engine's own are untouched, `compat_oldrandom` or not.
- **Names ignore case,** up to 64 characters. An empty name draws nothing and returns 0.
- **Without a seed,** a stream starts from the game's random seed, as every engine stream does, and starts
  over with it at each new game.
- **A CLIENTSIDE script's streams are its own.** A clientside script and a server script naming the same
  stream draw from two different sequences, so cosmetic draws never move a gameplay roll, in single player
  too.
- **Savegames and demos keep every stream.** A loaded game goes on where the stream was; a demo plays back
  the numbers it recorded.

## Online

- **Gameplay rolls belong in server scripts,** which make them once for everyone; clients see the results.
- **A clientside stream is each client's own.** Seeded the same way, every client draws the same numbers,
  so effects built from a seed (a decal pattern, a sky's stars) look alike on every screen, a late joiner's
  included. Unseeded, each client draws its own.
- Clientside draws never reach the server's streams, so they can't make a client disagree with the server.

## Examples

Each example is a whole library: compile it with zt-bcc, add it to `LOADACS` and run its scripts with
`pukename`.

The same seed builds the same layout, whatever else draws numbers in between:

```bcs
#library "fuadung"
#import "zcommon.bcs"

script "Layout" ( int seed ) {
   fua_SetRandomSeed( "dungeon", seed );
   int rooms = fua_Random( "dungeon", 4, 9 );
   int first = fua_Random( "dungeon", 1, 100 );
   Random( 1, 100 );   // anything else drawing numbers changes nothing above
   Log( s: "seed ", d: seed, s: ": ", d: rooms, s: " rooms, first ", d: first );
}
```

<!-- example
pukename Layout 1234 => /seed 1234: \d rooms, first \d+/
-->

A loot roll on its own stream, so adding a random effect elsewhere never changes what drops:

```bcs
#library "fualoot"
#import "zcommon.bcs"

script "RollLoot" ( void ) {
   int roll = fua_Random( "loot", 1, 100 );
   if ( roll <= 10 ) {
      Log( s: "rare drop (", d: roll, s: ")" );
   }
   else {
      Log( s: "common drop (", d: roll, s: ")" );
   }
}
```

<!-- example
pukename RollLoot => /(rare|common) drop \(\d+\)/
-->

## Best practices

- **One stream per purpose.** `"loot"`, `"dungeon"`, `"sparks"`: each sequence then depends only on its own
  draws, so a change to one system never reshuffles another.
- **Seed for reproducible content,** such as a level layout from a number players share; leave cosmetic
  streams unseeded.
- **Keep the set of names fixed.** Each name is an engine stream for the rest of the session; a name built
  at run time per actor or per tic makes a new one each time.
- **Don't decide gameplay in clientside scripts.** Their streams are per client, so each player would roll
  differently.

## Implementation notes

[`src/zandronum/src/features/acs-random/`](../../../src/zandronum/src/features/acs-random/README.md).
