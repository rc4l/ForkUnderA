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

	ContinueDepartInputs()
		: joinInFlight(false), reconnecting(false), crashing(false), wasInSession(false) {}
};

enum class ContinueDepartVerdict
{
	Return,		// take the player back to whatever the button would have taken them to
	Ignore,
};

ContinueDepartVerdict DecideContinueDepart(const ContinueDepartInputs &in);

} // namespace zx

#endif // ZX_CONTINUEDEPART_COMPUTE_H
