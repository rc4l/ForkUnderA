**[Actor](https://zdoom.org/wiki/Classes:Actor)** flag

`+USERIPSTATE`

# USERIPSTATE

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2.

## Usage

Set on a **projectile**. After [ripping](Ripping.md), it enters its [`Rip:`](Rip.md) state.

This mirrors `+USEBOUNCESTATE` and its `Bounce:` state exactly, and gives a ripper a place to
react to its own hits — spawn sparks or gibs, play a sound on a cadence of your choosing, change
sprite as it dulls, or check a counter and jump.

Harmless if the actor defines no `Rip:` state; nothing happens.

Notes:

- The state is entered **once per tic**, matching the cadence of rip damage — not once per
  victim, and not once per movement sub-step. A `FastProjectile` can land several rips in a tic
  but still enters `Rip:` only once.
- The state change is applied after the projectile finishes moving, which makes it safe for a
  `Rip:` state to destroy the projectile immediately.
- A projectile that detonated during its move never has its Death state overwritten by this.
- Like `Bounce:`, the state change is **not broadcast**, following the same client-side
  convention as `+USEBOUNCESTATE`.

## Examples

A blade that throws sparks each tic it is inside something:

```
ACTOR SparkBlade
{
    Radius 10
    Height 10
    Speed 30
    Damage 8
    Projectile
    +RIPPER
    +USERIPSTATE
    States
    {
    Spawn:
        BLAD AB 2 Bright
        Loop
    Rip:
        BLAD C 1 Bright A_SpawnItemEx("Sparks", 0, 0, 0, 0, 0, 0, 0, SXF_NOCHECKPOSITION)
        Goto Spawn
    Death:
        BLAD DE 4 Bright
        Stop
    }
}
```

`Goto Spawn` returns the projectile to its flight animation; without it the projectile would sit
in the `Rip:` sequence.

Using the state to visibly dull as the ripper is spent:

```
    Rip:
        BLAD C 1 Bright A_JumpIf(RipperHitsDone > 4, "Dull")
        Goto Spawn
    Dull:
        BLAD F 1 A_SetTranslucent(0.5, 1)
        Goto Spawn
```

## See also

- [Rip](Rip.md) — the state this flag enables
- [Ripping](Ripping.md) — the rip system and its readable counters
- [+NORIPSOUND](NORIPSOUND.md) — often paired with this, to drive rip sound from the state
  instead of once per tic
