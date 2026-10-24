# features/gameloop

The single-player loop: each tic's schedule, inline or on the game thread, the handoff's work around it, and the
HUD recorded for the frames drawn beside it. It sits above `features/gamecopy`, `features/frame` and
`features/draw2d`, and uses `features/handoff`'s seams, queues and worker.

- `loop.cpp` `RunTic`: each tic as TryRunTics runs it, the handoff's work before it (`handoff/before_tic`: console
  and menu tickers, tic-scheduled RPCs, the seen bits the frames marked), G_Ticker's halves (`handoff/ticker_begin`,
  `handoff/ticker_end`) around the playsim's tic (`tic/playsim`), and after it (`handoff/after_tic`: the profiler,
  the trace, gametic, input and the render copy). `FrameBegins` and `FrameEnds` take the main thread out of the
  handoff for a single-player frame, so a handoff seam a frame enters counts as a violation.
- `fua_gamethread` (on by default; `loop.cpp` `GameThreadFrame`, `features/handoff/gamethread.cpp`): the playsim's
  part of a tic (`P_Ticker`) on one worker, started by the handoff after `G_TickerBegin` and taken back after the next tic boundary, so its result
  shows exactly a tic later than inline, smoothly, the frame's blend counted from that boundary (`I_GetTime(true)`).
  Frames beside it (`D_DisplayBesideTic`) read only the copy and the recorded HUD and take no input; one drawn
  after its boundary holds positions (`handoff/held_frames`). `computation/threadedtic_compute` decides each tic's
  schedule: inline whenever GL draws, a wipe, the automap, a menu or the console is up, the demo loop moves on, or
  `fua_copy_check` holds the copy to the live game.
  A quit or restart takes it back first.
- Catch-up while tics run threaded (queue #70): a frame owed more than one tic runs inline only those that fit
  `kInlineCatchUpMs` at the last tic's playsim cost (`ComputeInlineCatchUp`), and the rest go to the game thread a
  handoff each with frames drawn beside them; `NetUpdate` makes input at most `kThreadedTicBacklog` tics ahead
  (`ComputeTicBacklogLimit`), so a game whose tics cost more than a tic's time slows down rather than freezing
  a frame per owed tic or answering the player later and later. `handoff/backlog_tics` and
  `handoff/inline_catchup` count both; `fua_debug_playsim_us` makes every tic's playsim that slow
  (`fuactl scenarios ticoverload`). The inline schedule (`fua_gamethread 0`) still catches up in one frame, as stock does.
- `hud.cpp` `RecordHUD`/`ReplayHUD`: the level's HUD drawn into a capture (`features/draw2d` `BeginCapture2D`) in the
  handoff when a tic has run or the layout changed (`computation/hudrecord_compute`), and replayed by every frame
  GL does not draw; `fua_hud_record 0` draws it live, which `fuactl fixture hudcheck` holds it to.
- After each tic it copies the projected decals' owner alpha (`features/levelmesh` `CopyProjectedDecalOwners`)
  with the render copy, so a frame beside a tic never reads an engine decal.
- A pending mode or view size change (`setmodeneeded`, `setsizeneeded`) keeps the next tic inline, so a frame with
  no tic in flight applies it; `fua_gamethread_report` prints the tics run on the thread and the frames held.
- `P_Ticker` leaves whether the tic moved anything in `p_NoInterpolate`, which `TickerEnd` gives the renderer's
  `r_NoInterpolate` with the tic taken back; set from the game thread it froze a frame beside the tic at the full
  blend, and the view lurched a tic and stepped back (queue #51). A tic is taken back on the clock tic after the
  one it started in, where the frame's blend is anchored. Before each tic `TickerBegin` notes the loaded fonts
  (`V_FontsForTic`), and each frame beside one serves what it asked the main thread for (`ServeAsks`).

## In-engine hooks

- `d_net.cpp` `TryRunTics`: each tic is `RunTic`; `Net_TicsDue` (the tics due, input taken, none run) and
  `Net_PendingLocalTurn` (the turn built and not yet shown, which `R_InterpolateView` adds beside a tic).
  `NetUpdate` holds the input made ahead to `ComputeTicBacklogLimit` while `TicsRunThreaded`.
- `d_main.cpp` `D_RunFrame`: `GameThreadFrame` first in the single-player branch, the frame-top work that can load
  wads or maps or answer RPCs only with no tic in flight, and `FinishInFlight` before a quit or a restart;
  `D_Display` defers a mode or view size change while one is in flight.
- `g_game.cpp` `G_AddViewAngle`/`G_AddViewPitch`: beside a tic they read the console player and level from the copy.
- `features/jobs/jobs.cpp`: the pool leaves a core for the game thread when it is on.
- `g_game.cpp` `G_Ticker`: `G_TickerBegin`, `G_TickerPlaysim` and `G_TickerEnd`, the playsim's tic split out.
- `p_tick.cpp` `P_Ticker`: sets `p_NoInterpolate` where it set `r_NoInterpolate`; `cl_demo.cpp`'s skipped tics hand
  it over the same way.
- `d_main.cpp` `D_RunFrame`'s single-player branch: `D_PrepareDisplay` (the render copy, its check and the console
  player's camera) in the handoff, then `D_Display` between `FrameBegins` and `FrameEnds`.
