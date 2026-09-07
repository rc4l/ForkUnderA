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

	if (in.offerableCount <= 0)
		return out;			// Hidden

	out.mode = ContinueMode::Continue;
	out.target = in.newestTarget;

	// [rc4l] Counted from the ROWS, not from the ones we would act on. A history of two where only
	// one is pressable is still two things in front of the player, and a press that skipped the
	// question threw them into a rehost they never chose.
	//
	// One row and one row only is the exception: the pill already names it, and putting a menu in
	// front of a single row would turn the one-press feature this started as into two presses for
	// no decision.
	out.opensList = (in.listCount > 1);
	return out;
}

} // namespace zx
