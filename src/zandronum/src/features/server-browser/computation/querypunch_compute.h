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

// [rc4l] How long to hold the FIRST challenge back while the punch goes ahead of it, for a row we
// already know needs one.
//
// THE QUERY POISONS THE HOLE. This is not a tuning detail, it is the reason the punch was failing
// outright, and it took packet counters in the NAT lab to see it. The joiner's first challenge lands
// on the host's router as an unsolicited packet, and the router tracks it even though it drops it --
// so the tuple (hostPublic:port <-> joiner:port) is now taken. When the host is then told to punch at
// that same joiner, its NAT cannot reuse that tuple and rewrites the source port, so the hole opens
// somewhere the joiner is not knocking. The joiner cannot follow it either: a knock from a port it
// never sent to is dropped by its OWN NAT before ShouldAdoptPunchKnock is ever consulted.
//
// So the punch has to go FIRST, and the challenge has to wait for it. 600ms covers a registry round
// trip and the first punch packet with room to spare, and it costs nothing on a server that would
// have answered anyway -- those are not punch-eligible in the first place.
const int kQueryPunchLeadMs = 600;

// Decide for one slot. `elapsedMs` is time since the FIRST challenge went out (resends must not
// restamp it, or the ladder never advances). `punchEligible` is the caller's whole verdict --
// "this is a registry-listed internet server and the punch budget allows one more" -- so the
// schedule needs no opinion on LAN addresses or rate limits. `punchRequested` and `resendsSent`
// are per-slot state the caller keeps between calls.
QueryPunchStep StepQueryPunch(int elapsedMs, bool punchEligible, bool punchRequested,
	int resendsSent);

// Whether a row that is about to be challenged for the FIRST time should punch before it speaks.
//
// `knownUnreachable` is the caller's memory that this exact address went unanswered on an earlier
// sweep. Only those lead with a punch: doing it for every listed server would spend the sweep's
// budget on servers that were going to answer anyway, and starve the ones that need it. A server
// that has never been tried gets the ordinary challenge first, which is right -- most of them
// answer, and one wasted round trip per unreachable server is cheaper than a punch for everyone.
bool ShouldPunchBeforeFirstChallenge(bool lan, bool knownUnreachable, bool punchBudgetLeft);

// Whether the held-back first challenge is now due. `punchLedMs` is time since the punch was asked
// for. Returns false when this row did not lead with a punch, since then the challenge already went.
bool FirstChallengeDue(bool punchLed, bool firstChallengeSent, int punchLedMs);

// Whether a punch knock -- an unsolicited packet from the server we asked the registry to punch --
// may re-aim a waiting browser slot at the knock's source. Under endpoint-dependent (carrier) NAT
// the server's packets leave from a DIFFERENT public port than the registry listed, so the knock's
// source is the only endpoint that actually works. The rule is also the security boundary: only a
// slot that is still waiting AND asked for a punch AND matches the knock's host may be re-aimed --
// an unsolicited packet must never redirect a row that did not invite it.
bool ShouldAdoptPunchKnock(bool slotWaiting, bool slotPunchRequested, bool sameHost);

// Whether a slot that already exists for a re-announced address should be re-armed for a fresh
// query. A slot that gave up earlier keeps its address, and the browser's add-path dedupe used to
// eat the registry's re-announcement of it -- one missed reply window and the server was gone for
// the whole session. Timed-out slots re-arm; so do inactive (removed) ones unless a re-check is
// still in flight on them; anything else -- answered, mid-query, nonsense reply -- is left alone.
bool ShouldRearmListedSlot(bool slotTimedOut, bool slotInactive, bool slotRefreshing);

} // namespace zx

#endif // ZX_QUERYPUNCH_COMPUTE_H
