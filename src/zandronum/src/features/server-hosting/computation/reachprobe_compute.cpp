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

bool ProbeIsFinished(ProbePhase phase)
{
	return (phase == ProbePhase::Reachable) || (phase == ProbePhase::Unreachable)
		|| (phase == ProbePhase::Failed);
}

bool ProbeSaysReachable(ProbePhase phase)
{
	return phase == ProbePhase::Reachable;
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

bool ProbeCacheUsable(const ProbeCacheKey &cached, const ProbeCacheKey &now, int ageMs)
{
	// Never usable before it was recorded. Clocks go backwards -- a resync, a laptop waking -- and a
	// negative age would otherwise pass the TTL test and keep a stale answer alive indefinitely.
	if (ageMs < 0)
		return false;

	if (ageMs >= kProbeCacheTtlMs)
		return false;

	return ProbeCacheKeyMatches(cached, now);
}

} // namespace zx
