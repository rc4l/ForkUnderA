// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Deciding whether a host string needs the resolver at all before handing it to one.
//
// Asking getaddrinfo for the AAAA of "203.0.113.10" looks harmless and is not. The string will not
// parse as an IPv6 literal, so the resolver stops treating it as an address and starts treating it
// as a NAME -- it appends the search domains and chases the lot to NXDOMAIN, which costs seconds on
// whichever thread asked. The browser asks once per registry on every refresh, from the main thread,
// so a v4-only setup spent those seconds stalled and the server list timed out empty.
//
// A literal answers for itself. Only a real name is worth a lookup.
//
// Header-pure by the features/ rules: no engine types, and no inet_pton either, since that lives in
// a different header on Windows than it does everywhere else.

#ifndef ZX_HOSTLITERAL_COMPUTE_H
#define ZX_HOSTLITERAL_COMPUTE_H

#include <cstddef>

namespace zx
{

enum class HostLiteral
{
	V4,		// a dotted quad, so there is no AAAA to look up and nothing to ask
	V6,		// already the answer
	Name,	// worth a lookup
};

// Classify `host`, writing the address part into `out` with any brackets and :port removed.
//
// Returns Name for an empty or oversized input, which is the safe reading: a lookup that finds
// nothing costs a caller far less than a truncated address that resolves to the wrong machine.
HostLiteral ClassifyHost( const char *host, char *out, size_t outSize );

} // namespace zx

#endif // ZX_HOSTLITERAL_COMPUTE_H
