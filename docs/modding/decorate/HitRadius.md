**[Actor](https://zdoom.org/wiki/Classes:Actor)** property

`HitRadius value`

# HitRadius

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2 or GZDoom — it is the horizontal counterpart
> to the long-standing [`HitHeight`](HitHeight.md) / `ProjectilePassHeight`.
>
> **Works in netgames.** A class default, derived identically on client and server.

## Usage

Defines the radius of this actor for the purposes of being **hit**, independent of the `Radius`
it moves with.

If this is `0`, the actor's `Radius` is used. If it is positive, that value replaces `Radius`
when deciding whether an attack connects — larger to make the actor shootable outside its
collision box, smaller to let attacks pass through the margin.

Default is `0`.

This lets an actor be a bigger target than it is an obstacle. A boss can be hittable across a
96-unit span while still walking through a 128-unit corridor, because movement collision keeps
using `Radius` and is completely unaffected.

Notes:

- Like `Radius`, this is a **half-width** measured from the actor's centre, not a diameter.
- It applies to projectiles, hitscan, autoaim, splash damage, bot targeting and the HUD
  crosshair — but never to movement or to Use interactions. See
  [Attack hitbox](Attack_hitbox.md#what-honours-the-attack-hitbox) for the full table.
- A value **larger** than `Radius` also widens how the actor is linked into the blockmap, so
  attack sweeps can reach it. Movement is unaffected.
- A value larger than `Radius` additionally triggers a line-of-sight check to stop the widened
  box poking through windows and railings. That check is centre-based, so an actor whose centre
  is behind cover is not hittable through the widened box even on an exposed edge — see
  [Attack hitbox](Attack_hitbox.md#widening-narrowing-and-bleed).
- A value **smaller** than `Radius` makes attacks pass through the band between the two, without
  detonating, bouncing or ripping.
- Unlike [`HitHeight`](HitHeight.md), negative values have no special meaning here; anything
  that is not positive falls back to `Radius`.
- Crouching does not affect this, since crouching does not change an actor's radius.

**`ProjectilePassRadius` is an accepted alias** for this property. `HitRadius` is the canonical
name; the alias exists so the two extents can be written in matching style.

## Examples

A boss that is easy to hit but no harder to fight around than an Imp. Its collision box is
unchanged, so it navigates the map exactly as before, but shots land anywhere within 96 units of
its centre.

```
ACTOR BroadBoss : BaronOfHell
{
    Radius 24        // physical — unchanged, still fits normal corridors
    Height 64        // physical — unchanged
    HitRadius 96     // shootable out to 96 units from centre
    HitHeight 96     // and up to 96 units tall
}
```

A thin pole that is annoying to shoot at its true width, given a forgiving target box without
becoming an obstacle players bump into:

```
ACTOR SlenderTotem : TallGreenColumn
{
    Radius 8
    HitRadius 24
}
```

The reverse — a decoration whose collision box has to stay wide so players cannot walk through
it, but which shots should mostly ignore:

```
ACTOR WireFence : TechPillar
{
    Radius 32        // still blocks movement across its full width
    HitRadius 8      // but bullets pass through everything outside the centre
}
```

## See also

- [Attack hitbox](Attack_hitbox.md) — how the attack box relates to the physical box, and which
  attack paths honour it
- [HitHeight](HitHeight.md) — the vertical counterpart
- [Radius](https://zdoom.org/wiki/Actor_properties#Collisions_and_physics) — the physical
  movement radius this overrides for attacks only
