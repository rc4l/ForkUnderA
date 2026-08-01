**[Actor](https://zdoom.org/wiki/Classes:Actor)** property

`RipperLevel value`

# RipperLevel

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Ported from GZDoom/UZDoom.

## Usage

Defines this projectile's **rip tier** — how much rip power it brings against victims that
define a resistance window with [`RipLevelMin`](RipLevelMin.md) and
[`RipLevelMax`](RipLevelMax.md).

Default is `0`.

A projectile whose tier falls outside a victim's window **cannot rip that victim and explodes
on it instead**, behaving exactly as though the victim had `+DONTRIP`. Against a victim that
sets no window — the overwhelming majority of actors — the tier is ignored and the projectile
rips normally.

This turns rip immunity from a single on/off flag into a graded system: a low-tier ripper tears
through fodder but detonates on armoured enemies, while a high-tier one goes through both.

Notes:

- Unlike the rip budgets, this is **not** clamped non-negative. Negative tiers are legal and
  give you a band below the default `0` for deliberately feeble rippers.
- Readable in DECORATE expressions as `RipperLevel`, and changeable mid-flight with
  [`A_SetRipperLevel`](A_SetRipperLevel.md).
- The tier check is server-authoritative and is not broadcast.

## Examples

A tiered weapon family: the basic shot pierces only unarmoured enemies, while the charged shot
goes through everything.

```
ACTOR LightPiercer
{
    Projectile
    +RIPPER
    Damage 8
    RipperLevel 1
    // ... states ...
}

ACTOR HeavyPiercer
{
    Projectile
    +RIPPER
    Damage 20
    RipperLevel 5
    // ... states ...
}
```

Paired with a victim that resists anything under tier 3, so `LightPiercer` detonates on it while
`HeavyPiercer` rips through:

```
ACTOR ArmoredKnight : HellKnight
{
    RipLevelMin 3
}
```

A projectile that loses tier as it travels, so it pierces armour up close and detonates on it at
range:

```
    Spawn:
        SHOT A 8 Bright
        SHOT A 0 A_SetRipperLevel(1)
        SHOT A 1 Bright
        Loop
```

## See also

- [Ripping](Ripping.md) — the tier system in context
- [RipLevelMin](RipLevelMin.md), [RipLevelMax](RipLevelMax.md) — the victim's side of the check
- [A_SetRipperLevel](A_SetRipperLevel.md) — change the tier at runtime
