# bot-save

Bots survive a savegame.

## They were already being saved

Bots occupy real player slots, so `P_SerializePlayers` writes their name, inventory, frags and their
actor exactly as it does for a human. They were lost on **load**, in one line:

```c
playerUsed[i] = playeringame[i] ? 0 : 2;   // 2 == unusable
```

Saved players are matched by name only against slots **already occupied**. Load into a fresh session
and only the human's slot is, so the saved bots have nowhere to land and are dropped with their data
unread. Measured before this change: 3 players before the save, 1 after the load.

## A chunk, not a change to the save stream

`botS` carries the roster — slot, name, team — and the loader uses it to re-occupy those slots
*before* the matcher runs. Everything else is then restored by the engine's own machinery.

Zandronum already does exactly this for skirmishes: `mpEm` is one byte recording
`NETSTATE_SINGLE_MULTIPLAYER`, appended beside the image. Following that shape means an absent chunk
is simply "no bots", so every save ever written still loads, `SAVEVER` does not move, an older engine
ignores it, and the diff stays out of upstream's `ReadMultiplePlayers`, which every load path shares.

## What is carried, and what is refused

Tier one of `CSkullBot` only: plain integers and bools describing how the bot was moving and aiming.

Deliberately absent:

* `m_pGoalActor` — a raw pointer. Written as one it is a use-after-free; written as an index it
  silently names a different actor, and the bot chases the wrong thing.
* The player indices it holds grudges by — that slot may belong to somebody else after a load.
* `SCRIPTDATA_t` — a paused interpreter: a program counter, a call stack and a string stack that
  only mean anything against the exact script that was loaded.

A bot restored without its intent picks a new target within a tic or two, which no player can
perceive. What players do notice is where the bot is standing and what it is carrying, and that is
restored exactly.

## Two hazards handled

* **An unknown bot name.** The constructor looks the name up and, finding nothing, leaves its index
  past the end of the definitions and then indexes with it. Names are checked with `BOTS_IsValidName`
  first and an unknown one is passed as `NULL`, taking the random-bot branch instead. This is normal,
  not corrupt: it happens whenever a save is loaded without the mod whose bots it used.
* **A stale slot number.** A slot that is already occupied is never re-occupied, so a bad chunk
  cannot evict the human.

A chunk that does not parse is treated as "no bots" and never fails the load.

## In-place engine edits

| File | Edit |
|---|---|
| `src/CMakeLists.txt` | registers `features/bot-save/zx_botsave.cpp` |
| `g_game.cpp` | `BotSave_Write` beside the `mpEm` append; `BotSave_Restore` before `G_InitNew` |
