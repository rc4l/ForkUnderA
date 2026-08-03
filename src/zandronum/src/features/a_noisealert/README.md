# A_NoiseAlert

DECORATE-callable port of the MBF21 `A_NoiseAlert` codepointer — alerts nearby monsters (via the
sound flood-fill) to the calling actor's current target.

- **Upstream origin:** `uzdoom@7bfbf612d9d8197c36bb77ab171005bce521a514`, `mbf21.zs` — the body is
  `if (target) SoundAlert(target)`, i.e. `P_NoiseAlert(target, self)`.
- **Why:** Eviternity II (and the wider GZDoom-targeted MBF21 corpus) calls `A_NoiseAlert` **by
  name in DECORATE**. The MBF21 `[CODEPTR]` (DeHackEd) path already aliased it to
  `A_AlertMonsters`, but DECORATE resolves names against the exported action table, so a state
  block calling `A_NoiseAlert` failed with `Invalid state parameter a_noisealert` until a real
  action + `action native` decl existed.

## In-engine hooks

- C++: `DEFINE_ACTION_FUNCTION(AActor, A_NoiseAlert)` in `thingdef/thingdef_codeptr.cpp`
  (no params; server-authoritative — clients no-op, mirroring `A_AlertMonsters`).
- DECORATE decl: `action native A_NoiseAlert();` in `wadsrc/static/actors/actor.txt`.

## Verify

`a_noisealert_conformance.txt` is a synthetic DECORATE lump exercising both call shapes
(`A_NoiseAlert` and `A_NoiseAlert()`). Pack it into a pk3 and load it; a dropped decl resurfaces as
a parse error:

    mkdir -p /tmp/zx && cp a_noisealert_conformance.txt /tmp/zx/DECORATE
    ( cd /tmp/zx && zip -q -X c.pk3 DECORATE )
    ZandroX -iwad doom2.wad -file /tmp/zx/c.pk3 +logfile out.log +quit
    grep -iE "Invalid state parameter|has not been exported" out.log   # expect nothing
