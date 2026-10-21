// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-hosting/computation/reachprobe_compute.h"

namespace zx
{

// 4 seconds. This leg goes back down the path our own request just opened, so it does not depend on
// any forwarding -- if it is slow, the registry is slow.
const int kCookieTimeoutMs = 4000;

// 8 seconds. This one crosses the open internet to a port a router has to forward, and a busy home
// router can sit on the first packet for a while. Long enough not to call a working setup broken.
const int kProbeTimeoutMs = 8000;

// 10 minutes. Long enough to spare a player who is fiddling with the host form from re-probing on
// every visit, short enough that a router reboot or a lease change is noticed in the same sitting.
const int kProbeCacheTtlMs = 600000;

// 5 seconds, which clears the registry's 3 second flood window with room to spare. Long enough that
// a registry which is genuinely down is not hammered, short enough that the player never sees the
// retry happen.
const int kFailedCacheTtlMs = 5000;

bool ProbeIsFinished(ProbePhase phase)
{
	return (phase == ProbePhase::Reachable) || (phase == ProbePhase::Unreachable)
		|| (phase == ProbePhase::Failed);
}

bool ProbeSaysReachable(ProbePhase phase)
{
	return phase == ProbePhase::Reachable;
}

ProbeDisplay ProbeDisplayFor(ProbePhase phase)
{
	if (phase == ProbePhase::Reachable)
		return ProbeDisplay::Reachable;

	if (phase == ProbePhase::Unreachable)
		return ProbeDisplay::Unreachable;

	// Idle, both waiting states, and Failed. Failed belongs here rather than with Unreachable: it
	// means the registry never answered, which is a fact about our service and not about the
	// player's port, and saying "not forwarded" on the strength of it would be a confident lie.
	return ProbeDisplay::Unknown;
}

ProbePhase StepProbe(ProbePhase phase, bool cookieArrived, bool probeArrived, int elapsedMs)
{
	switch (phase)
	{
	case ProbePhase::AwaitingCookie:
		if (cookieArrived)
			return ProbePhase::AwaitingProbe;
		if (elapsedMs >= kCookieTimeoutMs)
			return ProbePhase::Failed;		// the registry, not the player's router
		return phase;

	case ProbePhase::AwaitingProbe:
		// [rc4l] The probe checked BEFORE the deadline, so a packet that lands on the last millisecond
		// still counts. Deciding "too late" about something already in hand would fail a player whose
		// port is genuinely open.
		if (probeArrived)
			return ProbePhase::Reachable;
		if (elapsedMs >= kProbeTimeoutMs)
			return ProbePhase::Unreachable;
		return phase;

	case ProbePhase::Idle:
	case ProbePhase::Reachable:
	case ProbePhase::Unreachable:
	case ProbePhase::Failed:
		// Terminal, or not started. Late packets keep arriving after a verdict: a retransmitted
		// probe, a duplicate cookie. None of them may restart or rewrite a finished answer.
		//
		// [rc4l] Falls out to the return below rather than returning here. Returning from every case
		// would leave the closing statement unreachable, which is a line no test can cover and a
		// coverage gate can only be lied to about.
		break;
	}

	return phase;
}

bool ProbeNonceMatches(const std::string &expected, const std::string &received)
{
	// An empty expectation matches nothing. Otherwise a client that failed to generate a nonce would
	// accept every packet that reached the socket, which is the exact false positive the nonce is
	// there to prevent.
	if (expected.empty())
		return false;

	return expected == received;
}

bool ProbeCacheKeyMatches(const ProbeCacheKey &a, const ProbeCacheKey &b)
{
	return (a.port == b.port) && (a.publicIp == b.publicIp) && (a.localSubnet == b.localSubnet);
}

bool ProbeCacheUsable(const ProbeCacheKey &cached, const ProbeCacheKey &now, int ageMs,
	ProbePhase cachedPhase)
{
	// Never usable before it was recorded. Clocks go backwards, a resync or a laptop waking, and a
	// negative age would otherwise pass the TTL test and keep a stale answer alive indefinitely.
	if (ageMs < 0)
		return false;

	// Failed expires on its own short clock, so the next visit re-asks instead of repeating a verdict
	// that only ever meant "we did not hear back". See kFailedCacheTtlMs.
	const int ttl = (cachedPhase == ProbePhase::Failed) ? kFailedCacheTtlMs : kProbeCacheTtlMs;

	if (ageMs >= ttl)
		return false;

	return ProbeCacheKeyMatches(cached, now);
}

bool ComputeShouldTryOtherFamily(ProbePhase verdict, bool bTryingV6, bool bTriedV4, bool bTriedV6,
                                 bool bOtherFamilyAvailable)
{
	// Not a verdict yet: nothing to spend a second attempt on.
	if (ProbeIsFinished(verdict) == false)
		return false;

	// One family answering yes is the whole answer.
	if (ProbeSaysReachable(verdict))
		return false;

	if (bOtherFamilyAvailable == false)
		return false;

	return bTryingV6 ? (bTriedV4 == false) : (bTriedV6 == false);
}

ProbeDisplay ComputeJoinableDisplay(ProbePhase phase)
{
	// The port is open: dialable by anyone, no introduction needed.
	if (phase == ProbePhase::Reachable)
		return ProbeDisplay::Reachable;

	// The port is shut, but the registry answered our cookie leg to tell us so -- which is the same
	// registry, reachable the same way, that brokers a punch. People get in.
	if (phase == ProbePhase::Unreachable)
		return ProbeDisplay::Reachable;

	// The registry never answered: no direct path and nobody to introduce us. This is the one state
	// where nothing is going to work, and the only one worth painting as a problem.
	if (phase == ProbePhase::Failed)
		return ProbeDisplay::Unreachable;

	// Idle and both waiting states: nothing is known yet.
	return ProbeDisplay::Unknown;
}

bool ComputeJoinableViaRelay(ProbePhase phase)
{
	return (phase == ProbePhase::Unreachable);
}

} // namespace zx
