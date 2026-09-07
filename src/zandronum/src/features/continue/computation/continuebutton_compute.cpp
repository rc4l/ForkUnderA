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
