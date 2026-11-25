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

## Implementation notes

[`src/zandronum/src/features/acs-funcrefs/`](../../../src/zandronum/src/features/acs-funcrefs/README.md).
