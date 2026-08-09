// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/federated-server-registry/computation/reachcookie_compute.h"

namespace zx
{

CookieVerdict DecideIssueCookie(bool sameSourceHasOne, int fromSameIP, int total,
	int maxPerSource, int maxTotal)
{
	// A retry is free. Checked before either limit, because the same source asking again is the same
	// ask: counting it against their own share would let one dropped reply lock somebody out of the
	// very thing they are retrying.
	if (sameSourceHasOne)
		return CookieVerdict::Reissue;

	// The per-source limit first, so that when the table IS full the answer tells the truth about
	// whose fault it is. A hoarder should be refused for hoarding even at the moment the table fills.
	if ((maxPerSource >= 0) && (fromSameIP >= maxPerSource))
		return CookieVerdict::TooManyFromSource;

	if ((maxTotal >= 0) && (total >= maxTotal))
		return CookieVerdict::TableFull;

	return CookieVerdict::Issue;
}

} // namespace zx
