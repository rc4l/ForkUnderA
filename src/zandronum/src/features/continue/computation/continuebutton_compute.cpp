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
		return out;
	}

	if (in.usableCount <= 0)
		return out;			// Hidden

	out.mode = ContinueMode::Continue;
	out.target = in.newestTarget;

	// [rc4l] Two or more is a choice, and a choice is what the list is for. One is not: the pill
	// already names it, and putting a menu in front of a single row would turn the one-press feature
	// this started as into two presses for no decision.
	out.opensList = (in.usableCount > 1);
	return out;
}

} // namespace zx
