// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Who may hold a reach cookie, and how many.
//
// A cookie is how the registry learns that somebody really receives mail where they claim to. It is
// sent to the address they SAY they are and it has to come back, so naming a victim's address only
// succeeds in sending that victim a cookie they will never echo. Everything that dials an address on
// a stranger's say-so is gated behind claiming one.
//
// The table therefore has to be bounded, and a bound is itself an attack unless it is shared fairly.
// The first version deduplicated "one in flight per address" using the full address INCLUDING the
// port, which is not one per machine at all: a source port costs nothing to change, so a single
// client could hold every slot and the cap became a denial of service against everyone else rather
// than a defence. Exactly what the bound was there to prevent, implemented backwards.
//
// So there are two limits and they answer different questions. The per-source limit keeps one
// machine from crowding out the rest. The table limit keeps the whole thing bounded no matter how
// many machines there are. Counting by IP and ignoring the port is the point: an attacker controls
// their port and cannot easily change their address, so the limit has to be keyed on the part they
// cannot pick.
//
// A few per source rather than one, because a machine can legitimately run more than one server
// behind the same router and they should not have to queue for a ten-second cookie.
//
// Header-pure by the features/ rules: no engine types, no sockets.

#ifndef ZX_REACHCOOKIE_COMPUTE_H
#define ZX_REACHCOOKIE_COMPUTE_H

namespace zx
{

// A few per household, so two servers behind one router both work and a thousand cannot.
const int kMaxCookiesPerSource = 4;

enum class CookieVerdict
{
	Issue,				// a fresh one
	Reissue,			// this exact source already has one in flight; hand back the same cookie
	TooManyFromSource,	// this IP is using more than its share
	TableFull,			// nobody gets one right now
};

// `sameSourceHasOne` means an entry exists for this address AND port, which is a retry rather than a
// new ask. `fromSameIP` counts entries from this address ignoring the port. `total` is the whole
// table.
//
// Checked in the order that makes a retry free: somebody re-asking must not be counted against their
// own limit, or a dropped reply would lock them out of the thing they are retrying.
CookieVerdict DecideIssueCookie(bool sameSourceHasOne, int fromSameIP, int total,
	int maxPerSource, int maxTotal);

} // namespace zx

#endif // ZX_REACHCOOKIE_COMPUTE_H
