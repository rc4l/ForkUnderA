**[Actor](https://zdoom.org/wiki/Classes:Actor)**

`void A_SetRipperLevel (int level)`

# A_SetRipperLevel

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2. Ported from GZDoom/UZDoom.
>
> **Server-authoritative and not broadcast** — see [Netplay](#netplay).

## Usage

Sets the calling actor's [`RipperLevel`](RipperLevel.md), its [rip tier](Ripping.md#tiered-ripping),
at runtime.

Lets a projectile's piercing power change during flight rather than being fixed at class
definition: a shot that pierces armour up close and loses that power at range, a charged
projectile that gains tier as it travels, or a projectile whose tier is set by the weapon that
fired it.

The value is not clamped, so negative tiers are legal.

### Parameters

- `int level`

  The new rip tier. There is no "keep current" sentinel — the value is applied as given.

## Netplay

The tier is an input to a decision the server makes on its own, so this is not replicated. That
is the same treatment the [`RipperLevel`](RipperLevel.md) property gets, and it costs nothing:
rip damage is server-side already, so a client's disagreement about tier cannot change the
outcome.

## Examples

A shot that pierces armour only in the first half-second of flight, then drops to a tier that
armoured enemies resist:

```
ACTOR FadingPiercer
{
    Radius 8
    Height 8
    Speed 40
    Damage 15
    Projectile
    +RIPPER
    RipperLevel 5
    States
    {
    Spawn:
        SHOT A 16 Bright
        SHOT A 0 A_SetRipperLevel(1)
        SHOT A 1 Bright
        Loop
    Death:
        SHOT BC 4 Bright
        Stop
    }
}
```

The reverse — a drill that bores harder the longer it survives, escalating through tiers from
its [`Rip:`](Rip.md) state:

```
    Rip:
        DRIL B 1 A_JumpIf(RipperHitsDone > 8, "Overdrive")
        Goto Spawn
    Overdrive:
        DRIL C 1 A_SetRipperLevel(9)
        Goto Spawn
```

## See also

- [RipperLevel](RipperLevel.md) — the property this sets
- [A_SetRipMin](A_SetRipMin.md), [A_SetRipMax](A_SetRipMax.md) — the victim-side bounds
- [Ripping](Ripping.md) — the tier system in context
