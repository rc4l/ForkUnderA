# features/acs-parity

UZDoom's ACS functions and actor properties at UZDoom's numbers, answering as UZDoom's do. Modder
reference: [`docs/modding/acs/UZDoom_functions.md`](../../../../../docs/modding/acs/UZDoom_functions.md).

| Number | Function | Upstream |
|---|---|---|
| 93 | `GetMaxInventory` | uzdoom@c51abb0161 |
| 94 | `SetSectorDamage`, its amount and damage type only | uzdoom@d0c372692b |
| 98 | `CheckProximity` | uzdoom@6633e41cca |
| 99 | `CheckActorState` | uzdoom@df4a8e38c5 |
| 200 | `CheckClass` | uzdoom@3299a29c44 |
| 201 | `DamageActor` | uzdoom@d7b5bdc0f7 |
| 205 | `GetActorFloorTerrain` | uzdoom@85488f2f30 |
| 206 | `StrArg` | uzdoom@f82ab889b2 |
| 207-209 | `Floor`, `Round`, `Ceil` | uzdoom@1ff4016498 |
| 215, 216 | `GetNetID`, `SetActivatorByNetID` | uzdoom@f5ceaafbbc |
| 300, 301 | `GetLineX`, `GetLineY` | uzdoom@3a569f176d |
| APROP 44, 45 | `MaxStepHeight`, `MaxDropOffHeight` | uzdoom@1d286d5bd5 |
| APROP 46 | `DamageType` | uzdoom@d09ec5a930 |

- None of the numbers clash: Zandronum's own functions run 100 to 191, and 204 is already UZDoom's
  `GetActorFloorTexture`.
- `Ceil` keeps a whole number whole, where UZDoom's `(x & ~0xffff) + 0x10000` adds one.
- `DamageActor` on a tid no actor holds gives -1. UZDoom's `P_DamageMobj` reads the null target's class
  and crashes, and ours read its flags in `isFakePain` before its own null check.
- In a CLIENTSIDE script on a client, `DamageActor` hurts only the client's own actors and gives -1 for a
  synced one, whose health the server holds; a client's `P_DamageMobj` would take the health off locally
  and keep it (`ComputeClientMayDamage`).
- Net ids are Zandronum's `NetID`, the same on a server and its clients; 0 names no actor, as UZDoom's
  world id does.
- A step or drop-off height a script changes goes to clients through `SetThingProperty`, and to a
  joining client when it differs from the default, since clients move their own pawn and walking
  monsters through `P_TryMove`. A damage type stays on the server.

## Hooks

- `p_acs.cpp`: the numbers in `EACSFunctions`, their cases in `CallFunction`, `CheckInventory` through
  `InventoryAmount`, and the three properties in `DoSetActorProperty`, `GetActorProperty` and
  `CheckActorProperty`.
- `p_acs.h`: `APROP_MaxStepHeight`, `APROP_MaxDropOffHeight`, `APROP_DamageType`.
- `sv_commands.cpp`, `cl_main.cpp`, `sv_main.cpp`: the two heights in `SetThingProperty` and in
  `SERVER_UpdateActorProperties`.
- `p_interaction.cpp`: `isFakePain` answers no for a null target.
- `features/acs-argcount`: each function's minimum argument count, UZDoom's `MIN_ARG_COUNT`.

## Tests

- `computation/acsparity_compute_test.cpp`: the rounding, `GetLineX`/`GetLineY`, net ids, the
  `CheckProximity` scan and its flags, and `GetMaxInventory`, each against UZDoom's formula.
- `fuactl scenarios acsparity`: every function and property in one script, and `DamageActor` on a
  missing tid; it fails on a build without this feature, and crashes on one without the
  `isFakePain` check.
- `fuactl scenarios netacsparity`: `acsparity`'s checks run by the player on the server and as CLIENTSIDE
  copies on the player's client, an observer and a late joiner, each machine against its own answers; it
  fails on a build whose client `DamageActor` hurts a synced barrel. `SetSectorDamage` runs the same way:
  a client's own call hurts no one, the server's hurts the player, and every client then holds the health
  the server left (`fua_debug_netdrop=12`, `SVC_DAMAGEPLAYER` unsent, fails them).
- `fuactl scenarios netacsstepheight`: the server, its player's client and a late joiner hold the
  heights a server script set; `--side lan --cvar fua_debug_netdrop=65` leaves `SetThingProperty`
  unsent and fails it.
- Against UZDoom itself, the scenario's pk3 logged the same 48 values but for `Ceil(2.0)` and the
  default terrain's spelling (`SOLID`).
