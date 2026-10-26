# acs-lineattack

ACS `LineAttack(0, ...)` fires from the script's activator. A world script (`OPEN`, or one started by a line
with nobody on it) has none, and neither does a script whose activator was removed while it waited. Zandronum
3.3 and UZDoom both hand that null straight to `P_LineAttack`, which dereferences it. Here it fires nothing and
the console says so once per script (`ComputeLineAttackShooter`, `ComputeFirstWarning`):

```
LineAttack in script "DnD Try Spawn Area" has no activator to fire from, so it fires nothing
```

Found through DnD, whose merchant spawn reached it from a script orphaned by the `NoDelay` timing change
(`features/compat-oldnodelay`).

`LineAttack` also takes the ninth argument, `pufftid`, which the base (Zandronum 3.2.1) predated: the puff an
attack leaves takes that tid (`uzdoom@c9afb199caf363e1387bd9111b31137d5c44d972` and its null check
`uzdoom@b2f860d7ae6842ab0bdf168f6b2bb4a03bf5e9b0`; Zandronum 3.3's copies are `zandronum@54f77bd2daddc918950064ecb68bdd600f220b91`
and `zandronum@58e9235da3e1ffcbd3fad8184dba14596e3094b0`). DnD finds its hitscan victims through that puff.

## In-engine hooks

- `p_acs.cpp` `DLevelScript::CallFunction`, `ACSF_LineAttack`: the shooter decision, the warning, and `pufftid`.

Scenarios: `lineattacknoactivator`, an `OPEN` script calling `LineAttack(0, ...)` and counting that it got past it
(it crashed the engine before); `lineattackpufftid`, a map spot's attack whose puff must carry the pufftid.
