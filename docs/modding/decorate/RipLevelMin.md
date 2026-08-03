**[Actor](https://zdoom.org/wiki/Classes:Actor)** property

`RipLevelMin value`

# RipLevelMin

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Ported from GZDoom/UZDoom.

## Usage

Defines the **lowest** [`RipperLevel`](RipperLevel.md) that may rip this actor. Rippers below it
cannot pierce this actor and explode on it instead.

If this is `0`, there is no lower bound. Default is `0`.

This is the armour half of [tiered ripping](Ripping.md#tiered-ripping): it makes an actor immune
to weak rippers while still vulnerable to strong ones, where `+DONTRIP` could only refuse all of
them.

Notes:

- A rejected ripper behaves exactly as though this actor had `+DONTRIP` — it detonates on
  contact rather than passing through.
- Not clamped non-negative, so a negative bound is legal and only excludes rippers below it.
- Combines with [`RipLevelMax`](RipLevelMax.md) to define a closed window. Setting only this one
  leaves the window open at the top.
- Readable in DECORATE expressions as `RipLevelMin`, and changeable at runtime with
  [`A_SetRipMin`](A_SetRipMin.md).

## Examples

An armoured enemy that shrugs off light piercing weapons — anything below tier 3 blows up on its
plating instead of going through.

```
ACTOR ArmoredKnight : HellKnight
{
    Health 800
    RipLevelMin 3
}
```

An enemy whose armour breaks partway through the fight, dropping its resistance so previously
useless weapons start piercing it:

```
    Pain:
        BOS2 H 2
        BOS2 H 0 A_JumpIf(health < 300, "Breached")
        BOS2 H 2 A_Pain
        Goto See

    Breached:
        BOS2 H 4 A_SetRipMin(0)
        BOS2 H 4 A_Pain
        Goto See
```

## See also

- [Ripping](Ripping.md) — the tier system in context
- [RipLevelMax](RipLevelMax.md) — the upper bound
- [RipperLevel](RipperLevel.md) — the projectile's side of the check
- [A_SetRipMin](A_SetRipMin.md) — change the bound at runtime
