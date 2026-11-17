# features/acs-funcrefs

ACS function references (BCS function pointers) that name a library's function the same way from
every module and on every map. Modder reference:
[`docs/modding/acs/Function_references.md`](../../../../../docs/modding/acs/Function_references.md).

Zandronum 3.3 and UZDoom push the running module's function-table index ORed with that module's load
position. That has two problems:
- An importing module's reference to a library function is its own stub, so it never equals the
  library's reference.
- Load positions change with each map's own BEHAVIOR and per-map `loadacs` libraries, so a reference
  kept in a world or global variable can call another module's function after a level change.

| Bits | Load-position form (unchanged) | Stable form |
|---|---|---|
| 0-15 | function index | function index in its home library |
| 19 | 0 | 1 |
| 20-31 | module's load position | library's place among the ACS library lumps |

- `PCD_PUSHFUNCTION` resolves an imported function to its home library and pushes the stable form.
  A map's own BEHAVIOR keeps the load-position form, since its functions exist only on its map.
- `PCD_CALLSTACK` reads both forms; older savegames and old values keep working. A reference into a
  module the current map does not hold stops the script with a console line, where it used to run
  another module's function, or dereference a null module when the position was past the last one.
- The library number comes from the loaded files, so a server, its clients and a savegame agree on
  it as long as they load the same files.

## Hooks

- `p_acs.h`: `FBehavior::LibraryOrdinal` with `GetLibraryOrdinal`, `GetFunctionIndex` and
  `StaticModuleCount`.
- `p_acs.cpp`: the library number set when a module loads, and `PCD_PUSHFUNCTION` and
  `PCD_CALLSTACK` going through `PushFunctionRef` and `ResolveFunctionRef`.

## Tests

- `computation/funcref_compute_test.cpp`: the bit layout, both forms, the limits, the library
  numbering and lookup.
- `fuactl scenarios acsfuncref` (`tools/fuactl/fixtures/acsfuncref/`): equality between modules,
  references kept in a global across a map whose BEHAVIOR moves every library, and a reference into
  a library the next map never loads. It fails on a build without this feature.
