**[Actor](https://zdoom.org/wiki/Classes:Actor)** flag

`+NORIPSOUND`

# NORIPSOUND

## Availability

> **ZandroX only.** Does not exist in Zandronum 3.2.

## Usage

Set on a **projectile**. Its [rips](Ripping.md) are silent.

[`RipSound`](RipSound.md) can replace the sound a rip makes but never remove it — an empty
`RipSound` means "use the default", not "use nothing". This flag is how you get silence.

Useful for a ripper whose sound design lives elsewhere: a projectile with a continuous flight
loop, or one whose [`Rip:`](Rip.md) state plays its own sound at a cadence you control rather
than once per tic.

Notes:

- The flag silences the rip sound only. The projectile's other sounds — `SeeSound`,
  `DeathSound`, anything played from its states — are unaffected.
- Under a Doom IWAD rips are already silent by default, because the default rip sound is defined
  only for Heretic and Hexen. This flag matters there only if you have also set an explicit
  `RipSound`.

## Examples

A beam that hums continuously and should not squelch on every tic of contact:

```
ACTOR HumBeam
{
    Radius 8
    Height 8
    Speed 40
    Damage 4
    Projectile
    +RIPPER
    +NORIPSOUND
    SeeSound "weapons/beamloop"
    States
    {
    Spawn:
        BEAM AB 2 Bright
        Loop
    Death:
        BEAM C 4 Bright
        Stop
    }
}
```

Silent rips with sound driven from the `Rip:` state instead, so it plays on a slower cadence
than once per tic:

```
    +RIPPER
    +NORIPSOUND
    +USERIPSTATE
    States
    {
    Rip:
        BEAM A 6 A_PlaySound("weapons/rip")
        Goto Spawn
    }
```

## See also

- [Ripping](Ripping.md) — the rip system
- [RipSound](RipSound.md) — set the sound rather than removing it
- [+RIPSOUNDNORESTART](RIPSOUNDNORESTART.md) — keep the sound but stop it restarting every tic
- [Rip](Rip.md) — drive sound from a state instead
