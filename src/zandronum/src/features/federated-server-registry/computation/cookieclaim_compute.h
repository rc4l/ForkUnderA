// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether presenting a reach cookie destroys it, which a probe must and a punch must not.

#ifndef ZX_COOKIECLAIM_COMPUTE_H
#define ZX_COOKIECLAIM_COMPUTE_H

namespace zx
{

// [rc4l] What the caller should do with the cookie it just matched.
struct CookieClaim
{
	bool accepted;	// the presenter proved they are where they say
	bool consume;	// and the cookie must now be destroyed

	CookieClaim() : accepted(false), consume(false) {}
	CookieClaim(bool a, bool c) : accepted(a), consume(c) {}
};

// [rc4l] Why the cookie is being claimed, since the two answers differ.
enum class CookiePurpose
{
	ReachProbe,	// consumes, so a replay cannot make the registry fire packets at an address
	Punch,		// does not, since one sweep asks about several servers with the one cookie it was given
};

// [rc4l] `found` is whether a cookie issued to THIS address matches what was presented.
CookieClaim DecideCookieClaim(bool found, CookiePurpose purpose);

} // namespace zx

#endif // ZX_COOKIECLAIM_COMPUTE_H
