# A_CheckRange (two_dimension)

Adds the third `two_dimension` parameter to `A_CheckRange`, matching upstream.

- **Upstream origin:** `uzdoom@7bfbf612d9d8197c36bb77ab171005bce521a514`, `checks.zs`:
  `A_CheckRange(double distance, statelabel label, bool two_dimension = false)`. When
  `two_dimension` is true the range test measures horizontal (XY) distance only, ignoring height.
- **Why:** Eviternity II calls `A_CheckRange(2048, "Delay", true)`. The two-parameter decl rejected
  the 3-arg form with `Expected ')', got ','`, aborting the entire DECORATE load at `LoadActors`.

## In-engine hooks

- C++: `DoCheckRange()` gained a `bool twodi` (forces `dz = 0`); `A_CheckRange` reads a third
  `ACTION_PARAM_BOOL` and threads it through — in `thingdef/thingdef_codeptr.cpp`.
- DECORATE decl: `action native A_CheckRange(float distance, state label, bool twodimension = false)`
  in `wadsrc/static/actors/actor.txt` (defaulted third arg — existing 2-arg callers unaffected).

## Verify

`a_checkrange_conformance.txt` exercises both the 2-arg and 3-arg forms. Pack into a pk3 and load
(see run steps in `features/a_noisealert`).
