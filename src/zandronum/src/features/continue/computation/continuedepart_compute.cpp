// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/computation/continuedepart_compute.h"

namespace zx
{

ContinueDepartVerdict DecideContinueDepart(const ContinueDepartInputs &in)
{
	// Nothing about a crash is a decision the player made.
	if (in.crashing)
		return ContinueDepartVerdict::Ignore;

	// On our way INTO somewhere: the teardown belongs to the arrival, not to a departure.
	if (in.joinInFlight)
		return ContinueDepartVerdict::Ignore;

	// Going straight back to the same server. Returning them to an offline game first would make
	// reconnect impossible to use.
	if (in.reconnecting)
		return ContinueDepartVerdict::Ignore;

	// A destination the player named beats one we remembered for them.
	if (in.goingSomewhereChosen)
		return ContinueDepartVerdict::Ignore;

	// Our own return falling over. Asking for another one is asking for the same failure.
	if (in.returnInFlight)
		return ContinueDepartVerdict::Ignore;

	// Not connected in the first place, so this teardown is tidying up something else.
	if (in.wasInSession == false)
		return ContinueDepartVerdict::Ignore;

	return ContinueDepartVerdict::Return;
}

} // namespace zx
