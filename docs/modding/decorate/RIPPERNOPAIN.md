**[Actor](https://zdoom.org/wiki/Classes:Actor)** flag

`+RIPPERNOPAIN`

# RIPPERNOPAIN

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2.

## Usage

Set on a **projectile**. Its [rips](Ripping.md) never make a victim enter its Pain state, but the
projectile's terminal explosion still can.

That split is the point. `+PAINLESS` already exists and suppresses pain for **all** damage from a
projectile, explosion included. This flag is the rip-only half: a ripper can shred through a
crowd without pain-stunlocking every enemy it touches, and still stagger whatever it finally
detonates on.

This is the supported replacement for the old workaround of granting `PowerProtection` in a
monster's Pain state to blunt a ripper — a hack that had to be applied to every victim, and that
changed damage as well as pain.

Notes:

- A ripper that rips once per tic would otherwise re-trigger a victim's Pain state every tic,
  which holds monsters helpless for as long as the projectile is inside them. That stunlock is
  what this flag removes.
- The flag suppresses pain, not damage. Victims still take full rip damage.
- Use `+PAINLESS` instead if you want the explosion to be painless too.

## Examples

A drill that bores through a crowd without freezing everything it passes, while still staggering
the enemy it finally stops in:

```
ACTOR CrowdDrill
{
    Radius 10
    Height 10
    Speed 30
    Damage 10
    Projectile
    +RIPPER
    +RIPPERNOPAIN
    +RIPEXPLODEONLIMIT
    RipperMaxCount 12
    States
    {
    Spawn:
        DRIL AB 2 Bright
        Loop
    Death:
        DRIL CD 5 Bright A_Explode(40, 64)
        Stop
    }
}
```

Contrast with the blanket version, where neither the rips nor the explosion cause pain:

```
    +RIPPER
    +PAINLESS
```

## See also

- [Ripping](Ripping.md) — the rip system
- [RipperCount](RipperCount.md), [RipperMaxCount](RipperMaxCount.md) — bound how long a ripper
  keeps going
- [+RIPEXPLODEONLIMIT](RIPEXPLODEONLIMIT.md) — give the ripper a terminal explosion to stagger
  with
