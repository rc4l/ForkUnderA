**[Actor](https://zdoom.org/wiki/Classes:Actor)**

`void A_SetRipMax (int maximum)`

# A_SetRipMax

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Ported from GZDoom/UZDoom.
>
> **Server-authoritative and not broadcast** — see
> [A_SetRipperLevel](A_SetRipperLevel.md#netplay).

## Usage

Sets the calling actor's [`RipLevelMax`](RipLevelMax.md) — the highest
[rip tier](Ripping.md#tiered-ripping) that may pierce it — at runtime.

The upper bound resists *strong* rippers rather than weak ones, so this setter is for actors
whose material changes state: a gel that solidifies, a swarm that packs tight enough to detonate
what used to thread through it.

The value is not clamped, so negative bounds are legal. `0` disables the upper bound entirely.

### Parameters

- `int maximum`

  The new upper bound. There is no "keep current" sentinel — the value is applied as given.

## Examples

A blob that hardens when damaged, so heavy rippers that once tore through it start exploding on
its surface instead:

```
ACTOR HardeningBlob
{
    Health 400
    Radius 24
    Height 48
    Monster
    RipLevelMax 8
    States
    {
    Pain:
        BLOB E 4
        BLOB E 4 A_SetRipMax(2)
        Goto See
    }
}
```

Clearing both bounds at once, to make an actor freely pierceable by any tier:

```
    BLOB A 0 A_SetRipMin(0)
    BLOB A 0 A_SetRipMax(0)
```

## See also

- [RipLevelMax](RipLevelMax.md) — the property this sets
- [A_SetRipMin](A_SetRipMin.md) — the lower bound
- [A_SetRipperLevel](A_SetRipperLevel.md) — the projectile-side tier
- [Ripping](Ripping.md) — the tier system in context
