// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] What happens to a reach cookie when somebody presents it.
//
// A cookie proves one thing: whoever holds it receives mail at the address it was sent to. Two
// features ask for that proof and they need OPPOSITE lifetimes, which is the whole reason this is a
// decision worth writing down rather than an `erase` in a loop.
//
//   * A reach probe consumes it. The probe makes the registry send a packet at an address, so one
//     cookie must buy exactly one probe or a replay turns the registry into a packet cannon.
//
//   * A punch must NOT consume it. A launcher refreshing its list asks about several servers at
//     once, and the issuer deliberately hands a repeat asker the SAME cookie -- so consuming it made
//     the first punch of a sweep work and every one after it fail as "the cookie was missing or
//     wrong". That shipped, and showed up as a stream of refusals from ordinary clients.
//
// Not consuming is safe for the punch because the fact being proved does not decay with use: the
// address either receives mail or it does not. What bounds abuse is the rate limit and expiry, not
// the number of times a proof may be shown.
//
// Header-pure by the features/ rules: no engine types, no sockets, no clock.

#ifndef ZX_COOKIECLAIM_COMPUTE_H
#define ZX_COOKIECLAIM_COMPUTE_H

namespace zx
{

// What the caller should do with the cookie it just matched.
struct CookieClaim
{
	bool accepted;	// the presenter proved they are where they say
	bool consume;	// and the cookie must now be destroyed

	CookieClaim() : accepted(false), consume(false) {}
	CookieClaim(bool a, bool c) : accepted(a), consume(c) {}
};

// Why the cookie is being claimed. The two answers differ, so the caller must say which it is rather
// than inherit whichever default was written first.
enum class CookiePurpose
{
	ReachProbe,	// consumes: one cookie, one probe
	Punch,		// does not: one sweep asks about several servers with the one cookie it was given
};

// `found` is whether a cookie issued to THIS address matches what was presented. An empty or
// unmatched cookie is not a near miss, it is a no.
CookieClaim DecideCookieClaim(bool found, CookiePurpose purpose);

} // namespace zx

#endif // ZX_COOKIECLAIM_COMPUTE_H
