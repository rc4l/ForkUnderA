// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Asking the outside world whether it can reach a port, WITHOUT hosting anything on it yet.
//
// The browser already learns this for a running server: the registry contacts it unprompted, and an
// inbound packet nobody asked for is the only proof a port is forwarded that a machine cannot fake
// about itself. The gap was that you had to start hosting to find out -- pick INTERNET, wait, and
// then discover nobody can see you.
//
// Nothing about that test needs a game server. It needs SOMETHING bound to the port. So the client
// binds it for a couple of seconds, has the registry probe it, and releases it again.
//
// THE THREE RULES BELOW ARE SECURITY, NOT TIDINESS. A service that sends a packet to whoever asks is
// a weapon pointed at whoever is named in the request:
//
//   1. The registry may only ever probe the SOURCE address of the request. An address carried in the
//      packet body would make this a reflector -- name a victim, and the registry attacks them.
//
//   2. UDP sources are forgeable, so "the source address" is not enough on its own. The requester
//      must first echo a cookie the registry sent to that address. That reply travels back through
//      the NAT hole the request just opened, so it needs no forwarding -- which is exactly why it
//      can prove the address is real without pre-supposing the answer we are trying to measure.
//
//   3. The probe carries a nonce the CLIENT generated, and only a packet bearing it counts. Without
//      that, any stranger's stray packet arriving on the temporary socket reads as success, and the
//      feature confidently tells people they are reachable when they are not.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_REACHPROBE_COMPUTE_H
#define ZX_REACHPROBE_COMPUTE_H

#include <string>

namespace zx
{

// Where a probe attempt has got to.
enum class ProbePhase
{
	Idle,			// nothing asked for
	AwaitingCookie,	// request sent; waiting for the registry to reply to our source address
	AwaitingProbe,	// cookie echoed; waiting for the unsolicited packet on the bound port
	Reachable,		// it arrived, carrying our nonce
	Unreachable,	// it did not, within the deadline
	Failed,			// the registry never answered at all -- a different thing from "not forwarded"
};

// [rc4l] Unreachable and Failed are deliberately separate. "Your port is closed" is about the
// player's router and is worth acting on; "the registry did not answer" is about us being down and
// is not their problem to fix. Collapsing them would have the browser blame a player's network for
// our outage.
bool ProbeIsFinished(ProbePhase phase);

// Whether a finished probe means the player may host to the internet.
bool ProbeSaysReachable(ProbePhase phase);

// How the INTERNET option should read, which is not the same question as the phase.
//
// Three answers, not six, because the player only cares about three: it works, it does not, or we
// do not know. Failed collapses into Unknown deliberately -- a registry that never answered has told
// us something about ITSELF, and colouring the option as though the player's router were shut would
// be blaming them for our outage.
enum class ProbeDisplay
{
	Unknown,		// untested, still running, or the check itself could not be completed
	Reachable,		// the probe arrived: this port is genuinely open
	Unreachable,	// the check ran and nothing arrived
};

ProbeDisplay ProbeDisplayFor(ProbePhase phase);

// How long to wait for each leg, in milliseconds.
//
// The cookie leg is short: it travels the path we KNOW works, so a slow answer means the registry is
// struggling rather than that the player's router is thinking about it. The probe leg is longer,
// because a forwarded packet crosses the open internet and a home router under load can sit on it.
extern const int kCookieTimeoutMs;
extern const int kProbeTimeoutMs;

// The next phase, given what has happened. `elapsedMs` is the time in the CURRENT phase.
ProbePhase StepProbe(ProbePhase phase, bool cookieArrived, bool probeArrived, int elapsedMs);

// [rc4l] Whether a received probe packet is ours. The nonce is the whole check -- an unsolicited
// packet on that port could come from anyone, and "something arrived" is not the question. Empty
// nonces never match, so a client that failed to generate one cannot accidentally accept everything.
bool ProbeNonceMatches(const std::string &expected, const std::string &received);

// What a cached verdict is about. A forward is per-port and per-network, so all three parts belong
// in the key: 10666 being open says nothing about 10667, and neither says anything once the player
// is on a different network or their ISP has moved them.
struct ProbeCacheKey
{
	std::string publicIp;	// as the registry observed it
	std::string localSubnet;	// which network we were on
	int port;

	ProbeCacheKey() : port(0) {}
};

bool ProbeCacheKeyMatches(const ProbeCacheKey &a, const ProbeCacheKey &b);

// Whether a cached answer may still be shown. Age is milliseconds since it was recorded.
//
// Cached answers are shown IMMEDIATELY and re-checked underneath, the same bargain the server list
// makes: what you knew a moment ago is nearly always still true, and hiding it while re-deriving it
// buys accuracy nobody asked for at the price of a wait everybody notices.
extern const int kProbeCacheTtlMs;

// [rc4l] Failed keeps its own, much shorter life. It is not an answer, it is the absence of one, and
// holding it for the full TTL made a single lost request paint the INTERNET option white for ten
// minutes. The registry drops every command from an address in its short flood queue, and the
// browser's own refresh puts us there for three seconds, so losing that first request is routine.
extern const int kFailedCacheTtlMs;

// The phase is part of the question because Failed expires on the shorter clock above.
bool ProbeCacheUsable(const ProbeCacheKey &cached, const ProbeCacheKey &now, int ageMs,
	ProbePhase cachedPhase);

} // namespace zx

#endif // ZX_REACHPROBE_COMPUTE_H
