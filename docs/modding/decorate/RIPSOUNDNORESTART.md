**[Actor](https://zdoom.org/wiki/Classes:Actor)** flag

`+RIPSOUNDNORESTART`

# RIPSOUNDNORESTART

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2.

## Usage

Set on a **projectile**. Its rip sound is allowed to finish instead of being restarted every tic.

By default the rip sound is played once per tic for as long as the projectile is inside a victim,
and starting a sound stops that channel first. For the short vanilla squelch that is exactly
right — the rapid retrigger is what makes it read as continuous shredding. For anything longer
it is fatal: the sample never gets past its first few milliseconds, so a meaty rip sound comes
out as a stutter.

**Any [`RipSound`](RipSound.md) longer than about 1/35 of a second needs this flag.**

It is opt-in precisely so existing content keeps the machine-gunned squelch it was authored
against.

Notes:

- With the flag set, the sound is only started when it is not already playing on the projectile,
  so it runs to completion and then begins again.
- The guard is inert on a dedicated server, which has no sound channels — correct, since servers
  do not play sound.
- This does not change *which* sound plays. Use [`RipSound`](RipSound.md) for that, or
  [`+NORIPSOUND`](NORIPSOUND.md) for silence.

## Examples

A ripper with a long, wet tearing sound that would otherwise be chopped into a buzz:

```
ACTOR FleshRender
{
    Radius 12
    Height 12
    Speed 25
    Damage 12
    Projectile
    +RIPPER
    +RIPSOUNDNORESTART
    RipSound "monster/fleshtear"
    States
    {
    Spawn:
        REND AB 2 Bright
        Loop
    Death:
        REND CD 5 Bright
        Stop
    }
}
```

Leave the flag off for a short squelch, where the retrigger is the effect:

```
    +RIPPER
    RipSound "misc/ripslop"
```

## See also

- [RipSound](RipSound.md) — set the sound, and its other silent-failure modes
- [+NORIPSOUND](NORIPSOUND.md) — silence rips entirely
- [Ripping](Ripping.md) — the rip system
