# compat-oldnodelay

`compat_fua_oldnodelay` (a `zacompatflags` bit, off by default) times a `NoDelay` spawn state the way
Zandronum 3.3 does, for mods built against it.

- **Default (UZDoom's, `uzdoom@5aba252b873ae6885b2fc056c9ede5105213ef59`):** the actor's first tick calls
  the state's action, then counts the state down in the same tick. A `NoDelay` state of N tics lasts N - 1
  after its action, and `TNT1 A 1 NoDelay` moves on in the tic it spawned.
- **With the flag (Zandronum 3.3, `zandronum@c3543573546a9691bc19ead5a0e3614254929ed5`, its port of ZDoom's
  `NODELAY`):** the first tick re-enters `SetState` on the spawn state, which calls the action and chains its
  0-tic states, then adds a tic (`ComputeOldNoDelayTics`). The state lasts its whole duration after the action.

Why it matters: actors tick before the ACS thinker (`STAT_DEFAULT` before `STAT_SCRIPTS`), so a script a
`NoDelay` action starts runs at the end of that tic. A mod that removes the actor from that script (DnD's
`"DND Shared Item Init"` swaps an `_MP` item for its `_SP` copy) relies on the actor still being in its first
state. Under the default timing, the actor's next state has already started another script by then, and that
script runs on with an activator that is gone. In DnD that ended in `LineAttack(0, ...)` with no shooter
(`features/acs-lineattack`).

## Exposure

The bit is FUA's: `1 << 30`, counting down from the top, so Zandronum's own bits (up to 26, counting up) cannot
take it on a re-sync. `zacompatflags` reaches clients through the existing flagset sync, so server and client
time states alike. The server registry writes the whole word to launchers, which show the unknown bit as theirs
to decode.

## In-engine hooks

- `doomdef.h`: `ZACOMPATF_FUA_OLD_NODELAY`.
- `d_main.cpp`: the `compat_fua_oldnodelay` flag cvar.
- `p_mobj.cpp` `AActor::Tick`: the `NoDelay` branch takes Zandronum 3.3's path when the flag is set.
- `features/server-browser/computation/flaghelp_compute.cpp`: the flag's tooltip.

Scenario `oldnodelay`: three probes whose 1-tic `NoDelay` state starts a script that removes them. With the
flag on, none reaches its second state; with it off, all three do.
