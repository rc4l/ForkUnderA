// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] When a tic's playsim runs on the game thread rather than inline: only a level drawn by the backend alone, with nothing up that reads the game every frame or must not wait a tic.

#ifndef ZX_THREADEDTIC_COMPUTE_H
#define ZX_THREADEDTIC_COMPUTE_H

namespace zx { namespace gameloop {

// [rc4l] What decides a tic's schedule.
struct ThreadedTicIn
{
	bool enabled;        // [rc4l] fua_gamethread
	bool inLevel;        // [rc4l] gamestate is GS_LEVEL
	bool glDraws;        // [rc4l] GL draws this frame, which reads the live game
	bool wipePending;    // [rc4l] a wipe starts or runs, which holds the tics back
	bool automap;        // [rc4l] the automap, which draws the live map every frame
	bool menuOrConsole;  // [rc4l] a menu or the console is up, where tics pause anyway
	bool paused;
	bool advanceDemo;    // [rc4l] the demo loop moves on, which loads a level
	bool singleTics;
	bool copyCheck;      // [rc4l] fua_copy_check, which holds the copy to the live game every frame
	bool resizePending;  // [rc4l] a screen mode or view size change, which waits for a frame with no tic in flight
};

// [rc4l] Whether the next tic's playsim may run on the game thread.
inline bool ComputeThreadedTic(const ThreadedTicIn &in)
{
	return in.enabled && in.inLevel && !in.glDraws && !in.wipePending && !in.automap && !in.menuOrConsole && !in.paused &&
		!in.advanceDemo && !in.singleTics && !in.copyCheck && !in.resizePending;
}

// [rc4l] Whether a tic started in clock tic `startedAt` is taken back now, at clock tic `now`: it has finished and a later clock tic has come, so its result starts showing on a tic boundary, where the frame's blend is anchored, never part way through the slot it started in.
inline bool ComputeTakeBackDue(bool finished, int now, int startedAt)
{
	return finished && now > startedAt;
}

// [rc4l] Whether a frame drawn at clock tic `now` holds positions, its tic due back and not finished.
inline bool ComputeHeldFrame(bool finished, int now, int startedAt)
{
	return !finished && now > startedAt;
}

// [rc4l] The most tics of input made ahead of the game while its tics run on the game thread, so an overloaded game slows rather than answering the player later and later; the net layer's own limit, BACKUPTICS/2-1, was a second of input.
const int kThreadedTicBacklog = 3;

// [rc4l] How many tics of input may be made ahead of the game: the net layer's limit, held to kThreadedTicBacklog while tics run threaded.
inline int ComputeTicBacklogLimit(int netLimit, bool threaded)
{
	return threaded && kThreadedTicBacklog < netLimit ? kThreadedTicBacklog : netLimit;
}

// [rc4l] The milliseconds of owed tics a threaded frame may run inline before the rest go to the game thread one a handoff.
const double kInlineCatchUpMs = 6.0;

// [rc4l] How many of `owed` catch-up tics a frame runs inline, each costing about lastTicMs: as many as fit budgetMs, all when no tic has been timed yet, none when one alone would not fit, so cheap tics drain at once and an expensive one never stalls a frame.
inline int ComputeInlineCatchUp(int owed, double lastTicMs, double budgetMs)
{
	if (owed <= 0) return 0;
	if (lastTicMs <= 0.0) return owed;
	const int fit = (int)(budgetMs / lastTicMs);
	return fit < owed ? fit : owed;
}

// [rc4l] Whether a frame draws the tic a batch runs with `ticsAfter` more to come in it: only the last, so the tics before it leave the render copy's scan and fill to that one.
inline bool ComputeTicDrawn(int ticsAfter)
{
	return ticsAfter <= 0;
}

}} // namespace zx::gameloop

#endif // ZX_THREADEDTIC_COMPUTE_H
