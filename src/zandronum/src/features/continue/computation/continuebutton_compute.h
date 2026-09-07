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
// A HISTORY, NOT A RECORD. What is remembered is a capped list of the different things the player
// has been doing (see continuehistory_compute), so joining a server no longer forgets the campaign
// they were half way through, and neither does the map they tested in between. Out of a session the
// pill opens that list -- unless there is only one thing in it, in which case the press has already
// chosen and an one-row menu would only be in the way.
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
	Hosted,		// a game we hosted, started again from its config: a fresh match on the same terms
	Server,		// rejoin the server we were on
	MainMenu,	// the WAD set the engine was launched with, and nothing loaded
};

struct ContinueButtonInputs
{
	// Connected to a server. NOT merely "a level is running": the title screen plays a demo, so a
	// level is running while the player sits at the main menu.
	bool inSession;

	// [rc4l] How many entries of the history are worth offering right now. The pill exists when this
	// is more than none, and it OPENS A LIST rather than acting when it is more than one.
	int usableCount;

	// The newest usable entry's kind, so the pill can name where one press would take them.
	ContinueTarget newestTarget;

	// [rc4l] Where LEAVING lands, which is a different question and has to be answered from a
	// different entry. The newest entry after a join is the server being left, so a Disconnect that
	// read it would put the player back into the game they just asked to leave. What it wants is the
	// newest entry that is not a server: the local game or hosted match they were in before.
	bool localUsable;
	bool localIsHosted;

	ContinueButtonInputs()
		: inSession(false), usableCount(0), newestTarget(ContinueTarget::None),
		  localUsable(false), localIsHosted(false) {}
};

struct ContinueButtonVerdict
{
	ContinueMode mode;
	ContinueTarget target;

	// [rc4l] Whether pressing it should ASK. With one thing to continue there is nothing to choose
	// between, and a one-row list is a click charged for nothing -- the press already said which one.
	bool opensList;

	ContinueButtonVerdict()
		: mode(ContinueMode::Hidden), target(ContinueTarget::None), opensList(false) {}
};

ContinueButtonVerdict DecideContinueButton(const ContinueButtonInputs &in);

} // namespace zx

#endif // ZX_CONTINUEBUTTON_COMPUTE_H
