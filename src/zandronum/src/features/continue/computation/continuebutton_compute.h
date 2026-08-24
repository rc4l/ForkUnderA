// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] What the left-hand pill says, and where it goes.
//
// One tab with two jobs, because they are the same job seen from either side: out of a session it
// is the way back IN to what you were doing, and inside one it is the way back OUT to the same
// thing. A second pill would be a second thing to look at that is never useful at the same time.
//
// WHICH IT IS gets ASKED, never stored. The pattern the header already uses for which tab is lit:
// a remembered mode is a second source of truth, and it goes stale the first time anything moves
// the player without telling the bar -- a kick, a server dying, a console connect.
//
// TWO RECORDS, NOT ONE. The last server and the last offline session are remembered separately, so
// joining a server no longer forgets the campaign you were half way through. Out of a session the
// more recent of the two wins, which is what `stamp` is for: a counter bumped on every write, so
// "most recently left" survives a restart without needing a clock.
//
// Header-pure by the features/ rules: no engine types.

#ifndef ZX_CONTINUEBUTTON_COMPUTE_H
#define ZX_CONTINUEBUTTON_COMPUTE_H

namespace zx
{

enum class ContinueMode
{
	Hidden,		// nothing to offer
	Continue,	// out of a session: go back into something
	Disconnect,	// in a session: leave it, and land somewhere sensible
};

enum class ContinueTarget
{
	None,
	Offline,	// whatever was being played locally, restored from its snapshot
	Server,		// rejoin the server we were on
	MainMenu,	// the WAD set the engine was launched with, and nothing loaded
};

struct ContinueButtonInputs
{
	// Connected to a server. NOT merely "a level is running": the title screen plays a demo, so a
	// level is running while the player sits at the main menu.
	bool inSession;

	bool offlineUsable;			// a snapshot we could actually return to
	int offlineStamp;

	bool serverUsable;			// a server record that is still worth offering
	int serverStamp;

	ContinueButtonInputs()
		: inSession(false), offlineUsable(false), offlineStamp(0),
		  serverUsable(false), serverStamp(0) {}
};

struct ContinueButtonVerdict
{
	ContinueMode mode;
	ContinueTarget target;

	ContinueButtonVerdict() : mode(ContinueMode::Hidden), target(ContinueTarget::None) {}
};

ContinueButtonVerdict DecideContinueButton(const ContinueButtonInputs &in);

} // namespace zx

#endif // ZX_CONTINUEBUTTON_COMPUTE_H
