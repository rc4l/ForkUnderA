# Function references in ACS

## Availability

> **ZandroX behaviour.** A mod needs no new syntax: this is how ZandroX runs the function
> references [zt-bcc](https://github.com/Doruk-Aksoy/bcc_fixed) and other BCS compilers already
> make (`int function( void ) f = MyFunction;`).

## Usage

On ZandroX:

- **References compare equal between modules.** A reference one library takes to another library's
  function equals the one that library takes to it. A library can find or remove a callback another
  module registered.
- **References last across maps.** A reference kept in a world or global variable still calls the
  same library function after a level change.
- **A missing library stops the script.** A kept reference into a library the new map doesn't load
  (one loaded through a map's own `#import` or MAPINFO `loadacs`) stops the calling script with a
  console message, instead of calling some other module's function.

On Zandronum 3.3, UZDoom and older ZandroX builds, neither holds. An imported function's reference
never equals the library's own, and a reference kept across a level change can call the wrong
function. zt-bcc warns about both when it builds for those engines. A mod that also runs on stock
servers should not depend on either.

A reference to a function in a map's own BEHAVIOR works only on that map, as before.

## Examples

Each example is a whole library: compile it with zt-bcc (`-target fua`), add it to `LOADACS` and run its
scripts with `pukename`.

A registry one library keeps, which any other module adds its handlers to. Since references compare
equal between modules, the registry can refuse a handler it already holds.

```bcs
#library "fuaevts"
#import "zcommon.bcs"

int function( int )? handlers[ 8 ];
int count;

// Adds a kill handler once; false when it was there already or the list is full.
bool OnKill( int function( int )? f ) {
   for ( int i = 0; i < count; ++i ) {
      if ( handlers[ i ] == f ) {
         return false;
      }
   }
   if ( count == 8 ) {
      return false;
   }
   handlers[ count++ ] = f;
   return true;
}

// Call from a monster's death state: every handler sees the dead monster's tid.
script "FireKill" ( int tid ) {
   int total = 0;
   for ( int i = 0; i < count; ++i ) {
      total += handlers[ i ]( tid );
   }
   Log( s: "kill handlers ", d: count, s: ", points ", d: total );
}
```

<!-- example: compile -->

```bcs
#library "fuascore"
#import "zcommon.bcs"
#import "fuaevts.bcs"

int Points( int tid ) {
   return tid == 31 ? 100 : 10;
}

script "RegisterScore" ( void ) {
   bool first = OnKill( Points );
   bool again = OnKill( Points );
   Log( s: "registered ", d: first, s: ", again ", d: again );
}
```

<!-- example
pukename RegisterScore => /registered 1, again 0/
pukename FireKill 31 => /kill handlers 1, points 100/
-->

A reference kept in a global variable still calls the same function on the next map:

```bcs
#library "fuakeep"
#import "zcommon.bcs"

global int function( void )? 3:gOnEnter;

int Greet( void ) {
   Log( s: "welcome back" );
   return 1;
}

script "KeepGreeter" ( void ) {
   gOnEnter = Greet;
}

// Run from the next map's ENTER script, for instance.
script "CallGreeter" ( void ) {
   if ( gOnEnter ) {
      gOnEnter();
   }
   else {
      Log( s: "no greeter" );
   }
}
```

<!-- example
pukename CallGreeter => /no greeter/
pukename KeepGreeter
@changemap MAP01
pukename CallGreeter => /welcome back/
-->

## Best practices

- **Check a reference before calling it.** Calling a null reference stops the script; `if ( f )` costs
  one instruction.
- **Keep references to library functions.** A library loaded by `LOADACS` is on every map, so its
  references stay good; a reference into a map's own BEHAVIOR, or into a library only some maps load,
  stops the script when called on a map without it.
- **One registry, many modules.** Put the list in one library every other module `#import`s, as above,
  so there is one copy of it.
- **For mods that also run on stock servers,** build with `-target any` or `zandronum` and don't compare
  references to imported functions or call ones kept across a map change; zt-bcc warns where you do.

## Online

Clientside scripts take, compare, keep and call references as server scripts do, across map changes too.
Each machine keeps its own variables, so a reference a server script stores isn't on any client: a
clientside script that needs one takes it itself.

## Implementation notes

[`src/zandronum/src/features/acs-funcrefs/`](../../../src/zandronum/src/features/acs-funcrefs/README.md).
