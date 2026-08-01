**[Actor](https://zdoom.org/wiki/Classes:Actor)** property

`RipLevelMax value`

# RipLevelMax

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Ported from GZDoom/UZDoom.

## Usage

Defines the **highest** [`RipperLevel`](RipperLevel.md) that may rip this actor. Rippers above it
cannot pierce this actor and explode on it instead.

If this is `0`, there is no upper bound. Default is `0`.

An upper bound is the less obvious half of [tiered ripping](Ripping.md#tiered-ripping), because
it makes an actor resistant to *strong* rippers rather than weak ones. It is for materials that
a fine blade slips through but a heavy one shatters against — gel, mesh, a swarm that a needle
threads but a cannon shell detonates inside.

Notes:

- A rejected ripper behaves exactly as though this actor had `+DONTRIP` — it detonates on
  contact rather than passing through.
- Not clamped non-negative.
- Combines with [`RipLevelMin`](RipLevelMin.md) to define a closed window; together they read as
  "only rippers between these tiers pass through me". Setting only this one leaves the window
  open at the bottom.
- Readable in DECORATE expressions as `RipLevelMax`, and changeable at runtime with
  [`A_SetRipMax`](A_SetRipMax.md).

## Examples

A gel blob that fine projectiles pass straight through, but which detonates anything heavier —
so the intuitive answer of "use the big gun" is the wrong one.

```
ACTOR GelBlob
{
    Health 400
    Radius 24
    Height 48
    Monster
    RipLevelMax 2
}
```

A closed window, resisting both the feeble and the overwhelming and letting only mid-tier
rippers through:

```
ACTOR MeshBarrier
{
    Health 200
    RipLevelMin 2
    RipLevelMax 4
}
```

## See also

- [Ripping](Ripping.md) — the tier system in context
- [RipLevelMin](RipLevelMin.md) — the lower bound
- [RipperLevel](RipperLevel.md) — the projectile's side of the check
- [A_SetRipMax](A_SetRipMax.md) — change the bound at runtime
