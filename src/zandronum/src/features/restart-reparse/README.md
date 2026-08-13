# restart-reparse

Keeps the engine's lump-parsed tables correct across a **restart**, i.e. a second (third, nth)
trip through `D_DoomMain`'s reinit loop. `wad_reload` and the `restart` CCMD both come back
through that loop; the process, and every file-static in it, survives.

## The bug this exists for

Several parsers fill a `static` table *additively* and were only ever written for the one boot
where that table starts empty. Re-parsing a **different** WAD set on top of the previous set's
leftovers gives a table that matches neither set:

```
boot 1:  base gamemode.txt  -> Domination = TEAMGAME
         mod's GAMEMODE     -> RemoveFlag TEAMGAME, AddFlag DEATHMATCH
                            => Domination = DEATHMATCH          (valid)

wad_reload onto a set WITHOUT that mod (e.g. joining a server from the browser):
boot 2:  g_GameModes still says DEATHMATCH  <-- never reset
         base gamemode.txt  -> AddFlag TEAMGAME
                            => Domination = DEATHMATCH|TEAMGAME (two gametype bits)
```

which fails the table's own consistency check and kills the engine mid-restart:

```
Execution could not continue.
Can't determine if "domination" is cooperative, deathmatch, or team-based.
```

`I_Error` here is unrecoverable and lands *after* teardown, so `wad_reload`'s
validate-before-commit rollback cannot save the session: the player just loses the game.

## The rule

**A lump parser must reset its table before it reads, not trust the caller to.** Every fix below
clears at the top of the parse/construct function itself, so it holds for the restart CCMD, for
`wad_reload`, and for any future caller, rather than depending on a separate hook being called
first.

## In-place hooks (per `features/README.md`, listed here rather than moved)

| File | What it does now |
| --- | --- |
| `gamemode.cpp` `GAMEMODE_ParseGameModeInfo` | resets `g_GameModes` before reading GAMEMODE lumps |
| `callvote.cpp` `CALLVOTE_ReadVoteInfo` | clears `g_VoteTypeDefinitions` before reading VOTEINFO lumps |
| `announcer.cpp` `ANNOUNCER_Construct` | clears `g_AnnouncerProfile` before pushing the Default profile |

Without the callvote one, a reload where the *same* VOTEINFO-carrying mod is in both sets dies the
same way, on the parser's own duplicate check:

```
Script error, "votetest.pk3:voteinfo.txt" line 1:
Vote type "fua_testvote" already exists
```

Without the announcer one, each restart appends another "Default" profile to the list and
`g_DefaultAnnouncer` keeps pointing at the previous set's row.

## What is unit-tested

`computation/gamemodetable_compute.*` owns the two pure rules `gamemode.cpp` used to inline: how
one `AddFlag`/`RemoveFlag` directive folds into a mode's flag word, and whether the resulting row
is self-consistent. `gamemodetable_compute_test.cpp` replays the exact boot-1 / boot-2 sequence
above through those real functions, so the regression is pinned off-engine.

The clears themselves are three lines of glue with nothing to compute; they are verified in the
engine (launch with a mod that repoints Domination, `wad_reload -`, confirm it comes back up).
