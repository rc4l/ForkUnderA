// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/computation/continuebutton_compute.h"

namespace zx
{

namespace
{

ContinueTarget OfflineTarget(const ContinueButtonInputs &in)
{
	return in.offlineIsHosted ? ContinueTarget::Hosted : ContinueTarget::Offline;
}

} // namespace

ContinueButtonVerdict DecideContinueButton(const ContinueButtonInputs &in)
{
	ContinueButtonVerdict out;

	if (in.inSession)
	{
		// Always offered, because leaving is always possible. Where it lands is the only question,
		// and the main menu is the floor: somebody who joined straight from the browser has no
		// offline session to go back to and must still end up somewhere deliberate.
		out.mode = ContinueMode::Disconnect;
		out.target = in.offlineUsable ? OfflineTarget(in) : ContinueTarget::MainMenu;
		return out;
	}

	if ((in.offlineUsable == false) && (in.serverUsable == false))
		return out;			// Hidden

	out.mode = ContinueMode::Continue;

	if (in.offlineUsable && in.serverUsable)
	{
		// Most recently left wins. Ties go to offline: a tie means both were written in the same
		// breath, which is what leaving an offline game FOR a server looks like, and in that pair
		// the server is where the player already is rather than what they left.
		out.target = (in.serverStamp > in.offlineStamp) ? ContinueTarget::Server : OfflineTarget(in);
		return out;
	}

	out.target = in.offlineUsable ? OfflineTarget(in) : ContinueTarget::Server;
	return out;
}

} // namespace zx
