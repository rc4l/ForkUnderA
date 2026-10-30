# ACS function argument counts

Adapts UZDoom's "Check all ACS functions for minimum number of arguments"
(`uzdoom@258f4b6786d2da1306615582e5fb583d80f82ef3`).

An ACS function (`CALLFUNC`) gets a pointer to its arguments on the script's stack. A function that reads
past the arguments a script passed reads whatever the stack held there before, which differs from run to
run. DnD calls `SpawnForced(class, x, y, z, tid)` without its angle, so every item it swapped got an angle
from an earlier calculation, and two playbacks of one demo parted at tic 1.

- A function called with fewer arguments than it reads does nothing, returns 0 and prints
  `Called ACS function <name> (index N) with too few args: A (need B)`, as upstream does.
- An optional argument a script leaves out is its default rather than the stack past it:
  - `SpawnForced`: TID and angle 0 (upstream).
  - `SpawnSpotForced`: TID and angle 0. `SpawnSpotFacingForced`: TID 0. BCC lets a script leave these out
    (`SpawnSpotForced(str, int; int, int)`), so these two need 2 arguments rather than upstream's 4 and 3.
  - `GetMapPosition`: the current position, since older Zandronum declared it `GetCurrentMapPosition()` with
    no argument.
- The minimums are one table, `computation/acsargcount_compute.cpp`, by function index. Each is upstream's
  `MIN_ARG_COUNT`, or for Zandronum's and ZDaemon's functions the minimum `zspecial.acs` declares, and
  never above what ACC or BCC lets a script pass. `CallFunction` looks it up once before its switch: a
  direct array for indices under 256, a binary search for ZDaemon's 19620.
- A function that checks every argument it reads has no entry: `UniqueTID`, `SetHUDClipRect`,
  `SetHUDWrapWidth`, `GetWeapon`, `GetMapPosition`, `AddBot`, `RemoveBot`, the shader functions and those
  that take none.
- A new function whose case reads an argument without checking `argCount` gets an entry.

Upstream puts a `MIN_ARG_COUNT` line in each case; one table keeps the minimums in a unit-tested unit. The
`argCount` checks the table makes redundant are gone, as upstream removed them, and the cvar functions
take their minimum rather than exactly that many.

## Online

The server runs the scripts and clients run their own CLIENTSIDE ones, with the same table on each. No
command changes.

## Tests

- `AcsArgCount.*` (`computation/acsargcount_compute_test.cpp`).
- `fuactl scenarios acsargcount`: a script fills the stack past each call with its own value, then calls
  `SpawnForced` without its angle, `SpawnForced` short of its z, and `SpawnSpotForced` with BCC's two
  arguments. Each check adds a bit to a counter held to 15. A build before this reads 8: the script ran
  and every check failed.

## In-engine hooks

- `p_acs.cpp` `DLevelScript::CallFunction`: the check before the switch; the defaults of `SpawnForced`,
  `SpawnSpotForced`, `SpawnSpotFacingForced` and `GetMapPosition`; the cvar functions and the `argCount`
  checks upstream removed.
- `p_acs.cpp` `RequestScriptPuke`: its debug message prints the four arguments it sends, not the stack
  past those the script passed.
