# features/

Package-by-feature home for ZandroX's **own** additions to the Zandronum engine. It lives
**inside the engine source tree** (`src/zandronum/src/features/`) on purpose — see the
critical build note below. Each feature gets its own subfolder grouping everything that
belongs to it: source, headers, tests, and a short README.

```
src/zandronum/src/features/
  freeform-menu/
    README.md              # what it is, upstream origin, and the in-engine hooks it needs
    freeformmenu.cpp       # feature source
    freeformmenuitems.h
    freeformmenu_test.cpp  # its tests, right beside it
```

## ⚠️ Build rule: link feature source in normal engine order (not `target_sources` at EOF)

Zandronum's DObject class registry (`IMPLEMENT_CLASS`) is a **linker section** (`__DATA,creg`)
walked from `CRegHead` (first, in `__autostart.cpp`) to `CRegTail` (last, in `zzautozend.cpp`).
Object files land in that section in **link order**. If a feature source that uses
`IMPLEMENT_CLASS` is appended via `target_sources(...)` at the end of `CMakeLists.txt`, it
links **after** `zzautozend.cpp`, so its class registration falls **outside** the walked
range and the class is silently never registered — `RUNTIME_CLASS(X)->CreateNew()` then calls
a garbage `ConstructNative` and crashes.

**Therefore:** add feature `.cpp` files to the `add_executable( zdoom … )` list **before
`zzautozend.cpp`** (that's why `features/` lives inside `src/`, so the path is src-relative and
it sits in normal source order). This is also why colocating the feature *inside* the engine
tree matters — an out-of-tree file compiled via a trailing `target_sources` breaks the registry.

## Rules of thumb

- **Use a feature folder when the feature is mostly *new* files.** Group its source + tests +
  notes here (vertical slice / package-by-feature).
- **Includes are src-relative:** `#include "features/<name>/<file>.h"` resolves via the engine's
  `.` (src) include path — no extra `include_directories` needed.
- **Unavoidable in-place hooks stay put.** A port needs a few one-line edits to existing engine
  files (e.g. registering a menu type in `menu/menu.cpp`); list them in the feature's README.
- **Tests of *vendored* engine units are not features** — colocate those next to the code
  (e.g. `vectors_test.cpp` beside `vectors.h`).
- **Naming:** tests are `*_test.cpp` (GoogleTest); the test glob + `coverage.sh --auto` pick them
  up automatically from anywhere under `src/zandronum/src/` (features included).

See `AGENTS.md` and `.claude/skills/writing-tests` for how to write the tests.
