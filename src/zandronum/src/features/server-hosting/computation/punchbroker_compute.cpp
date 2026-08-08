// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-hosting/computation/punchbroker_compute.h"

namespace zx
{

PunchVerdict DecidePunch(const PunchAsk &ask, int maxRequests)
{
	// Rate first, deliberately. It is the one refusal that exists to protect us rather than to
	// inform the caller, so somebody flooding is turned away before we look anything up for them.
	if ((maxRequests > 0) && (ask.recentRequests >= maxRequests))
		return PunchVerdict::RateLimited;

	// Then the answer almost every request gets. Every Zandronum server ever shipped, and every
	// build of ours older than this, cannot punch: there is nobody to instruct, so say so at once
	// instead of making the client wait out a timeout to learn it.
	if (!ask.serverSupportsPunch)
		return PunchVerdict::NoSupport;

	// We introduce people to servers WE have verified, not to whatever address was asked about. A
	// stranger naming an arbitrary target is the shape of the attack this whole unit is built
	// against, and refusing here is what stops the request going any further.
	if (!ask.serverListed)
		return PunchVerdict::NotListed;

	// Last, because it is the expensive one and the only one that proves anything: the requester has
	// echoed a cookie sent to the address it claims to hold. Without this the observed source could
	// be spoofed and the host would be aimed at a victim who never asked for a packet.
	if (!ask.cookieProven)
		return PunchVerdict::BadCookie;

	return PunchVerdict::Broker;
}

PunchIntent DecidePunchIntent(bool fromList, bool lan, bool registryAnswering)
{
	// Same network, so nothing is crossing a router and there is no hole to make. Asking a machine
	// on the internet to introduce two computers in the same house would be absurd, and slower.
	if (lan)
		return PunchIntent::Skip;

	// A typed or pasted address has no registry entry behind it, so the only possible answer is
	// NotListed. Skipping saves a round trip to be told what we already know.
	if (!fromList)
		return PunchIntent::Skip;

	// Nothing has been heard from the registry lately. The fallback would cover this, but paying a
	// timeout on every single join to rediscover that it is still down is not a cost worth carrying.
	if (!registryAnswering)
		return PunchIntent::Skip;

	return PunchIntent::Ask;
}

int PunchAttemptDelayMs(int attempt)
{
	// 0, 100, 300, 700, 1500. Doubling, because a punch that works almost always works on the first
	// or second try and the later attempts are there for the slow cases only. Repeating flat would
	// spend the same packets where they are least likely to help.
	if (attempt <= 0)
		return 0;

	if (attempt >= kPunchAttempts)
		attempt = kPunchAttempts - 1;

	int span = 1;
	for (int i = 0; i < attempt; ++i)
		span *= 2;

	return 100 * (span - 1);
}

} // namespace zx
