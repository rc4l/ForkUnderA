**[Actor](https://zdoom.org/wiki/Classes:Actor)** property

`RipperDamageFactor value`

# RipperDamageFactor

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2.

## Usage

Defines how much damage a [rip](Ripping.md) loses on each repeat hit against the **same**
victim.

The factor compounds: hit *n* against a given victim deals `Damage * factor^(n-1)`. The first
hit is always at full damage, the second is multiplied once, the third twice, and so on. The
count is per victim, so moving on to a fresh enemy starts again at full damage.

Default is `1.0` — no falloff, which is what every ripper did before this property existed.

A factor below `1.0` makes a ripper punishing on contact but weak if it lingers, which is the
usual reason to reach for it: it rewards sweeping through a crowd over parking inside one enemy.
A factor above `1.0` does the opposite and ramps damage up the longer the projectile stays
inside something.

Notes:

- Negative values are clamped to `0` when parsed. A factor of exactly `0` means every hit after
  the first deals nothing.
- The factor is additionally clamped to a maximum of `128.0` before compounding.
- Scaled damage saturates at `4194304` rather than overflowing, and **floors at `0`** once
  falloff has decayed it past a whole point of damage — a long-lived ripper with a small factor
  eventually deals nothing rather than rounding up to 1.
- Setting this to anything other than `1.0` makes the projectile carry a per-victim ledger, the
  same one [`RipperCount`](RipperCount.md) uses.

## Examples

A shredder whose first bite hurts and whose later ones taper off, so standing in it is survivable
but being caught by its leading edge is not.

```
ACTOR Shredder
{
    Radius 12
    Height 12
    Speed 30
    Damage 20
    Projectile
    +RIPPER
    RipperDamageFactor 0.6
    States
    {
    Spawn:
        SHRD AB 2 Bright
        Loop
    Death:
        SHRD C 5 Bright
        Stop
    }
}
```

With `Damage 20` and a factor of `0.6`, successive hits on one victim deal 20, 12, 7, 4, 2, 1,
then 0 — the projectile keeps flying but stops mattering against that target.

A drill that builds up instead, becoming more dangerous the longer it stays embedded:

```
    +RIPPER
    RipperDamageFactor 1.5
    RipperCount 6
```

The `RipperCount` matters here: without a cap, a factor above `1.0` grows without bound until it
saturates.

## See also

- [Ripping](Ripping.md) — the rip system and its rounding limits
- [RipperCount](RipperCount.md) — cap how many times one victim can be hit
- [RipperMaxDamage](RipperMaxDamage.md) — cap total damage instead of scaling it
