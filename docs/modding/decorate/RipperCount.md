**[Actor](https://zdoom.org/wiki/Classes:Actor)** property

`RipperCount value`

# RipperCount

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2.

## Usage

Defines how many times this projectile may [rip](Ripping.md) **any one victim**.

If this is `0`, there is no per-victim limit. Default is `0`.

The budget is tracked per victim, so a projectile with `RipperCount 3` can rip three times into
each enemy it passes through, not three times in total — that is
[`RipperMaxCount`](RipperMaxCount.md).

Rip damage lands once per tic of contact, so this counts **tics spent inside a victim**, not
separate passes through it.

Notes:

- Once the budget against a victim is spent, the projectile passes through **that** victim
  inertly — no damage, blood, sound, poison or push — while still hurting everyone else.
- Add [`+RIPEXPLODEONLIMIT`](RIPEXPLODEONLIMIT.md) to detonate on the spending hit instead of
  ghosting through. `RipperCount 3` with that flag reads as "pierce three times, then boom".
- **The budget is spent on the hit that exhausts it, and detonation happens then** — there is
  never a further contact.
- Negative values are clamped to `0` when parsed.
- Setting this makes the projectile carry a small per-victim ledger. A ripper that sets neither
  this nor [`RipperDamageFactor`](RipperDamageFactor.md) allocates nothing, which is why the
  feature costs existing content no memory.
- A projectile that rips more than 128 distinct actors starts forgetting the oldest, which
  refills that actor's budget. This is a pathological case in practice.

## Examples

A saw blade that grinds each enemy for a few tics and then slips past, so it sweeps a crowd
rather than stalling in the first body it meets.

```
ACTOR SawBlade
{
    Radius 10
    Height 10
    Speed 25
    Damage 6
    Projectile
    +RIPPER
    RipperCount 5
    States
    {
    Spawn:
        SAWB ABCD 2 Bright
        Loop
    Death:
        SAWB E 6 Bright
        Stop
    }
}
```

A spear that pierces exactly twice and then detonates, using the flag to turn the spent budget
into an explosion:

```
    +RIPPER
    +RIPEXPLODEONLIMIT
    RipperCount 2
```

## See also

- [Ripping](Ripping.md) — the budget model and what a spent budget does
- [RipperMaxCount](RipperMaxCount.md) — the whole-life counterpart
- [+RIPEXPLODEONLIMIT](RIPEXPLODEONLIMIT.md) — detonate instead of passing through inertly
- [RipperDamageFactor](RipperDamageFactor.md) — falloff on repeat hits against the same victim
