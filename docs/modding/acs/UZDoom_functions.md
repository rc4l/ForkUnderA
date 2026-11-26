# UZDoom's ACS functions

## Availability

> **ZandroX behaviour.** These are UZDoom's (and GZDoom's) ACS functions and actor properties, at
> UZDoom's numbers, so a script compiled for UZDoom calls them unchanged. Zandronum 3.3 has none of
> them: there, each function returns 0 and each property reads 0.

| Function | Number | Returns |
|---|---|---|
| `GetMaxInventory(tid, item)` | 93 | the most of `item` the actor can carry; `"Health"` gives its maximum health |
| `SetSectorDamage(tag, amount [, damagetype [, interval [, leakiness]]])` | 94 | nothing; players in the tagged sectors take `amount` of `damagetype` damage |
| `CheckProximity(tid, class, distance [, count [, flags [, ptr]]])` | 98 | whether `count` actors of `class` stand within `distance`, by `CPXF_` flags |
| `CheckActorState(tid, state [, exact])` | 99 | whether the actor's class has the state |
| `CheckClass(class)` | 200 | whether an actor class of that name exists |
| `DamageActor(tid, ptr, inflictor, ptr, amount, damagetype)` | 201 | the damage dealt, or -1 when none was |
| `GetActorFloorTerrain(tid)` | 205 | the name of the TERRAIN under the actor |
| `StrArg(string)` | 206 | a name an action special takes, such as a named script for `ACS_Execute` |
| `Floor(x)`, `Round(x)`, `Ceil(x)` | 207-209 | a fixed-point value made whole |
| `GetNetID(tid, index [, ptr])` | 215 | the actor's net id, the same on a server and its clients, or 0 |
| `SetActivatorByNetID(netid [, ptr])` | 216 | 1 after making that actor the activator, else 0 |
| `GetLineX(lineid, along, offset)`, `GetLineY(...)` | 300, 301 | a point `along` the line (0 to 1.0), moved `offset` units off its front side |

| Actor property | Number | |
|---|---|---|
| `APROP_MaxStepHeight` | 44 | fixed; how high the actor steps up |
| `APROP_MaxDropOffHeight` | 45 | fixed; how far a monster walks down |
| `APROP_DamageType` | 46 | string; the damage type the actor deals |

## Usage

zt-bcc's `zcommon.bcs` declares all of them, naming the rounding functions `ZDoom_Floor`, `ZDoom_Round`
and `ZDoom_Ceil`. Builds of zt-bcc older than its UZDoom declarations lack `GetNetID` and
`SetActivatorByNetID`, which a script built with one declares itself:

```
special -215:GetNetID(int, int; int): int;
special -216:SetActivatorByNetID(int; int): bool;
```

```
// The barrel nearest the player becomes its target, then takes 10 fire damage from it.
if (CheckProximity(0, "ExplosiveBarrel", 300.0, 1, CPXF_SETTARGET | CPXF_CLOSEST))
    DamageActor(0, AAPTR_TARGET, 0, AAPTR_DEFAULT, 10, "Fire");
SetActorProperty(0, APROP_MaxStepHeight, 40.0);
```

In a netgame:
- A step or drop-off height a server script sets reaches every client, and players who join later,
  since a client moves its own player and walking monsters itself.
- A damage type stays on the server, which deals all damage.
- A net id names the same actor on the server and on each client, so a script can pass one to a
  clientside script.

## Examples

Each example is a whole library: compile it with zt-bcc, add it to `LOADACS` and run its scripts with
`pukename`.

The closest barrel within 200 units becomes the player's target and takes fire damage from it:

```bcs
#library "fuaburn"
#import "zcommon.bcs"

script "BurnNearest" ( void ) {
   if ( CheckProximity( 0, "ExplosiveBarrel", 200.0, 1, CPXF_SETTARGET | CPXF_CLOSEST ) ) {
      int dealt = DamageActor( 0, AAPTR_TARGET, 0, AAPTR_DEFAULT, 5, "Fire" );
      Log( s: "burned for ", d: dealt );
   }
   else {
      Log( s: "nothing to burn" );
   }
}
```

<!-- example
pukename BurnNearest => /nothing to burn/
pukename Place 250 250 0
pukename BurnNearest => /burned for 5/
-->

Footsteps by what the player stands on, from TERRAIN:

```bcs
#library "fuastep"
#import "zcommon.bcs"

script "Footstep" ( void ) {
   str terrain = GetActorFloorTerrain( 0 );
   if ( StrCmp( terrain, "Water" ) == 0 ) {
      Log( s: "splash" );
   }
   else {
      Log( s: "step on ", s: terrain );
   }
}
```

<!-- example
pukename Footstep => /step on Solid/
-->

A server script hands an actor to every client's clientside script by its net id, the one name for an
actor every machine shares:

```bcs
#library "fuaflash"
#import "zcommon.bcs"

script "FlashActor" ( int tid ) {
   ACS_NamedExecuteAlways( "FlashOnClients", 0, GetNetID( tid, 0 ) );
}

script "FlashOnClients" ( int netid ) clientside {
   if ( SetActivatorByNetID( netid ) ) {
      // The actor is this script's activator now: spawn a client-only effect at it, change its look.
      Log( s: "flashing a ", s: GetActorClass( 0 ) );
   }
}
```

<!-- example
pukename FlashActor 31 => /flashing a DoomImp/
-->

## Best practices

- **Deal damage from server scripts.** In a clientside script `DamageActor` only hurts actors that exist
  on that client alone (and returns -1 for the rest), since the server holds every other actor's health.
- **Pass net ids to clientside scripts,** not tids or handles: a tid can name several actors, and each
  machine has its own handle table. `GetNetID( tid, 0 )` on the server, `SetActivatorByNetID` on the client.
- **Prefer `CheckProximity` to a loop over tids** for "is there one near": it walks the blockmap around
  the actor rather than every actor with a tid.
- **Round only what you show.** `ZDoom_Round` and the others drop the fraction a position or speed keeps
  in its low 16 bits, so movement built on rounded values drifts.

## Differences from UZDoom

- `Ceil` of a whole number returns it unchanged. UZDoom's adds one, so `Ceil(2.0)` is 3.0 there.
- `DamageActor` on a tid no actor holds returns -1. UZDoom crashes.
- The default terrain's name is `Solid`; UZDoom spells it `SOLID`.
- `SetSectorDamage` sets what `Sector_SetDamage` does: the damage lands once every 32 tics, and a radiation
  suit lets it through by the amount (none under 20, now and then under 50, always from 50). The
  `interval` and `leakiness` arguments are accepted and ignored for now. The damage type is one of the
  built-in ones (`Fire`, `Slime`, `Ice`, `Poison`, `Electric`, `Drowning`, `Crush` and the rest of
  `Sector_SetDamage`'s); another name does normal damage and warns once. A negative amount, which heals
  in UZDoom, sets no damage.

## Implementation notes

[`src/zandronum/src/features/acs-parity/`](../../../src/zandronum/src/features/acs-parity/README.md).
