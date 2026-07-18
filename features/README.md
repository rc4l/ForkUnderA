# features/

Package-by-feature home for ZandroX's **own** additions to the Zandronum engine. Each
feature we build lives in its own subfolder with everything that belongs to it grouped
together — source, headers, tests, and a short README:

```
features/
  freeform-menu/
    README.md              # what it is, upstream origin, and the in-engine hooks it needs
    freeformmenu.cpp       # feature source (compiled into the engine via target_sources)
    freeformmenuitems.h
    freeformmenu_test.cpp  # its tests, right beside it
```

## Rules of thumb

- **Use a feature folder when the feature is mostly *new* files.** Group its source +
  tests + notes here (vertical slice / package-by-feature). New source is compiled into
  the engine with `target_sources( zdoom PRIVATE … )` (the same mechanism the MCP bridge
  overlay uses).
- **Unavoidable in-place hooks stay in the engine tree.** A port usually needs a few
  one-line edits to existing engine files (e.g. registering a menu type in
  `src/zandronum/src/menu/menu.cpp`). Those can't move; list them in the feature's README
  so the footprint is documented.
- **Tests of *vendored* engine units are not features.** A test for existing engine code
  (e.g. `vectors.h`) colocates next to that code in `src/zandronum/src/`, not here.
- **Naming:** tests are `*_test.cpp` (GoogleTest). The test glob and the 100%-coverage
  gate (`tests/coverage.sh --auto`) pick them up automatically from `features/` and the
  engine tree — no build or CI edits per test.

See the repo `AGENTS.md` and `.claude/skills/writing-tests` for how to write the tests.
