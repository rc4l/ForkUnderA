// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether to broker a hole punch, and how hard to try.
//
// THE PROBLEM. A server behind an ordinary home router is visible but not reachable. Announcing to
// the registry works, because that packet goes OUT and no router blocks outbound, so the server is
// listed and everyone can see it. A joiner then sends a packet to that address and the host's router
// discards it, because the router never saw the host talk to this stranger and assumes the traffic is
// unwanted. That is the whole of the port forwarding problem.
//
// THE TRICK. Routers do accept replies to traffic that already left. So if the HOST sends one packet
// to the joiner first, the joiner's packets stop looking unsolicited and get through. The host cannot
// do that alone because it has no idea the joiner exists, and the registry does: both of them have
// talked to it. So the registry introduces them and then gets out of the way. It never carries game
// traffic, and the hole is in the host's router, opened by the host, by sending.
//
// THE ONE SECURITY RULE, and everything here exists to enforce it:
//
//     NEVER DIAL AN ADDRESS THAT CAME OUT OF A PAYLOAD.
//     Only ever the observed source address of somebody who completed a cookie round trip.
//
// Break that and the registry becomes a machine for firing unsolicited UDP at any address a stranger
// names, which is a reflection weapon. The cookie is what makes an address trustworthy: it is sent to
// where the requester CLAIMS to be and must come back, so an attacker naming a victim's address only
// succeeds in sending the victim a cookie they will never echo. That gate already exists for the
// reach probe (SERVERREGISTRY_IssueReachCookie); this reuses it rather than inventing a second one.
//
// SAYING NO IS A FEATURE. Most requests should be refused instantly rather than timing out: every
// Zandronum server that ever shipped, and every build older than this, cannot punch and never will.
// A fast refusal lets the client fall through to an ordinary connection immediately, which is why
// PunchVerdict carries a reason rather than a bool.
//
// NOTHING HERE IS LOAD-BEARING. A punch is an attempt made ALONGSIDE the ordinary connection, never
// instead of it. Registry down, registry slow, server too old, punch refused, punch simply failed:
// every one of those ends at the same place, which is the direct connection that is all we have
// today. That is what makes this safe to ship.
//
// Header-pure by the features/ rules: no engine types, no sockets.

#ifndef ZX_PUNCHBROKER_COMPUTE_H
#define ZX_PUNCHBROKER_COMPUTE_H

namespace zx
{

// Why the registry did or did not broker. Every refusal is meant to be answered immediately, so the
// client stops waiting and connects the ordinary way.
enum class PunchVerdict
{
	Broker,			// introduce them

	// The common refusal by far, and the reason the whole ecosystem keeps working: this server never
	// said it could punch, so there is nobody to instruct.
	NoSupport,

	NotListed,		// not a server we have verified; we will not dial an address on a stranger's say-so
	RateLimited,
	BadCookie,		// the requester never proved it receives mail where it claims to
};

// What the registry knows when a client asks for an introduction. There is deliberately no address
// here: the only address this decision may ever lead to is the observed source of the request, which
// the caller holds and this unit must not be able to override.
struct PunchAsk
{
	bool serverListed;			// a verified entry, not something the client named
	bool serverSupportsPunch;	// the server said so when it announced
	bool cookieProven;			// the requester echoed the cookie back
	int recentRequests;			// from this source, inside the rate window

	PunchAsk() : serverListed(false), serverSupportsPunch(false), cookieProven(false),
		recentRequests(0) {}
	PunchAsk(bool listed, bool supports, bool proven, int recent)
		: serverListed(listed), serverSupportsPunch(supports), cookieProven(proven),
		recentRequests(recent) {}
};

// Checked cheapest and most-common first, so the answer that covers every existing server costs
// nothing to reach.
PunchVerdict DecidePunch(const PunchAsk &ask, int maxRequests);

// Whether a joining client should ask at all.
enum class PunchIntent
{
	Skip,	// nothing to broker, or nobody to broker with
	Ask,
};

// `fromList` is false when the player typed or pasted an address: we have no registry entry for it,
// and asking about a server the registry has never heard of can only end in NotListed.
//
// `lan` skips for a different reason. A machine on the same network is reached without crossing a
// router at all, so there is no hole to open, and involving a server on the internet to introduce two
// machines in the same house would be absurd.
PunchIntent DecidePunchIntent(bool fromList, bool lan, bool registryAnswering);

// [rc4l] How many punches to send, and when.
//
// One packet is not enough, and this is the mistake the naive version makes. The two sides act on
// separate messages from the registry and do not start together, so the first punch can easily leave
// before the other end is listening, and UDP is free to drop it besides. ICE (RFC 8445) answers this
// by repeating connectivity checks rather than trusting one, and this is the same idea at the
// smallest scale that works.
//
// Backing off rather than repeating flat: a punch that is going to succeed almost always does so on
// the first or second try, and the later attempts exist for the slow cases. Spacing them keeps the
// cost near zero for everyone else.
const int kPunchAttempts = 5;

// Delay before attempt `n` (0-based), in milliseconds. 0, 100, 300, 700, 1500.
int PunchAttemptDelayMs(int attempt);

// [rc4l] How often to refresh the mapping once it is open, in milliseconds.
//
// NAT mappings expire, commonly in 30 seconds and sometimes sooner, so a hole nobody uses closes
// again. Ordinary game traffic keeps it alive by itself; this is for the gap between opening it and
// the connection actually starting to flow. 15 seconds is the ICE default and is chosen to sit
// comfortably inside the shortest timeouts seen in the wild.
const int kPunchKeepaliveMs = 15000;

// [rc4l] How long to hold a hole open for a joiner who never turns up, in milliseconds.
//
// It has to outlast a slow join and it has to END. A punch nobody used is a mapping we are paying to
// keep and a trickle of packets at somebody who is not listening, so an entry that cannot expire is
// a leak with a timer on it. Everything that works is finished long before this.
const int kPunchLifetimeMs = 45000;

// [rc4l] What a pending punch should do at this moment.
//
// The burst and the keepalive are one job at two speeds, open the hole and then stop it closing, so
// one function owns the whole life of a punch and the caller only ever asks "now what?". Splitting
// them would mean two places that each know part of the schedule, which is how the two drift apart.
enum class PunchStep
{
	Wait,	// too early to send again
	Send,
	Done,	// stop tracking this one
};

// `attemptsSent` counts packets already sent, `msSinceFirst` runs from the request arriving, and
// `msSinceLastSend` from the most recent packet. `connected` ends it early: the hole did its job and
// ordinary game traffic holds the mapping open from there.
PunchStep NextPunchStep(int attemptsSent, int msSinceFirst, int msSinceLastSend, bool connected);

} // namespace zx

#endif // ZX_PUNCHBROKER_COMPUTE_H
