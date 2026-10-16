# features/handoff

The line between the game and the main thread. Tics run on their own thread (`fua_gamethread`) or inline, and
either way the main thread touches game data only in the handoff, the stretch between tics; everything else it
draws comes from `features/gamecopy`'s copy, and everything a tic sends out goes through a queue.

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
- `loop.cpp` `RunTic`: each tic as TryRunTics runs it, the handoff's work before it (`handoff/before_tic`: console
  and menu tickers, tic-scheduled RPCs, the seen bits the frames marked), G_Ticker's halves (`handoff/ticker_begin`,
  `handoff/ticker_end`) around the playsim's tic (`tic/playsim`), and after it (`handoff/after_tic`: the profiler,
  the trace, gametic, input and the render copy). `FrameBegins` and `FrameEnds` take the main thread out of the
  handoff for a single-player frame, so a handoff seam a frame enters counts as a violation.
- `fua_gamethread` (on by default; `loop.cpp` `GameThreadFrame`, `gamethread.cpp`): the playsim's part of a tic (`P_Ticker`) on one
  worker, started by the handoff after `G_TickerBegin` and taken back after the next tic boundary, so its result
  shows exactly a tic later than inline, smoothly, the frame's blend counted from that boundary (`I_GetTime(true)`).
  Frames beside it (`D_DisplayBesideTic`) read only the copy and the recorded HUD and take no input; one drawn
  after its boundary holds positions (`handoff/held_frames`). `computation/threadedtic_compute` decides each tic's
  schedule: inline whenever GL draws, a wipe, the automap, a menu or the console is up, the demo loop moves on, or
  `fua_copy_check` holds the copy to the live game.
  What the worker throws is rethrown on
  the main thread when it is taken back, and a quit or restart takes it back first.
- `hud.cpp` `RecordHUD`/`ReplayHUD`: the level's HUD drawn into a capture (`features/draw2d` `BeginCapture2D`) in the
  handoff when a tic has run or the layout changed (`computation/hudrecord_compute`), and replayed by every frame
  GL does not draw; `fua_hud_record 0` draws it live, which `fuactl fixture hudcheck` holds it to.
- `effects.cpp`: what a tic on its own thread does to the main thread's state, queued in order and done in the
  handoff after it: prints (`PrintString`) and UI effects (`AM_Stop`, `C_HideConsole`, `M_StartControlPanel`,
  `M_SetMenu`, `M_ClearMenus`, `R_ResetViewInterpolation`, `R_ClearPastViewer`, the Strife conversation's menu).
  `Deferred<T>` does the same for values a tic makes for a store the frame reads: the projected decals' marks and
  forgotten owners (`features/levelmesh/projdecals.cpp`, whose marks load their pictures) and the hitbox overlay's
  blasts (`features/hitboxviz`); `wad/read` makes any lump read from the game thread a violation.
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
  idle only) and `wipe/randoms`. `fuactl fixture seamcheck` fails a fixture on any violation.
- Shared engine data both threads read: `FString` buffers are counted atomically, and the name table takes a lock
  and keeps every array it outgrew (`name.cpp`).
- `computation/lock_compute`: `zx::Lock`, the one lock for state both threads touch, named, counting the takes that
  waited, and reporting a thread that takes one while it holds another, so no two can be taken in opposite orders.
- `computation/queue_compute`: `zx::Queue<T>`, the one queue that carries work between the threads: values only,
  drained oldest first, each listed with its depth and high-water mark.

Rules the boundary keeps are in `tools/boundary-rules.json`, checked by `tools/boundary_tripwire.py` against
allowances in `tools/boundary-allow.json` that may only shrink.

## In-engine hooks

- `d_main.cpp` `D_DoomMain`: `NoteMainThread` first, so every seam's check knows which thread is main.
- `d_net.cpp` `TryRunTics`: each tic is `RunTic`; `Net_TicsDue` (the tics due, input taken, none run) and
  `Net_PendingLocalTurn` (the turn built and not yet shown, which `R_InterpolateView` adds beside a tic).
- `d_main.cpp` `D_RunFrame`: `GameThreadFrame` first in the single-player branch, the frame-top work that can load
  wads or maps or answer RPCs only with no tic in flight, and `FinishInFlight` before a quit or a restart;
  `D_Display` defers a mode or view size change while one is in flight.
- `g_game.cpp` `G_AddViewAngle`/`G_AddViewPitch`: beside a tic they read the console player and level from the copy.
- `mcp_crash.cpp`: the hang watchdog watches the game thread's heartbeat while a tic is in flight.
- `features/jobs/jobs.cpp`: the pool leaves a core for the game thread when it is on.
- `g_game.cpp` `G_Ticker`: `G_TickerBegin`, `G_TickerPlaysim` and `G_TickerEnd`, the playsim's tic split out.
- `d_main.cpp` `D_RunFrame`'s single-player branch: `D_PrepareDisplay` (the render copy, its check and the console
  player's camera) in the handoff, then `D_Display` between `FrameBegins` and `FrameEnds`.
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
- level: `time`, `maptime`, `flags`, `flags2`, `skyspeed1`, `skyspeed2`, `fadeto`, `outsidefog`
- level, worked out by the fill: `sky1`, `sky2`
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
- `handoff/after_tic` (Handoff), `features/handoff/loop.cpp`
- `handoff/before_tic` (Handoff), `features/handoff/loop.cpp`
- `handoff/effects` (Handoff), `features/handoff/effects.cpp`
- `handoff/hud_record` (Handoff), `features/handoff/hud.cpp`
- `handoff/snapshot` (Handoff), `features/handoff/snapshot.cpp`
- `handoff/ticker_begin` (Handoff), `features/handoff/loop.cpp`
- `handoff/ticker_end` (Handoff), `features/handoff/loop.cpp`
- `sound/channels` (GameData), `s_sound.cpp`
- `sound/deferred` (Handoff), `s_sound.cpp`
- `sound/music` (GameData), `s_sound.cpp`
- `tic/playsim` (GameThread), `features/handoff/loop.cpp`
- `wad/read` (Main), `resourcefiles/resourcefile.cpp`
- `wad/read` (Main), `w_wad.cpp`
- `wipe/randoms` (Idle), `features/wipe/wiperandoms.cpp`
<!-- boundary:end -->
