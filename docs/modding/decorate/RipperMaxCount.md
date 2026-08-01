**[Actor](https://zdoom.org/wiki/Classes:Actor)** property

`RipperMaxCount value`

# RipperMaxCount

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2.

## Usage

Defines how many times this projectile may [rip](Ripping.md) over its **whole life**, counting
every victim together.

If this is `0`, there is no lifetime limit. Default is `0`.

This is the whole-flight counterpart to [`RipperCount`](RipperCount.md), which limits hits
against each victim separately. The two can be combined: `RipperCount 3` with
`RipperMaxCount 10` gives at most three bites out of any one enemy and ten bites in total.

Rip damage lands once per tic of contact, so this counts tics of contact, not enemies pierced.

Notes:

- Once spent, the projectile passes through everything inertly — no damage, blood, sound, poison
  or push. Add [`+RIPEXPLODEONLIMIT`](RIPEXPLODEONLIMIT.md) to detonate instead.
- **The budget is spent on the hit that exhausts it, and detonation happens then**, not on the
  next contact.
- Negative values are clamped to `0` when parsed.
- The running total is readable in DECORATE expressions as `RipperHitsDone`, and cleared by
  [`A_ResetRipCounters`](A_ResetRipCounters.md).

## Examples

A railgun slug with a hard ceiling on how much it can pierce, regardless of how many bodies are
in the line — it stops after ten bites wherever it happens to be.

```
ACTOR RailSlug
{
    Radius 6
    Height 6
    Speed 60
    Damage 10
    Projectile
    +RIPPER
    +RIPEXPLODEONLIMIT
    RipperMaxCount 10
    States
    {
    Spawn:
        SLUG A 1 Bright
        Loop
    Death:
        SLUG BC 4 Bright
        Stop
    }
}
```

A projectile that visibly weakens as it runs out of momentum, branching on the readable counter
before its budget is gone:

```
    Spawn:
        SLUG A 1 Bright A_JumpIf(RipperHitsDone > 6, "Fading")
        Loop
    Fading:
        SLUG D 1 Bright A_SetTranslucent(0.4, 1)
        Loop
```

## See also

- [Ripping](Ripping.md) — the budget model and what a spent budget does
- [RipperCount](RipperCount.md) — the per-victim counterpart
- [+RIPEXPLODEONLIMIT](RIPEXPLODEONLIMIT.md) — detonate instead of passing through inertly
- [A_ResetRipCounters](A_ResetRipCounters.md) — refill the budget mid-flight
