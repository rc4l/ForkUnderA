**[Actor](https://zdoom.org/wiki/Classes:Actor)** state

`Rip:`

# Rip

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2.
>
> Requires [`+USERIPSTATE`](USERIPSTATE.md) on the projectile. Without the flag the state is
> never entered, even if defined.

## Usage

Entered by a projectile after it [rips](Ripping.md) a victim, when it has
[`+USERIPSTATE`](USERIPSTATE.md) set.

This is the rip counterpart to `Bounce:`, and it works the same way: it is an ordinary state
sequence, so anything you can do in a state you can do here. The usual reasons to define it are
spawning debris or sparks on each hit, playing a sound on a slower cadence than the once-per-tic
rip sound, changing the projectile's appearance as it wears out, or branching on
`RipperHitsDone` / `RipperDamageDone`.

**Entered once per tic**, matching the cadence of rip damage — not once per victim, and not once
per movement sub-step. A `FastProjectile` resets its per-move rip tracking on every sub-step and
can therefore land several rips in one tic, but still enters `Rip:` only once.

Notes:

- The state chain should end by returning to the projectile's flight states, usually with
  `Goto Spawn`. A `Rip:` sequence that just runs to `Stop` destroys the projectile.
- It is safe for a `Rip:` state to destroy the projectile immediately: the state change is
  applied after the move completes, not in the middle of the collision walk.
- A projectile that exploded during the same move keeps its Death state; `Rip:` does not
  overwrite it.
- The state change is **not broadcast**, matching `+USEBOUNCESTATE` / `Bounce:`.

## Examples

The minimal form — one frame of feedback, then back to flight:

```
ACTOR Shredder
{
    Radius 10
    Height 10
    Speed 30
    Damage 10
    Projectile
    +RIPPER
    +USERIPSTATE
    States
    {
    Spawn:
        SPIK A 1
        Loop
    Rip:
        SPIK B 1 A_SpawnItemEx("Sparks")
        Goto Spawn
    Death:
        SPIK CD 4
        Stop
    }
}
```

A ripper that dulls visibly as it spends its budget, using the readable counter:

```
    Rip:
        SPIK B 1 A_JumpIf(RipperHitsDone > 4, "Dull")
        Goto Spawn
    Dull:
        SPIK E 1 A_SetTranslucent(0.5, 1)
        Goto Spawn
```

A projectile that ends itself on its first rip — safe, and a way to build a single-target
piercing shot that leaves an effect behind:

```
    Rip:
        SPIK B 4 A_SpawnItemEx("Impact")
        Stop
```

## See also

- [+USERIPSTATE](USERIPSTATE.md) — the flag that enables this state
- [Ripping](Ripping.md) — the rip system and its readable counters
- [A_ResetRipCounters](A_ResetRipCounters.md) — refill budgets from this state
