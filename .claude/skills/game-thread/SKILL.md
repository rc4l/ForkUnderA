---
name: game-thread
description: How code in ForkUnderA crosses between the game thread and the main thread, and how to check it. Use when touching playsim code that reaches the renderer, sound, menus, console, fonts or textures; when renderer code reads anything of the game; when a report says rubber-banding, the view stepping back, sound acting up, or anything that only happens with fua_gamethread on; or before calling a game-thread change done.
---

# The game thread

With `fua_gamethread` on (single-player, Vulkan alone), each tic's playsim runs on its own thread while the main
thread draws. `src/zandronum/src/features/handoff/README.md` and `features/gameloop/README.md` are the spec; their
generated sections list every seam, every copied field and every file that hands work across, so read them rather
than a list here.

## The rules, and the one-line way to keep each

- **A tic never touches the main thread's state** (the renderer, sound device, menus, console, automap, fonts, view
  globals). A function that does queues itself: `if (zx::handoff::QueuedForHandoff ({ ... })) return;`, or
  `RunOrQueue` for a single action. One whose answer the tic needs now asks the main thread and waits:
  `zx::handoff::AskMainFor ([&] { font = V_GetFont (name); });`.
- **A frame never reads the live game.** It reads `features/gamecopy`'s copy (`Record`, `Player`, `Level`, a
  sector's or side's `.rc`). A value the frame needs once, such as whether the console forces the blend, is worked
  out once on the main thread (`r_ActorFrac`), never per sprite on the job threads.
- **Only a tic a frame draws fills the copy.** Catch-up tics run back to back (`RunTic (drawn)`,
  `ComputeTicDrawn`) leave the scan and fill to the last of the batch, which takes every change since; game code
  inside a tic reads the copy only while a tic is in flight on the game thread.
- **A query must not act.** `P_CheckTickerPaused` pauses the sound; frames ask `P_TickerPausedNow`. Before a frame
  calls a game helper, read what it calls.
- **Mark the crossing.** `ZX_SEAM_UNTIMED ("group/name", Main)` on anything only the main thread may do turns every
  slip into a counted violation with its site.

## Checking it

```bash
cd tools/fuactl
python3 ../boundary_tripwire.py                      # game code setting a renderer global, frames reading the live game
node src/cli.mjs fixture check                       # the traces under each schedule; --cvar fua_gamethread=0, and =1,fua_gamethread_stress=200
node src/cli.mjs fixture seamcheck                   # no seam entered by a thread its owner does not allow; names the site when one is
node src/cli.mjs fixture pictures|hudcheck|copycheck
node src/cli.mjs walkcheck --port 7799 --runs 8      # W held: no frame steps back or lurches; the first run after a launch lurches under either schedule
```

- A new crossing gets a fixture ingredient that fails first: gtmod's script loads a font and changes the sky from a
  tic, which `seamcheck` failed on before `AskMainFor` and the queued sky.
- In a live session, `ui exec fua_seams` lists every violating site by thread and phase (a tic running or not, the
  main thread drawing or in the handoff), and `fua_gamethread_report` the tics run on the thread and frames held.
- A fault tied to what the player did: `window --activate 0|1 --port P` tells the instance it was alt-tabbed away or
  back with nothing on the desktop moved; `toggleconsole` and the menu are console commands. Reproduce their state
  exactly: the sound violations that brought in the per-site report needed the console open, not a fight.
- From a player's save, `load` first and send everything else after the level is up: a command chained after `load ...; wait N` dies with the level's thinkers, and `god` or a setting silently never happens.
- Frame rate in a fight: `record --seconds 120 --name X --recap` while they play, which prints frames, tics on the
  thread and held frames a second and what the slow seconds spent more on. In single-player the console and menus
  pause the game, so a paused frame capped at the tic rate is not a slowdown.
