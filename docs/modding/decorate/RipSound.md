**[Actor](https://zdoom.org/wiki/Classes:Actor)** property

`RipSound value`

# RipSound

## Availability

> **Newly authorable in ZandroX.** The field itself already existed for MBF21's DeHackEd
> "Rip sound", but DECORATE had no way to set it — and, as it turns out, the DeHackEd route
> never worked either. See [It was broken before this](#it-was-broken-before-this).

## Usage

Defines the sound played when this projectile [rips](Ripping.md) a victim.

An empty value leaves it at the default, `misc/ripslop`. Default is empty.

The sound plays once per tic for as long as the projectile is inside a victim, which is what
makes the short vanilla squelch read as continuous shredding.

Notes:

- **Under a Doom IWAD the default is silent.** `misc/ripslop` is defined only for Heretic and
  Hexen — ripping is a Raven mechanic — so under Doom the default name resolves to nothing and
  no sound plays at all. **A Doom-based ripper must set an explicit `RipSound`.** This was left
  as-is deliberately: defining the sound for Doom would put a squelch on every existing Doom
  `+RIPPER` projectile, which are silent today.
- **A typo is indistinguishable from the default.** An unknown sound name resolves to `0`, and
  this code reads `0` as "use the default" — so a misspelled `RipSound` degrades silently rather
  than reporting an error. Under Doom that means total silence with no diagnostic.
- **Anything longer than about 1/35 of a second is chopped.** The sound is restarted every tic,
  and starting a sound stops the channel first, so a long sample never gets past its first few
  milliseconds. Use [`+RIPSOUNDNORESTART`](RIPSOUNDNORESTART.md) to let it play through.
- [`+NORIPSOUND`](NORIPSOUND.md) silences rips entirely. `RipSound` can replace the sound but
  never remove it.

## It was broken before this

Exposing the field to DECORATE surfaced a pre-existing bug worth knowing about if you have
touched MBF21 DeHackEd's `Rip sound`.

The field was declared with a type whose constructor zeroes it, while every other actor sound
field uses the variant that skips that initialisation. Because class defaults are copied into a
new object *before* its constructor runs, the authored value was wiped on **every single spawn**
— so `Rip sound = N` in DeHackEd could never have worked. Both routes work now.

## Examples

A Doom-based ripper with an explicit sound, since the default would be silent:

```
ACTOR BoneSaw
{
    Radius 10
    Height 10
    Speed 30
    Damage 8
    Projectile
    +RIPPER
    RipSound "weapons/sawhit"
    States
    {
    Spawn:
        SAWB AB 2 Bright
        Loop
    Death:
        SAWB C 5 Bright
        Stop
    }
}
```

A longer, meatier sound that needs the flag to survive:

```
    +RIPPER
    +RIPSOUNDNORESTART
    RipSound "monster/rip_long"
```

Without `+RIPSOUNDNORESTART`, that sample is cut off after roughly one tic and repeated, which
usually sounds like a stutter rather than the intended effect.

## See also

- [Ripping](Ripping.md) — the rip system
- [+RIPSOUNDNORESTART](RIPSOUNDNORESTART.md) — let a long rip sound finish
- [+NORIPSOUND](NORIPSOUND.md) — silence rips entirely
