# playerclass-fallback

Stops an unrecognised `playerclass` from silently becoming "roll a new class on every spawn".

## The bug

`playerclass` is **one archived cvar shared by every mod**, resolved **by name** against whatever is
currently loaded:

```cpp
int D_PlayerClassToInt (const char *classname)   // d_netinfo.cpp
{
    if (PlayerClasses.Size () > 1)
    {
        for (...) if (stricmp (type->Meta.GetMetaString (APMETA_DisplayName), classname) == 0) return i;
        return -1;                               // <-- "no class by that name"
    }
    return 0;
}
```

`-1` is also the value that means **"the player chose Random"**. Two different facts, one sentinel,
and every consumer reads it as "roll the dice":

| | |
| --- | --- |
| `InitPlayerClasses` (g_level.cpp) | `pr_classchoice() % PlayerClasses.Size()` |
| `G_UpdateSinglePlayerClass` (g_level.cpp) | same, or `TEAM_SelectRandomValidPlayerClass` |
| `P_SpawnPlayer` (p_mobj.cpp) | same |

So a name left behind by another mod, or ZDoom's own default `"Fighter"` on a fresh config, means a
different class every single respawn. On a mod that registers bot-only classes next to the real one,
that reads as the player randomly turning into bots after each death.

Reported against a mod whose `MAPINFO` registers `Street_Ninja` and whose `KEYCONF` adds
`Street_Ninja_Bot`, `Ninja_bot_Hard` and `Chain_Ninja_Bot` as `nomenu`. Measured in-engine with an instrumented
build:

```
DIAG userinfoClass=-1  SN=1        <- pawn is Street_Ninja, userinfo says Random
playerclass "Street Ninja"
DIAG userinfoClass=0   SN=1        <- pinned
```

It only bites mods with **more than one** player class, because `D_PlayerClassToInt` short-circuits
to `0` otherwise. That is why plain Doom never shows it, and why it looks engine-specific when it is
really "the first multi-class mod this config has seen".

The mod even set `NoRandomPlayerClass`. That GAMEINFO key is read **only by menu code**
(`menudef.cpp`, `playermenu.cpp`, `multiplayermenu.cpp`, `optionmenuitems.h`) -- it hides the Random
entry in the picker and nothing else. None of the three rolls above consult it.

## What this does

**Honours `NoRandomPlayerClass` where the dice are actually rolled.** When the stored choice is
unusable *and* the mod forbade random classes, pick the first **selectable** class instead of
rolling. Otherwise return -1 and let the existing code run untouched.

Deliberately narrow, because of what this value touches:

- **`-1` still means Random.** Mods that genuinely want random classes (Hexen) are untouched; this
  never takes randomness away, it only declines to invent it where the mod said not to.
- **The fallback skips `PCF_NOMENU` classes.** "Use index 0" would have worked for the reported mod
  only by luck of registration order; the bot classes are exactly what must never be handed to a
  human.
- **It respects team restrictions**, and falls back to the original roll when a team leaves nothing
  eligible, rather than forcing a class that team forbids.
- **An out-of-range stored index counts as unusable too** -- the other way a stale choice arrives is
  as an index saved by a mod with more classes than the current one.

## In-place hooks

| File | Change |
| --- | --- |
| `g_level.cpp` `InitPlayerClasses` | consult the fallback before rolling |
| `g_level.cpp` `G_UpdateSinglePlayerClass` | same |
| `p_mobj.cpp` `P_SpawnPlayer` | same |

## Note for future changes here

This value is transmitted (`SERVERCOMMANDS_SetPlayerUserInfo`) and serialized into savegames and
demos. Anything that resolves it differently on client and server desyncs the pawn class, so keep
resolution in one place and let the server stay authoritative.
