**[Actor](https://zdoom.org/wiki/Classes:Actor)** flag

`+RIPEXPLODEONLIMIT`

# RIPEXPLODEONLIMIT

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2.

## Usage

Set on a **projectile**. When one of its rip **count** budgets runs out, the projectile detonates
instead of passing through inertly.

Without this flag, a projectile that has spent [`RipperCount`](RipperCount.md) against a victim
ghosts through that victim — no damage, blood, sound, poison or push — while still hurting
everyone else; and one that has spent [`RipperMaxCount`](RipperMaxCount.md) ghosts through
everything. With the flag, spending either budget sends it to its Death state.

This is what turns a budget into "pierce N times, **then boom**", which is usually the more
readable behaviour: a projectile that silently stops mattering is confusing to fight, while one
that explodes explains itself.

Notes:

- **The detonation happens on the hit that spends the budget, not on the next contact.** That
  hit lands in full first. `RipperCount 3` with this flag pierces exactly three times and then
  explodes — there is never a fourth contact.
- This flag has **no bearing on [`RipperMaxDamage`](RipperMaxDamage.md)**, which always detonates
  on reaching its cap whether the flag is set or not.
- The flag does nothing on a projectile that sets no count budget, since nothing can be spent.
- A spent projectile that strikes a `+REFLECTIVE` actor is reflected rather than detonated —
  the blocked-missile machinery tries reflection first.

## Examples

A harpoon that pierces two enemies and detonates in the second, so its damage budget reads
clearly to the player:

```
ACTOR Harpoon
{
    Radius 8
    Height 8
    Speed 35
    Damage 25
    Projectile
    +RIPPER
    +RIPEXPLODEONLIMIT
    RipperCount 2
    DeathSound "weapons/harpoonburst"
    States
    {
    Spawn:
        HARP A 1 Bright
        Loop
    Death:
        HARP BCD 5 Bright A_Explode(64, 96)
        Stop
    }
}
```

A lifetime budget instead, so the projectile explodes wherever it happens to run out rather than
per-enemy:

```
    +RIPPER
    +RIPEXPLODEONLIMIT
    RipperMaxCount 8
```

## See also

- [Ripping](Ripping.md) — the budget model and what a spent budget does
- [RipperCount](RipperCount.md), [RipperMaxCount](RipperMaxCount.md) — the budgets this affects
- [RipperMaxDamage](RipperMaxDamage.md) — always detonates, flag or not
