// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "rowstep_compute.h"

#include "querypunch_compute.h"
#include "rowlifetime_compute.h"

namespace zx
{

RowStepOut StepRow(const RowStepIn &in)
{
	RowStepOut out;

	// Nobody is waiting on this row, so there is nothing to chase and nothing to give up on. This is
	// the state the mirror used to leave rows in, and the answer being "do nothing" is exactly why
	// such a row could never leave the list -- which is rowlifetime_compute's business, not this
	// one's. Here it only means there is no work.
	if (!RowIsAwaitingReply(in.refreshing, in.waitingForReply))
		return out;

	// Which clock this row is on. A re-check is timed from when the re-check went out, a first query
	// from when the first challenge did. Reading the wrong one is how a row expires early or never.
	const int msSince = in.refreshing ? in.msSinceRefresh : in.msSinceQuery;

	// Not sent yet. A queued row is waiting on us, not on the server, and starting its clock here
	// would punish it for our own scheduling.
	if (msSince <= 0)
		return out;

	// The re-check owns its own deadline, and it is the only thing that may end this row. Two
	// expiries racing on one row is how a row disappears out from under a punch in progress.
	//
	// A miss is not a death. The row only goes once it has missed maxRecheckFailures in a row: one
	// dropped datagram says nothing about whether the server is there, and deleting a joinable
	// server off the list is a far worse answer than briefly showing one that has gone.
	if (in.refreshing && (in.refreshTimeoutMs > 0) && (msSince >= in.refreshTimeoutMs))
	{
		out.recheckMissed = true;

		if ((in.maxRecheckFailures > 0) && ((in.recheckFailures + 1) >= in.maxRecheckFailures))
			out.dropFromList = true;

		return out;
	}

	// A LAN row has no NAT between us, so there is nothing an introduction could open; and a row past
	// the sweep's budget waits its turn rather than spending an ask we do not have.
	const bool eligible = (in.lan == false) && (in.punchRequested || in.punchBudgetLeft);

	const QueryPunchStep step = StepQueryPunch(msSince, eligible, in.punchRequested, in.resends);

	if (step.requestPunch)
	{
		out.requestPunch = true;
	}
	else if (step.resendChallenge)
	{
		out.resendChallenge = true;
	}
	else if (step.timeOut)
	{
		// Only the first-query path says "it never answered". A re-check that reaches here has
		// already been handled above, and saying it timed out would keep a row we have decided is
		// gone.
		if (in.refreshing == false)
			out.markTimedOut = true;
	}

	return out;
}

} // namespace zx
