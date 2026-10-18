// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The handoff loop: each tic is the handoff's work before it, the playsim's tic on the schedule fua_gamethread picks, and the handoff's work after it, every part a seam.

#ifndef ZX_GAMELOOP_LOOP_H
#define ZX_GAMELOOP_LOOP_H

namespace zx { namespace gameloop {

// [rc4l] One tic as TryRunTics runs it: the console and menu tickers, tic-scheduled RPCs and the seen bits before it, G_Ticker's halves around the playsim, and the profiler, trace, gametic, input and render copy after it.
void RunTic();

// [rc4l] fua_gamethread's single-player frame: a tic's playsim started on the game thread, a frame drawn beside one in flight, or the one in flight taken back on its boundary; false when this frame runs inline instead.
bool GameThreadFrame();
// [rc4l] Takes a tic in flight back, waiting for it, before a quit or anything else that needs the game.
void FinishInFlight();
// [rc4l] The tics run on the game thread and the frames beside one that held positions, since the start.
int ThreadedTics();
int HeldFrames();

// [rc4l] Marks the main thread drawing a frame (false) or back in the handoff (true), so a seam that only the handoff may enter counts a frame's touch of game data.
void FrameBegins();
void FrameEnds();

}} // namespace zx::gameloop

#endif // ZX_HANDOFF_LOOP_H
