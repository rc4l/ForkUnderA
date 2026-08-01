# Attack hitbox

Every actor has a **physical box** — its `Radius` and `Height` — and that box does two different
jobs. It decides where the actor can move (what it bumps into, what doorways it fits through),
and it decides where the actor can be hit.

ZandroX splits the second job out. An actor can have an **attack hitbox** of its own,
independent of the physical box, set with [`HitRadius`](HitRadius.md) and
[`HitHeight`](HitHeight.md). A boss can be shootable well outside its collision box without
becoming too fat for its own arena; a thin decoration can be given a generous target box without
blocking the corridor it stands in.

## Availability

> **ZandroX only.** `HitRadius` and the widened attack paths do not exist in Zandronum 3.2.
> `ProjectilePassHeight` does exist there, but only projectile impact honoured it.
>
> **Works in netgames.** These are class defaults, derived identically on the client and the
> server, so no replication is involved.

## What honours the attack hitbox

| Path | Uses the attack hitbox |
|---|---|
| Projectile impact | Yes |
| Hitscan (bullets, railgun, `A_CheckLOF`, `LinePickActor`) | Yes |
| Autoaim | Yes |
| Radius / splash damage | Yes |
| Bot targeting, HUD crosshair target ID | Yes |
| **Movement and collision** | **No** — always the physical box |
| **Use / activate** (`P_UseLines`, puzzle items) | **No** — always the physical box |

Use interactions deliberately stay physical: shooting an actor should follow the attack box, but
walking up and pressing Use on it should not.

`HitHeight` is honoured by all of the above. Before this feature, hitscan ignored it entirely and
only projectile impact consulted it.

## Everything defaults to the physical box

Both properties default to `0`, which means "fall back to `Radius` / `Height`". An actor that
sets neither is hit exactly as it always was, and the feature costs it nothing. That is also why
turning attack extents on by default for hitscan changed no existing content: stock decorations
use `ProjectilePassHeight -16`, and a **negative** value is a legacy compatibility setting, not
an override — see [`HitHeight`](HitHeight.md#negative-values).

## Widening, narrowing, and bleed

**Widening** makes the actor hittable outside its collision box. To make that reachable at all,
a widened actor is linked into the blockmap using `max(Radius, HitRadius)`, so attack sweeps
visit it. Movement tests still gate on the physical `Radius`, so the wider link only means the
actor is *considered* and then rejected — it does not become harder to walk past.

Widening introduces one hazard: a hitbox that pokes through a window, a railing, or a narrow
pillar could let a shot land on a body that is genuinely behind that geometry. ZandroX guards
this with a line-of-sight check, applied **only** to actors whose hitbox actually exceeds their
physical box in some dimension. A hitbox that is smaller or equal in both dimensions cannot
bleed and skips the check entirely, so ordinary actors pay nothing for it.

That check is centre-based, which has a visible consequence: **an enlarged-hitbox actor whose
centre is behind cover is not hittable through the widened box, even where an edge of it is
exposed.** This errs toward not letting shots through walls.

**Narrowing** — a value *smaller* than the physical extent — makes attacks pass through the
margin between the attack box and the collision box. A projectile crossing that band does not
detonate, bounce, or rip; it simply carries on, which is what the older
`ProjectilePassHeight` / `ProjectilePassRadius` naming describes.

## Crouching

A crouching player's height shrinks, and a custom `HitHeight` shrinks with it by the same
factor, so the vertical attack box tracks the crouch instead of leaving a phantom target above
their head. The scaling only ever reduces: an actor taller than its own class default keeps its
full `HitHeight`.

`HitRadius` is unaffected, since crouching does not change an actor's radius.

## Seeing it

The debug overlay from [#108](https://github.com/rc4l/ZandroX/pull/108) draws collision volumes
in the 3D view, including the attack hitbox alongside the physical box — the quickest way to
confirm a value is doing what you meant.

## Implementation notes

The fallback, crouch-scaling and enlargement-gate logic is in
[`p_attackextent.h`](../../../src/zandronum/src/p_attackextent.h) as three dependency-free
helpers (`ComputeAttackExtent`, `ComputeAttackHeight`, `AttackHitboxIsEnlarged`), reached through
`AActor::GetAttackRadius()` / `GetAttackHeight()` in
[`actor.h`](../../../src/zandronum/src/actor.h).

Introduced in [#62](https://github.com/rc4l/ZandroX/pull/62).

## See also

- [HitRadius](HitRadius.md) — the horizontal extent
- [HitHeight](HitHeight.md) — the vertical extent
- `A_SetHitSize`, `APROP_HitRadius` and `APROP_HitHeight` change these at runtime from DECORATE
  and ACS ([#104](https://github.com/rc4l/ZandroX/pull/104)) — reference pages not yet written
