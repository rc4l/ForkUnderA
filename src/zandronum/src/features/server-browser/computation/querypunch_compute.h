// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The schedule that lets a CGNAT-hosted server appear in the browser at all.
//
// The registry lists a server behind carrier NAT happily -- its announce goes OUT, so the
// registry can always hear it. But every row the browser shows is built from a DIRECT query to
// the server, and a server nobody port-forwarded drops that packet on the floor. Hole punching
// existed for exactly this and ran only at JOIN -- which needs a visible row, which needs an
// answered query, which needs the punch. This schedule breaks that deadlock: a query that has
// gone unanswered long enough asks the registry to have the server punch toward us, then re-sends
// the challenge a few times so one of them lands in the hole, on the same racing principle the
// join path documents ("the packet has to leave OUR side too").
//
// Pure decision, no I/O: the caller owns sockets, per-slot state, and the punch budget. Header-pure
// per the features/ rules.

#ifndef ZX_QUERYPUNCH_COMPUTE_H
#define ZX_QUERYPUNCH_COMPUTE_H

namespace zx
{

// What one waiting browser slot should do at this moment. At most one field is set.
struct QueryPunchStep
{
	bool requestPunch;		// ask the registry to broker a punch for this server, now
	bool resendChallenge;	// send the launcher challenge again (the punch may have landed)
	bool timeOut;			// stop waiting; the slot ages out exactly as before

	QueryPunchStep() : requestPunch(false), resendChallenge(false), timeOut(false) {}
};

// The moments, exposed for the tests and the caller's comments rather than tunable knobs.
// A punch is asked for once the plain query has clearly missed (typical replies arrive in well
// under a second); the resends trail the punch schedule the server side already uses (5 packets
// over ~2s, punchbroker_compute.h); the extended timeout covers registry round trip + punch +
// final resend. An ineligible slot keeps the pre-punch 4000ms lifetime to the millisecond.
const int kQueryPunchAskMs = 1500;
const int kQueryPunchResendMs[3] = { 2500, 4000, 5500 };
const int kQueryPunchTimeoutMs = 7000;
const int kQueryPlainTimeoutMs = 4000;

// Decide for one slot. `elapsedMs` is time since the FIRST challenge went out (resends must not
// restamp it, or the ladder never advances). `punchEligible` is the caller's whole verdict --
// "this is a registry-listed internet server and the punch budget allows one more" -- so the
// schedule needs no opinion on LAN addresses or rate limits. `punchRequested` and `resendsSent`
// are per-slot state the caller keeps between calls.
QueryPunchStep StepQueryPunch(int elapsedMs, bool punchEligible, bool punchRequested,
	int resendsSent);

} // namespace zx

#endif // ZX_QUERYPUNCH_COMPUTE_H
