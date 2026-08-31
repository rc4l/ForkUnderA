// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether leaving a server means "take me back", or is just something happening on the way
// somewhere else.
//
// EVERY exit is meant to route the same -- pressing Disconnect, being kicked, being banned, the
// server dying, a version mismatch -- because a player who ends up somewhere different depending on
// WHY they left has to understand the difference to predict the game.
//
// The danger is that the engine's disconnect is not only used for leaving. CLIENT_QuitNetworkGame is
// the teardown for EVERYTHING, including the tidy-up a successful join does on its way IN, and the
// one `reconnect` performs before it reconnects. Acting on those would yank a player out of a join
// they are half way through and into an old single-player game -- the same shape as the bug that
// produced "Couldn't join Filler14." while the player was already spectating on Filler14.
//
// So: a departure only counts when nothing else is in flight. Anything unsure is ignored, because a
// missed return leaves the player where they already are, and a wrong one destroys a connection they
// were making.
//
// Header-pure by the features/ rules: no engine types.

#ifndef ZX_CONTINUEDEPART_COMPUTE_H
#define ZX_CONTINUEDEPART_COMPUTE_H

namespace zx
{

struct ContinueDepartInputs
{
	bool joinInFlight;		// a join WE started has not landed yet, so this teardown is part of it
	bool reconnecting;		// reconnect is on its way back to the same server
	bool crashing;			// a fatal error is unwinding
	bool wasInSession;		// we were actually connected, rather than tearing down something else

	// [rc4l] The player named where they are going, so we are not the ones to decide it.
	//
	// `map` from a client is a deliberate exit to a new single-player game, and it announces the
	// leaving by calling the same disconnect everything else does (g_level.cpp) before starting the
	// map. Returning them somewhere at that point fights the destination they asked for, which is
	// how "map shoot" ended up somewhere that was not shoot.
	bool goingSomewhereChosen;

	// [rc4l] This teardown is a return of OURS coming apart, and a return that failed must not ask
	// for another one.
	//
	// A rehost that cannot be joined -- wrong files, port taken, server died on startup -- ends in
	// this same disconnect. Treating that as a fresh departure arms another rehost, which fails the
	// same way, forever: a wall of hash mismatches scrolling past with no way to type. The engine
	// was still running and still refusing to do anything else, which is the worst shape a bug can
	// take.
	bool returnInFlight;

	ContinueDepartInputs()
		: joinInFlight(false), reconnecting(false), crashing(false), wasInSession(false),
		  goingSomewhereChosen(false), returnInFlight(false) {}
};

enum class ContinueDepartVerdict
{
	Return,		// take the player back to whatever the button would have taken them to
	Ignore,
};

ContinueDepartVerdict DecideContinueDepart(const ContinueDepartInputs &in);

} // namespace zx

#endif // ZX_CONTINUEDEPART_COMPUTE_H
