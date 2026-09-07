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

	// [rc4l] How many entries one press may actually act on. The pill exists when this is more than
	// none: a button that is offered and then fails is worse than no button.
	int offerableCount;

	// [rc4l] How many rows the LIST would show, which is a larger number and a different question.
	//
	// Whether to ASK follows from what the player can see, not from what we have worked out. A
	// history holding a dead server and a game to rehost shows two rows; deciding that only one of
	// them is pressable and therefore silently doing that one means a press the player expected to
	// open a menu instead threw them into a session -- which is exactly what it did.
	int listCount;

	// The newest usable entry's kind, so the pill can name where one press would take them.
	ContinueTarget newestTarget;

	// [rc4l] Where LEAVING lands, which is a different question and has to be answered from a
	// different entry. The newest entry after a join is the server being left, so a Disconnect that
	// read it would put the player back into the game they just asked to leave. What it wants is the
	// newest entry that is not a server: the local game or hosted match they were in before.
	bool localUsable;
	bool localIsHosted;

	ContinueButtonInputs()
		: inSession(false), offerableCount(0), listCount(0),
		  newestTarget(ContinueTarget::None), localUsable(false), localIsHosted(false) {}
};

struct ContinueButtonVerdict
{
	ContinueMode mode;
	ContinueTarget target;

	// [rc4l] Whether pressing it should ASK. Only a genuinely single-row history skips the question:
	// with anything else on screen the player is choosing, whether or not we think one of the rows
	// would fail.
	bool opensList;

	ContinueButtonVerdict()
		: mode(ContinueMode::Hidden), target(ContinueTarget::None), opensList(false) {}
};

ContinueButtonVerdict DecideContinueButton(const ContinueButtonInputs &in);

} // namespace zx

#endif // ZX_CONTINUEBUTTON_COMPUTE_H
