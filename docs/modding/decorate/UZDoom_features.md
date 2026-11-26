# UZDoom's DECORATE features

## Availability

> **ZandroX behaviour.** These are UZDoom's DECORATE expressions, functions, parameters, properties
> and flags, under UZDoom's names, so a UZDoom mod's DECORATE uses them unchanged. Zandronum 3.3 has
> none of them, and stops loading a mod that uses one. They all work in netgames.

## Expressions

| Kind | Names |
|---|---|
| Math | `Min`, `Max`, `Clamp`, `Floor`, `Ceil`, `Round`, `Exp`, `Log`, `Log10`, `Tan`, `ACos`, `ASin`, `ATan`, `ATan2`, `VectorAngle`, `CosH`, `SinH`, `TanH` |
| Actor queries | `CountInv`, `GetCVar`, `GetDistance`, `GetAngle`, `GetZAt`, `GetSpawnHealth`, `GetGibHealth`, `GetMissileDamage`, `GetCrouchFactor`, `CountProximity`, `GetPlayerInput`, `OverlayX`, `OverlayY` |
| Actor fields | `threshold`, `FloatSpeed`, `tics`, `gravity`, `floorclip`, `bouncefactor`, `wallbouncefactor`, `bouncecount`, `DamageFactor`, `DamageMultiply`, `MaxStepHeight`, `MaxDropOffHeight`, `projectilepassheight`, `PainThreshold`, `Friction`, `meleethreshold`, `maxtargetrange`, `DesignatedTeam`, `lastpush`, `lastbump`, `weaponspecial`, `StartHealth`, `PoisonDamage`, `PoisonDuration`, `PoisonPeriod`, `skillrespawncount` |

User variables can be `var float`, arrays included, set with `A_SetUserVarFloat` and `A_SetUserArrayFloat`.
ACS reads and writes them as fixed point.

## Functions and parameters

New functions: `A_CheckSpecies`, `A_CheckProximity`, `A_CheckBlock`, `A_JumpIfHigherOrLower`, `A_SetSpecies`,
`A_SetFloatSpeed`, `A_SetPainThreshold`, `A_LogFloat`, `A_SetUserVarFloat`, `A_SetUserArrayFloat`.

New parameters:
- `src` and `inflict` on the `A_Damage*` and `A_Kill*` family.
- `damagetype` and `nailrange` on `A_Explode`, which also takes `XF_EXPLICITDAMAGETYPE` and `XF_NOSPLASH`.
- `noz` on `A_JumpIfCloser`, `A_JumpIfTargetCloser`, `A_JumpIfMasterCloser` and `A_JumpIfTracerCloser`.
- `usezero` on `A_SetScale`.
- `SXF_ISTARGET`, `SXF_ISMASTER` and `SXF_ISTRACER` on `A_SpawnItemEx`.
- `local` on `A_Log` and `A_LogInt`.

`A_SetTranslation("None")` clears the translation, as in UZDoom. `A_fua_ResetTranslation()`
(ZandroX only) gives back the class's own.

## Properties

`Threshold`, `DefThreshold`, `InfightingGroup`, `ProjectileGroup`, `SplashGroup`, `MissileChanceMult`,
`SelfDamageFactor`, `StealthAlpha`, `FriendlySeeBlocks`, `MinRespawnTics`, `RespawnDice`.

## Flags

| Flag | Effect |
|---|---|
| `+LAXTELEFRAGDMG` | Telefrag damage is cut by damage factors, armour and protection like any other. |
| `+FORCEZERORADIUSDMG` | A blast of zero damage still reaches what it covers, for pain and its specials. |
| `+HITOWNER` | A missile can hit the actor that fired it. |
| `+STOPRAILS` | A rail stops at this actor. |
| `+NOSHIELDREFLECT` | A missile explodes on a shield instead of being turned aside. |
| `+ICESHATTER` | Ice damage from this inflictor shatters frozen corpses. |
| `+SMASHABLE` | Dies on landing. |
| `+NOINFIGHTSPECIES` | Never turns on its own species. |
| `+FORCEINFIGHTING` | Turns on whoever hurts it, whatever the map or skill says about infighting. |
| `+FRIGHTENING` | Monsters chasing this actor run from it, as from a player with the frightening cheat. |
| `+SEEFRIENDLYMONSTERS` | A hostile monster also goes after friendly monsters. |
| `+DONTFOLLOWPLAYERS` | A friendly monster with nothing to fight stays put. |
| `+MINVISIBLE` | Monsters cannot see this actor. |
| `+MVISBLOCKED` | Sight checks to this actor always fail. |
| `+DONTFACETALKER` | A conversation leaves this actor facing where it was. |
| `+RETARGETAFTERSLAM` | A charge that slams into something goes idle instead of chasing. A class can also give a `Slam` state. |
| `+ONLYSLAMSOLID` | A charge passes through what is neither solid nor shootable. |
| `+NOAUTOOFFSKULLFLY` | A charge does not end when the actor stops moving. |
| `+GETOWNER` | A fast projectile's trail takes the shooter as its target, not the projectile. |
| `+DONTBOUNCEONSHOOTABLES` | A bouncing projectile explodes on a shootable actor instead of bouncing. |
| `+BOUNCEONUNRIPPABLES` | A bouncing ripper bounces off actors it cannot rip; otherwise a ripper rips through shootable actors rather than bouncing. |
| `+DONTBOUNCEONSKY` | A bouncer vanishes into a sky floor, ceiling or wall. |
| `+KEEPBOUNCEANGLE` | A bounce leaves the actor's angle as it was. |
| `+BOUNCEMODIFIESPITCH` | A bounce points the actor's pitch along its new velocity. |

`+FULLVOLDEATH` now also makes a monster's death scream full volume, as in UZDoom.

In a netgame, `A_ChangeFlag` on a render flag (such as `INVISIBLE`) or a bounce flag now reaches every client and
players who join later. Before, only the server's copy changed.

```
// A charger that stays put after a slam, passes through pickups, and cannot be hurt by its own kind.
actor Rammer : LostSoul
{
    +RETARGETAFTERSLAM
    +ONLYSLAMSOLID
    +NOINFIGHTSPECIES
}
```

## Differences from UZDoom

- `GetZAt` does not look through portals.
- `A_CheckBlock`'s `CBF_DROPOFF` still tests lines and actors.
- Not here yet: `NOSECTORDAMAGE` and `FORCESECTORDAMAGE` (monsters take no sector damage here yet),
  `AVOIDHAZARDS` and `STAYONLIFT` (MBF's smarter monster movement), `MASTERNOSEE`, and the render,
  player-movement and shadow flags.

## Implementation notes

[`src/zandronum/src/features/decorate-uzdoom/`](../../../src/zandronum/src/features/decorate-uzdoom/README.md).
