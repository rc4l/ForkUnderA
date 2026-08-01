**[Actor](https://zdoom.org/wiki/Classes:Actor)**

`void A_SetRipMin (int minimum)`

# A_SetRipMin

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Ported from GZDoom/UZDoom.
>
> **Server-authoritative and not broadcast** — see
> [A_SetRipperLevel](A_SetRipperLevel.md#netplay).

## Usage

Sets the calling actor's [`RipLevelMin`](RipLevelMin.md) — the lowest
[rip tier](Ripping.md#tiered-ripping) that may pierce it — at runtime.

This is the victim-side setter, and it is what makes rip resistance dynamic: armour that breaks
partway through a fight, a shield that raises resistance while it is up, or a boss phase that
becomes pierceable once staggered.

The value is not clamped, so negative bounds are legal. `0` disables the lower bound entirely.

### Parameters

- `int minimum`

  The new lower bound. There is no "keep current" sentinel — the value is applied as given.

## Examples

An armoured knight whose plating fails at low health, dropping the bound to `0` so weapons that
previously exploded on it start piercing:

```
ACTOR BreakableKnight : HellKnight
{
    Health 800
    RipLevelMin 3
    States
    {
    Pain:
        BOS2 H 2
        BOS2 H 0 A_JumpIf(health < 300, "Breached")
        BOS2 H 2 A_Pain
        Goto See

    Breached:
        BOS2 H 4 A_SetRipMin(0)
        BOS2 H 4 A_Pain
        Goto See
    }
}
```

A monster that raises its guard while attacking and lowers it afterwards, so its wind-up is the
window to use piercing weapons:

```
    Missile:
        BOS2 E 0 A_SetRipMin(6)
        BOS2 EF 6 A_FaceTarget
        BOS2 G 12 A_BruisAttack
        BOS2 G 0 A_SetRipMin(0)
        Goto See
```

## See also

- [RipLevelMin](RipLevelMin.md) — the property this sets
- [A_SetRipMax](A_SetRipMax.md) — the upper bound
- [A_SetRipperLevel](A_SetRipperLevel.md) — the projectile-side tier
- [Ripping](Ripping.md) — the tier system in context
