# features/handoff

The line between the game and the main thread. Tics run on their own thread (`fua_gamethread`, scheduled by
`features/gameloop`) or inline, and either way the main thread touches game data only in the handoff, the
stretch between tics; everything else it draws comes from `features/gamecopy`'s copy, and everything a tic
sends out goes through a queue. This folder is the low layer any feature may use; the loop is above it.

- `seam.h`/`.cpp`: `ZX_SEAM("group/name", Owner)` declares a crossing once, and that one line checks the thread
  entering it (`computation/handoff_compute`'s `ComputeOwnerAllows`), times it as a telemetry counter of the same
  name, records it as the seam the thread is in for a crash report and the hang watchdog, beats the thread's
  heartbeat, and under `fua_gamethread_stress <us>` (seeded by `fua_gamethread_stress_seed`) delays about one entry
  in eight to shake out what only breaks when the threads interleave differently. `ZX_SEAM_UNTIMED` is the same
  without the timing, for crossings too frequent to time. `fua_seams` lists every seam with its owner, entries and
  violations; `handoff/seam_violations` counts them, and a soak fails when it grows.
- Owners: `Main` (the main thread), `GameThread` (the thread running tics, which is the main thread while they run
  inline), `GameData` (a running tic, or the main thread in the handoff), `Handoff` (the main thread between tics),
  `Idle` (the main thread while no tic runs, drawing or not) and `Any`. Until the loop sets the phase, the main
  thread counts as always in the handoff.
- `effects.cpp`: what a tic on its own thread does to the main thread's state, queued in order and done in the
  handoff after it: prints (`QueuePrint`) and UI effects (`QueueUI`). A function that acts on the main thread's
  state queues itself in one line, `if (QueuedForHandoff ({ ... })) return;` (`QueuedBesideTic` for a frame beside a
  tic), and a single action runs or queues with `RunOrQueue`; the generated list below names every file that does.
  `Deferred<T>` does the same for values a tic makes for a store the frame reads: the projected decals' marks and
  forgotten owners (`features/levelmesh/projdecals.cpp`, whose marks load their pictures) and the hitbox overlay's
  blasts (`features/hitboxviz`); `wad/read` makes any lump read from the game thread a violation.
- `gamethread.cpp`: the worker that runs a tic's playsim (`StartOnGameThread`, `TakeBack`, `TicInFlight`); what it
  throws is rethrown on the main thread when the tic is taken back. `AskMain` (`AskMainFor` with a lambda) has the
  main thread run a call whose answer the tic needs at once and waits for it, up to a frame (`ServeAsks` each frame
  beside a tic, and while waiting on one; `handoff/asks` counts them), as `V_GetFont` loads a font not among those
  loaded before the tic (`V_FontsForTic`).
- `snapshot.cpp`: the screen's size for ACS `GetScreenWidth`/`GetScreenHeight`, taken before each tic.
- The sound seam (`s_sound.cpp`): a tic on its own thread keeps the channels' bookkeeping, so a sound it starts
  counts as playing for the rest of the tic, but asks nothing of the device; its starts (loaded then), stops and
  volume changes wait in order for `S_RunDeferredSounds`, which the handoff runs as it takes the tic back, on the
  boundary its picture starts showing on, and its music changes and its pauses and resumes of the device go through the UI effects. A page's OpenAL, which
  plays only on the page thread, is reached from the handoff alone that way. A focus change the window reports
  while a tic runs (`S_SetSoundPaused`, which sets `paused`) waits for the handoff the same way (`BesideTic`), as does
  a resize's status bar (`features/video-scale` `ResizeScreenInPlace`); a pending resize keeps the next tic inline,
  and `D_PrepareDisplay` sets the new view up before it records the HUD.
- Seams on shared engine state: `sound/channels` and `sound/music` (s_sound.cpp, game data), `gc/object`
  (every `DObject`, game data), `gc/collect` and `gc/full`, `texture/grow` (a texture added past the headroom,
  idle only), `wipe/randoms`, and the main thread's own `font/load`, `sky/map` and `translation/player`, which a
  tic reaches only through `AskMain` or the UI effects. `fuactl fixture seamcheck` fails a fixture on any
  violation; gtmod loads a font and changes the sky from a tic so it does.
- Shared engine data both threads read: `FString` buffers are counted atomically, and the name table takes a lock
  and keeps every array it outgrew (`name.cpp`).
- `computation/lock_compute`: `zx::Lock`, the one lock for state both threads touch, named, counting the takes that
  waited, and reporting a thread that takes one while it holds another, so no two can be taken in opposite orders.
- `computation/queue_compute`: `zx::Queue<T>`, the one queue that carries work between the threads: values only,
  drained oldest first, each listed with its depth and high-water mark.
- `computation/asks_compute`: `Asks`, the call one thread hands another and waits on, served while the serving thread
  waits on the asker too, rethrowing what the call threw to the asker.

Rules the boundary keeps are in `tools/boundary-rules.json`, checked by `tools/boundary_tripwire.py` against
allowances in `tools/boundary-allow.json` that may only shrink; game code setting any variable a renderer header
declares counts as a crossing too, since that is how queue #51's rubber band got in.

## In-engine hooks

- `d_main.cpp` `D_DoomMain`: `NoteMainThread` first, so every seam's check knows which thread is main.
- `mcp_crash.cpp`: the hang watchdog watches the game thread's heartbeat while a tic is in flight.
- Every file that queues work for the handoff or asks the main thread, listed by helper in the generated section.
- `mcp_crash.cpp`: every crash, abort and hang file names the seam each thread was in (`seams:` line), which
  `fuactl crashes` prints.
- `features/crashreport/zx_crashreport.cpp` `zx_before_send`: the `zx_seam_main` and `zx_seam_game` tags.
- `tools/zone-tripwire.sh`: seam names follow the telemetry counter rules and are declared at one site each.

<!-- boundary:begin (python3 tools/boundary_tripwire.py --doc writes this section) -->
## What crosses the boundary

Generated from the field tables and the `ZX_SEAM` declarations; do not edit by hand.

Copied by `features/gamecopy/fields.h`:

- actor: `x`, `y`, `z`, `PrevX`, `PrevY`, `PrevZ`, `velx`, `vely`, `velz`, `angle`, `PrevAngle`, `pitch`, `roll`, `PrevRoll`, `sprite`, `frame`, `scaleX`, `scaleY`, `RenderStyle`, `renderflags`, `alpha`, `fillcolor`, `Translation`, `flags`, `flags2`, `flags3`, `flags5`, `state`, `tics`, `radius`, `height`, `floorclip`, `floorz`, `ceilingz`, `Speed`, `Sector`, `subsector`, `reactiontime`, `health`
- actor, worked out by the fill: `who`, `id`, `type`, `target`, `nextInSector`, `shownSprite`, `shownScaleX`, `shownScaleY`, `visible`, `isPlayer`, `playerIndex`, `inventory`, `cameraIcon`, `bobTic`, `attackRadius`, `attackHeight`, `playerBody`, `tintActive`, `tintTexture`
- decal: `LeftDistance`, `Z`, `ScaleX`, `ScaleY`, `Alpha`, `AlphaColor`, `Translation`, `PicNum`, `RenderFlags`, `RenderStyle`, `Sector`
- flatdecal: `ContactX`, `ContactY`, `ContactZ`, `Normal`, `Travel`, `AnchorSector`, `AnchorPlane`, `AnchorOffset`
- level: `time`, `maptime`, `flags`, `flags2`, `skyspeed1`, `skyspeed2`, `fadeto`, `outsidefog`
- level, worked out by the fill: `sky1`, `sky2`, `endLevelDelay`, `freeSpectate`
- line: `flags`, `special`, `Alpha`, `args`
- player: `viewz`, `playerstate`, `cheats`, `bSpectating`, `extralight`, `fixedcolormap`, `fixedlightlevel`, `DesiredFOV`, `FOV`, `centering`, `MinPitch`, `MaxPitch`, `timefreezer`, `BlendR`, `BlendG`, `BlendB`, `BlendA`
- player, worked out by the fill: `who`, `number`, `mo`, `camera`, `lightPower`, `hasWeapon`, `weaponFOVScale`, `weaponType`, `weaponYAdjust`, `hudModel`, `bobIn`, `layers`, `screenBlend`
- sector: `planes`, `floorplane`, `ceilingplane`, `ColorMap`, `special`, `lightlevel`, `MoreFlags`, `Flags`, `reflect`
- side: `textures`, `Light`, `Flags`
- vertex: `x`, `y`

Seams:

- `copy/blend` (Main), `features/gamecopy/gamecopy.cpp`
- `copy/fill` (Handoff), `features/gamecopy/gamecopy.cpp`
- `copy/seen` (Handoff), `features/gamecopy/seen.cpp`
- `font/load` (Main), `v_font.cpp`
- `gc/collect` (GameData), `dobjgc.cpp`
- `gc/full` (Handoff), `dobjgc.cpp`
- `gc/object` (GameData), `dobject.cpp`
- `handoff/after_tic` (Handoff), `features/gameloop/loop.cpp`
- `handoff/before_tic` (Handoff), `features/gameloop/loop.cpp`
- `handoff/effects` (Handoff), `features/handoff/effects.cpp`
- `handoff/hud_record` (Handoff), `features/gameloop/hud.cpp`
- `handoff/snapshot` (Handoff), `features/handoff/snapshot.cpp`
- `handoff/ticker_begin` (Handoff), `features/gameloop/loop.cpp`
- `handoff/ticker_end` (Handoff), `features/gameloop/loop.cpp`
- `net/launcher_reply` (Main), `features/federated-server-registry/sv_serverregistry.cpp`
- `net/parse` (Main), `cl_main.cpp`
- `net/receive` (Main), `network.cpp`
- `net/send` (Main), `network.cpp`
- `net/server_tick` (Main), `sv_main.cpp`
- `sky/map` (Main), `r_sky.cpp`
- `sound/channels` (GameData), `s_sound.cpp`
- `sound/deferred` (Handoff), `s_sound.cpp`
- `sound/music` (GameData), `s_sound.cpp`
- `texture/grow` (Idle), `textures/texturemanager.cpp`
- `tic/playsim` (GameThread), `features/gameloop/loop.cpp`
- `translation/player` (Main), `r_data/r_translate.cpp`
- `wad/read` (Main), `resourcefiles/resourcefile.cpp`
- `wad/read` (Main), `w_wad.cpp`
- `wipe/randoms` (Idle), `features/wipe/wiperandoms.cpp`

Handed to the handoff, by helper:

- `AskMainFor`: `p_acs.cpp`, `v_font.cpp`
- `QueuePrint`: `c_console.cpp`
- `QueueUI`: `s_sound.cpp`
- `QueuedBesideTic`: `features/video-scale/videoscale.cpp`, `s_sound.cpp`
- `QueuedForHandoff`: `am_map.cpp`, `c_console.cpp`, `menu/menu.cpp`, `r_data/r_translate.cpp`, `r_sky.cpp`, `r_utility.cpp`, `s_sound.cpp`
- `RunOrQueue`: `p_conversation.cpp`, `p_user.cpp`
<!-- boundary:end -->
