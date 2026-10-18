// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Whether a menu or the console pauses the playsim's tic, worked out apart from pausing the sound, so a frame can ask it without touching the sound device.

#ifndef ZX_TICKERPAUSE_COMPUTE_H
#define ZX_TICKERPAUSE_COMPUTE_H

namespace zx { namespace gameloop {

// [rc4l] What P_CheckTickerPaused reads to decide.
struct TickerPauseIn
{
	bool client;        // [rc4l] a network client, whose tics the server runs
	bool titleLevel;    // [rc4l] the title map, which runs under any menu
	bool menuPauses;    // [rc4l] a menu that pauses is up
	bool consoleDown;   // [rc4l] the console is down or falling
	bool demo;          // [rc4l] a demo of either kind plays or records
	bool viewzUnset;    // [rc4l] the console player's viewz is still 1, before its first tic
	bool wipePending;   // [rc4l] a wipe to a new game state is still to come
};

// [rc4l] Whether the tic pauses for a menu or the console.
inline bool ComputeMenuPausesTicker(const TickerPauseIn &in)
{
	return !in.client && !in.titleLevel && (in.menuPauses || in.consoleDown) && !in.demo && !in.viewzUnset && !in.wipePending;
}

}} // namespace zx::gameloop

#endif // ZX_TICKERPAUSE_COMPUTE_H
