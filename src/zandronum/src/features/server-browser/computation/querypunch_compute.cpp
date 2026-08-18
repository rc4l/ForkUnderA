// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/querypunch_compute.h"

namespace zx
{

QueryPunchStep StepQueryPunch(int elapsedMs, bool punchEligible, bool punchRequested,
	int resendsSent)
{
	QueryPunchStep step;

	if (elapsedMs < 0)
		return step;

	// A slot that never got (or never earned) a punch lives exactly as long as it always has.
	// This branch also catches the slot whose punch budget ran out: it keeps asking (below) until
	// the plain timeout takes it, and the caller's budget decides which happens first.
	if (!punchRequested)
	{
		if (!punchEligible || elapsedMs >= kQueryPlainTimeoutMs)
		{
			step.timeOut = elapsedMs >= kQueryPlainTimeoutMs;
			return step;
		}

		step.requestPunch = elapsedMs >= kQueryPunchAskMs;
		return step;
	}

	// Punch in flight: walk the resend ladder, then the extended timeout.
	if (elapsedMs >= kQueryPunchTimeoutMs)
	{
		step.timeOut = true;
		return step;
	}

	if (resendsSent < 3 && elapsedMs >= kQueryPunchResendMs[resendsSent])
		step.resendChallenge = true;

	return step;
}

bool ShouldPunchBeforeFirstChallenge(bool lan, bool knownUnreachable, bool punchBudgetLeft)
{
	// A machine on our own LAN is reached without crossing a router, so there is no hole to open and
	// nothing a punch could improve.
	if (lan)
		return false;

	return knownUnreachable && punchBudgetLeft;
}

bool ShouldPunchOnFirstContact(bool lan, bool punchBudgetLeft)
{
	if (lan)
		return false;

	return punchBudgetLeft;
}

bool FirstChallengeDue(bool punchLed, bool firstChallengeSent, int punchLedMs)
{
	if (!punchLed || firstChallengeSent)
		return false;

	return punchLedMs >= kQueryPunchLeadMs;
}

bool ShouldAdoptPunchKnock(bool slotWaiting, bool slotPunchRequested, bool sameHost)
{
	return slotWaiting && slotPunchRequested && sameHost;
}

bool ShouldRearmListedSlot(bool slotTimedOut, bool slotInactive, bool slotRefreshing)
{
	if (slotTimedOut)
		return true;

	return slotInactive && !slotRefreshing;
}

} // namespace zx
