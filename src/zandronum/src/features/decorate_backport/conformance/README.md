# DECORATE backport conformance

`decorate_actions.txt` is a synthetic DECORATE lump that exercises the GZDoom-successor action
functions backported so **Eviternity II** (and the wider GZDoom-targeted MBF21 corpus) parses:

| Function          | Ported from | Predecessor here      | Notes |
|-------------------|-------------|-----------------------|-------|
| `A_NoiseAlert`    | uzdoom `zscript/actors/mbf21.zs` | `A_AlertMonsters` (via `P_NoiseAlert`) | no args; alerts monsters to the caller's target |
| `A_StartSound`    | uzdoom `zscript/actors/actor.zs` | `A_PlaySound` | `CHANF_OVERLAP` → this base's `CHAN_AUTO`; `CHANF_LOOP` loops |
| `A_FireProjectile`| uzdoom `zscript/actors/inventory/stateprovider.zs` | `A_FireCustomMissile` | shares `ZX_FireProjectile`; pitch sign is upstream's (added) |

Provenance SHA for all three: `uzdoom@7bfbf612d9d8197c36bb77ab171005bce521a514`.

## Why this exists

Eviternity II calls these three by their **GZDoom names directly in DECORATE**. We already resolved
the MBF21 `[CODEPTR]` (DeHackEd) path via the alias table, but DECORATE looks names up directly
against the exported action table — so calling `A_NoiseAlert` in a state block failed with
`Invalid state parameter a_noisealert` until the real function + `action native` decl existed.
This lump pins every call shape the wad uses so a dropped decl or param resurfaces immediately.

## Run

DECORATE (unlike DeHackEd) must live inside an archive, so pack the lump into a pk3 first:

    D=src/zandronum/src/features/decorate_backport/conformance
    mkdir -p /tmp/zxdeco && cp "$D/decorate_actions.txt" /tmp/zxdeco/DECORATE
    ( cd /tmp/zxdeco && zip -q -X conf.pk3 DECORATE )
    ZandroX -iwad doom2.wad -file /tmp/zxdeco/conf.pk3 +logfile out.log +quit
    grep -iE "Invalid state parameter|unknown identifier|Unknown function|has not been exported" out.log   # expect nothing

The stronger real-world check is that **Eviternity II.wad** loads to a live map:

    ZandroX -iwad doom2.wad -file "Eviternity II.wad" +map MAP01 +logfile out.log +quit
    grep -iE "Execution could not continue|Invalid state parameter" out.log   # expect nothing
