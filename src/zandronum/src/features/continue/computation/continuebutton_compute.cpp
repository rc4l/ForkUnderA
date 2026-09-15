// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/computation/continuebutton_compute.h"

namespace zx
{

ContinueButtonVerdict DecideContinueButton(const ContinueButtonInputs &in)
{
	ContinueButtonVerdict out;

	if (in.inSession)
	{
		// Always offered, because leaving is always possible. Where it lands is the only question,
		// and the main menu is the floor: somebody who joined straight from the browser has no
		// offline session to go back to and must still end up somewhere deliberate.
		out.mode = ContinueMode::Disconnect;
		out.target = in.localUsable
			? (in.localIsHosted ? ContinueTarget::Hosted : ContinueTarget::Offline)
			: ContinueTarget::MainMenu;

		// [rc4l] It asks in here too. Leaving used to be one act performed on the spot, which is
		// defensible and was not what anybody expected: the same button one press earlier had opened
		// a list, so pressing it again read as "open the list" and instead threw them out of the
		// game. The list in a session leads with leaving, so the immediate act is still one keystroke
		// away -- and going straight to another remembered session no longer means leaving first and
		// pressing again.
		out.opensList = true;
		return out;
	}

	if (in.offerableCount <= 0)
		return out;			// Hidden

	out.mode = ContinueMode::Continue;
	out.target = in.newestTarget;

	// [rc4l] ALWAYS. It used to skip the list for a single row, on the reasoning that a one-row menu
	// is a click charged for nothing -- and in isolation that is true. What it cost was
	// PREDICTABILITY: the same button sometimes asked and sometimes acted, and which it did depended
	// on a count the player cannot see. Every report about this button has been a version of "it did
	// something when I expected it to ask", including one from a history trimmed to a single entry.
	//
	// A button that always asks can be learned in one press. One that asks most of the time cannot be
	// learned at all.
	out.opensList = true;
	return out;
}

} // namespace zx
