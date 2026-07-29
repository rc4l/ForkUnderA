# A_SpawnProjectile

Exposes `A_SpawnProjectile` to DECORATE — upstream's corrected successor to `A_CustomMissile`.

Mods call it by name (Eviternity II does, at `DECORATE:1294`), and because DECORATE aborts parsing
on an unknown action, its absence rejected the *whole file* rather than the one state — reported as
`Invalid state parameter a_spawnprojectile` (#95).

## Why this is not an alias

The obvious implementation — point `A_SpawnProjectile` at the existing `A_CustomMissile` — is
wrong, and silently so.

Upstream renamed the function *and fixed a bug in the same move*. `A_CustomMissile` computes the
missile's vertical velocity from an inverted base pitch; `A_SpawnProjectile` computes it correctly.
Upstream kept the broken arithmetic reachable behind `CMF_BADPITCH` so existing mods keep aiming
where they always did, and their deprecated `A_CustomMissile` wrapper is literally
`A_SpawnProjectile(..., flags|CMF_BADPITCH, ...)` (`zscript/compatibility.zs:150`). Their comment on
that branch:

> Replicate the bogus calculation from A_CustomMissile in its entirety. This tried to do the right
> thing but in the process effectively inverted the base pitch.

Our `A_CustomMissile` is the ZDoom-era original — i.e. it *is* the bogus version. Aliasing would
have given every mod written against the fixed function the inverted aim, in the one case
(`CMF_OFFSETPITCH` / `CMF_ABSOLUTEPITCH` with a non-level shot) least likely to be noticed in a
smoke test.

So the two share one body and differ by exactly two things:

| | `A_CustomMissile` | `A_SpawnProjectile` |
|---|---|---|
| pitch arithmetic | `CMF_BADPITCH` forced on | corrected; the flag is stripped |
| aims at | always `target` | `ptr` parameter (default `AAPTR_TARGET`) |

`CMF_BADPITCH` is deliberately **not** exposed as a DECORATE constant: it is forced on one path and
stripped on the other, so a mod setting it could only be misled.

## Sign conventions

Upstream is float and defines `TVector3::Pitch()` as `-VecToAngle(XY().Length(), Z)`
(`vectors.h:1563`) — negated relative to our `R_PointToAngle2(0, 0, xyLength, velz)`. Writing `A`
for our unnegated angle, upstream's two branches become, in our convention:

    bad:  pitch = pitch + A;  velz = +FixedMul(sin, Speed)   <- what A_CustomMissile always did
    good: pitch = pitch - A;  velz = -FixedMul(sin, Speed)

This was checked against upstream's source rather than inferred; getting it backwards would invert
the aim of every mod using the new name.

## Layout

- `computation/spawnprojectile_compute.{h,cpp}` — the pure pitch/velocity decision, header-pure
  (raw `int64_t` fixed bits, `uint32_t` angle bits), with `_test.cpp` alongside.
- In-place engine edits:
  - `thingdef/thingdef_codeptr.cpp` — `A_CustomMissile` refactored into a shared
    `ZX_SpawnProjectile()`; both action functions now call it. Adds `CMF_BADPITCH`, `static_assert`s
    the flag values against the compute unit, and resolves the aim target through `COPY_AAPTR`.
  - `wadsrc/static/actors/actor.txt` — the `action native A_SpawnProjectile(...)` declaration.
  - `wadsrc/static/actors/constants.txt` — comment only; the `CMF_*` block now names both actions.

## Provenance

Adapted from `uzdoom@81fd6c819fd5a6b71a946ba6e95cb67a76e4cac7`
(`src/playsim/p_actionfunctions.cpp`, `A_SpawnProjectile`). The upstream file is VM-tainted
(`tools/backport-scout.sh` reports TAINTED; scriptified from `948ef62fcdbf…` onward), so this is a
hand back-translation into our fixed-point form, not a cherry-pick — per
`docs/zscript-insulation.md` and the "no post-2016 `thingdef/*` cherry-picks" rule.

## Verification

- `spawnprojectile_compute_test.cpp` pins both formulas, including that they *disagree* whenever an
  offset is applied and *agree* when it is zero (the flat-shot case that hid the bug).
- Not yet verified in-game by a human. The engine-side wiring — `COPY_AAPTR` resolution, the
  `ptr` parameter, and the `A_CustomMissile` no-change guarantee — wants a manual E2E pass with a
  mod that uses `CMF_OFFSETPITCH`, per the upstream-port skill's gate 5.
