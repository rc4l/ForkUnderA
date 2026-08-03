**[Actor](https://zdoom.org/wiki/Classes:Actor)** property

`HitHeight value`

# HitHeight

## Availability

> **`HitHeight` is a ZandroX name** for a property that already existed as
> `ProjectilePassHeight`. The two are interchangeable.
>
> **What is new in ZandroX** is how widely it is honoured. In Zandronum 3.2 only projectile
> impact consulted it; here hitscan, autoaim, splash damage, bot targeting and the HUD crosshair
> all do, and it scales with crouching.
>
> **Works in netgames.** A class default, derived identically on client and server.

## Usage

Defines the height of this actor for the purposes of being **hit**, independent of the `Height`
it moves with.

If this is `0`, the actor's `Height` is used. If it is positive, that value replaces `Height`
when deciding whether an attack connects — attacks above it pass over the actor. If it is
negative, see [below](#negative-values).

Default is `0`.

Notes:

- It applies to projectiles, hitscan, autoaim, splash damage, bot targeting and the HUD
  crosshair — but never to movement or to Use interactions. See
  [Attack hitbox](Attack_hitbox.md#what-honours-the-attack-hitbox) for the full table.
- **Hitscan honours this now and did not before.** A mod that set `ProjectilePassHeight` to make
  rockets fly over something will find bullets doing the same in ZandroX.
- A value **larger** than `Height` triggers a line-of-sight check to stop the taller box poking
  through geometry — see
  [Attack hitbox](Attack_hitbox.md#widening-narrowing-and-bleed).
- A value **smaller** than `Height` makes attacks above it pass through without detonating,
  bouncing or ripping.
- A crouching player's positive `HitHeight` shrinks by the same factor as their physical height,
  so the attack box follows the crouch. The scaling only reduces — an actor taller than its class
  default keeps its full value.

**`ProjectilePassHeight` is an accepted alias** for this property, and remains the name stock
content uses.

## Negative values

A negative value is **not** an override. It is a legacy compatibility setting: the absolute
value is used as the hit height *only* when the "missile clip" compatibility option is enabled,
and otherwise the actor's real `Height` applies.

This is why stock Doom decorations carry `ProjectilePassHeight -16` and are nonetheless hit at
their full height under default settings — and why enabling attack extents for hitscan changed
nothing about existing content. If you want an unconditional override, use a **positive** value.

Note that the crouch scaling above applies only to positive values; the legacy negative path is
untouched.

## Examples

A hanging corpse that shots should pass under, so players can shoot the monster standing behind
it. The physical height is left alone so the decoration still hangs where it should.

```
ACTOR ShootThroughCorpse : HangingCorpse
{
    Height 64        // physical — unchanged
    HitHeight 16     // only the bottom 16 units stop a shot
}
```

A tall boss that should be hittable across its whole visible extent, paired with
[`HitRadius`](HitRadius.md) so the target box grows in both dimensions at once:

```
ACTOR ToweringFiend : BaronOfHell
{
    Radius 24
    Height 64
    HitRadius 96
    HitHeight 96
}
```

The legacy form, as stock decorations use it — hit at full `Height` normally, and at 16 units
only when the missile-clip compatibility option is on:

```
    ProjectilePassHeight -16
```

## See also

- [Attack hitbox](Attack_hitbox.md) — how the attack box relates to the physical box, and which
  attack paths honour it
- [HitRadius](HitRadius.md) — the horizontal counterpart
- [Height](https://zdoom.org/wiki/Actor_properties#Collisions_and_physics) — the physical
  movement height this overrides for attacks only
