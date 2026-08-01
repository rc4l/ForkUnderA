**[Actor](https://zdoom.org/wiki/Classes:Actor)** property

`RipperMaxDamage value`

# RipperMaxDamage

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2.

## Usage

Defines the total damage this projectile may deal by [ripping](Ripping.md) before it is forced
into its Death state.

If this is `0`, the projectile rips without a damage limit. Default is `0`.

The counter tracks **damage actually dealt** — the amount after armour, `DamageFactor` and any
resistance the victim has, not the projectile's nominal `Damage`. A ripper aimed at a heavily
armoured target therefore lasts longer than one aimed at a soft one, which is usually what you
want from a "this projectile is worth N damage" budget.

Notes:

- **Reaching the cap detonates the projectile on the hit that crosses it**, not on its next
  contact. The crossing hit deals its full damage; it is not clamped to whatever budget
  remained, so the total can overshoot.
- Unlike the two count budgets, a spent `RipperMaxDamage` **always** detonates.
  [`+RIPEXPLODEONLIMIT`](RIPEXPLODEONLIMIT.md) is not needed and has no bearing on it.
- Negative values are clamped to `0` when parsed.
- The running total is readable in DECORATE expressions as `RipperDamageDone`, and cleared by
  [`A_ResetRipCounters`](A_ResetRipCounters.md).
- `RipperDamageDone` only advances on the server; a client's copy stays at `0`.

## Examples

A piercing lance that is "worth" 300 damage however it is spent — it may shred one tough enemy
or several weak ones, and detonates the moment its budget is used up.

```
ACTOR PiercingLance
{
    Radius 8
    Height 8
    Speed 40
    Damage 12
    Projectile
    +RIPPER
    RipperMaxDamage 300
    States
    {
    Spawn:
        LANC A 1 Bright
        Loop
    Death:
        LANC BCD 4 Bright
        Stop
    }
}
```

Combined with a per-victim cap, so it cannot dump its whole budget into a single target:

```
    RipperMaxDamage 300
    RipperCount 4
```

## See also

- [Ripping](Ripping.md) — how the budgets interact and when they detonate
- [RipperCount](RipperCount.md), [RipperMaxCount](RipperMaxCount.md) — the two hit-count budgets
- [A_ResetRipCounters](A_ResetRipCounters.md) — refill the budget mid-flight
