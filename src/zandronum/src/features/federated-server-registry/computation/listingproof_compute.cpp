// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "listingproof_compute.h"

namespace zx
{

ListingProof DecideListingProof(bool announceSent, bool answerReceived, bool listed,
	bool verified, int msSinceVerified, int staleAfterMs)
{
	if (!announceSent)
		return ListingProof(ListingState::NeverAnnounced, -1);

	if (!answerReceived)
		return ListingProof(ListingState::AwaitingAnswer, -1);

	if (!listed)
		return ListingProof(ListingState::Refused, -1);

	if (!verified)
		return ListingProof(ListingState::ListedUnverified, -1);

	// A negative age is a clock that moved backwards, which is not evidence. Treated as unverified
	// rather than as a very recent check, because the failure of a clock must not read as success.
	if (msSinceVerified < 0)
		return ListingProof(ListingState::ListedUnverified, -1);

	const int seconds = msSinceVerified / 1000;

	// A non-positive window means "never goes stale", which the caller may want for a fixed-address
	// host where re-checking is pointless. Anything else expires.
	if ((staleAfterMs > 0) && (msSinceVerified >= staleAfterMs))
		return ListingProof(ListingState::ListedStale, seconds);

	return ListingProof(ListingState::ListedVerified, seconds);
}

const char *DescribeListing(ListingState state)
{
	switch (state)
	{
	case ListingState::NeverAnnounced:
		return "Not announced to any server registry yet.";
	case ListingState::AwaitingAnswer:
		return "Announced. Waiting for the registry to answer.";
	case ListingState::Refused:
		return "The registry refused to list this server.";
	case ListingState::ListedUnverified:
		// Says the consequence, not the state. "Unverified" invites being read as a formality.
		return "Listed, but the registry has not reached this server. Players may not be able to join.";
	case ListingState::ListedVerified:
		return "Listed, and the registry reached this server from outside your network.";
	case ListingState::ListedStale:
		return "Listed. The last successful check is old enough that it no longer proves anything.";
	}
	return "Unknown listing state.";
}

bool ListingNeedsAttention(ListingState state)
{
	switch (state)
	{
	case ListingState::NeverAnnounced:
	case ListingState::Refused:
	case ListingState::ListedUnverified:
	case ListingState::ListedStale:
		return true;
	case ListingState::AwaitingAnswer:
	case ListingState::ListedVerified:
		return false;
	}
	return true;
}

} // namespace zx
