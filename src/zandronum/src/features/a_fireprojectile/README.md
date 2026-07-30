# A_FireProjectile

DECORATE-callable port of GZDoom's `A_FireProjectile`, the weapon successor to
`A_FireCustomMissile`.

- **Upstream origin:** `uzdoom@7bfbf612d9d8197c36bb77ab171005bce521a514`, `stateprovider.zs`:
  `A_FireProjectile(class<Actor> missiletype, double angle = 0, bool useammo = true,
  double spawnofs_xy = 0, double spawnheight = 0, int flags = 0, double pitch = 0)`. Upstream made
  `A_FireCustomMissile` a deprecated wrapper that calls `A_FireProjectile(..., -pitch)`.
- **Why:** Eviternity II fires its rail shots with `A_FireProjectile("_Rail Shot N", 0.0, 0)`.

## Shared body

`A_FireProjectile` and `A_FireCustomMissile` share a single `ZX_FireProjectile` helper (in
`thingdef/thingdef_codeptr.cpp`), mirroring upstream's wrapper relationship. `A_FireCustomMissile`
passes a **negated** pitch so its aim stays bit-for-bit unchanged; `A_FireProjectile` uses the
upstream (added) pitch sign.

## Flags (uzdoom `FPF_*`, `constants.zs`) — all honoured

| Flag | Value | Behavior |
|------|-------|----------|
| `FPF_AIMATANGLE`         | 1 | offset the shot by `angle` instead of aiming straight ahead |
| `FPF_TRANSFERTRANSLATION`| 2 | projectile inherits the shooter's `Translation` (replicated in MP via `SERVERCOMMANDS_SetThingTranslation`) |
| `FPF_NOAUTOAIM`          | 4 | keep the exact pitch — maps to `P_SpawnPlayerMissile`'s `nofreeaim` |

## In-engine hooks

- C++: `ZX_FireProjectile` helper + `DEFINE_ACTION_FUNCTION_PARAMS(AActor, A_FireProjectile)` and
  the reworked `A_FireCustomMissile` in `thingdef/thingdef_codeptr.cpp`; `A_FireCustomMissileHelper`
  gained `NoAutoAim`/`TransferTranslation` params.
- DECORATE decl: `action native A_FireProjectile(...)` in `wadsrc/static/actors/shared/inventory.txt`.
- Constants: `FPF_*` enum in `wadsrc/static/actors/constants.txt`.

## Verify

`a_fireprojectile_conformance.txt` exercises the plain, `FPF_AIMATANGLE`, `FPF_NOAUTOAIM`, and
`FPF_TRANSFERTRANSLATION` call shapes. Pack into a pk3 and load (see run steps in
`features/a_noisealert`).
