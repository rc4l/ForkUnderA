// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] "Is my server actually visible to other people on the internet?"
//
// Nothing on the hosting machine can answer that, and everything on it looks like it can:
//
//   - the browser's public row is FABRICATED from the LAN row by
//     browser_MirrorAnswerOntoOurOtherRows, so it lights up whether or not anyone outside could
//     reach you. It exists to stop the row flickering, and the cost is that it is not evidence.
//   - the ping on that row is a hairpin or a loopback. An 8ms round trip to your own house is not
//     the internet answering.
//   - the country flag reports which ADDRESS the row holds, not where the machine is or whether it
//     can be reached: NETWORK_GetCountryIndexFromAddress returns LAN for any private address before
//     GeoIP is ever consulted.
//
// A machine behind a NAT cannot test its own reachability, for the same reason you cannot check
// whether your phone is in airplane mode by calling yourself. The only witness is the registry,
// because it is the only party outside.
//
// So this unit turns what the REGISTRY said into one honest state. It never infers from local
// evidence, and it deliberately has no "probably fine" answer.
//
// VERIFICATION EXPIRES. That is the rule that makes the good state worth trusting: a check from ten
// minutes ago says the port was open ten minutes ago, and routers, leases and ISPs all change under
// you. A green light that never goes stale is how you end up confidently telling someone to join a
// server nobody can reach.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_LISTINGPROOF_COMPUTE_H
#define ZX_LISTINGPROOF_COMPUTE_H

namespace zx
{

enum class ListingState
{
	// We have not managed to send an announcement at all. The registry has never heard of us, so
	// there is nothing to be listed as.
	NeverAnnounced,

	// Announced, nothing back yet. NOT a failure: it is the normal first second or two, and calling
	// it one would make every startup look broken.
	AwaitingAnswer,

	// The registry answered, and the answer was no. The caller carries the reason; this only says
	// that the refusal was explicit rather than inferred from silence.
	Refused,

	// The registry holds us but has not confirmed it can reach us. This is the state that matters
	// most and reads the most like success: we are IN the list, and players still may not be able to
	// join, because being listed is a claim we made and being verified is a claim the registry made.
	ListedUnverified,

	// The registry reached us from outside, recently enough to still mean something. The only state
	// that justifies telling somebody the server works.
	ListedVerified,

	// Verified once, too long ago to claim now. Deliberately not folded into ListedVerified: the
	// difference between "it worked" and "it works" is the whole question being asked.
	ListedStale,
};

struct ListingProof
{
	ListingState state;

	// How long ago the registry confirmed it reached us, or -1 when it never has. Carried so the UI
	// can show the age rather than a bare light, because an age is checkable and a light is not.
	int secondsSinceVerified;

	ListingProof() : state(ListingState::NeverAnnounced), secondsSinceVerified(-1) {}
	ListingProof(ListingState s, int secs) : state(s), secondsSinceVerified(secs) {}
};

// [rc4l] `msSinceVerified` is ignored unless `verified` is true. Negative inputs are treated as
// "never", because a clock that went backwards is not evidence of anything.
ListingProof DecideListingProof(bool announceSent, bool answerReceived, bool listed,
	bool verified, int msSinceVerified, int staleAfterMs);

// One sentence, in the words a player would use. Never hedges: every state here is a definite thing
// that either happened or did not.
const char *DescribeListing(ListingState state);

// Whether this state should be shown as a problem rather than progress. AwaitingAnswer is not a
// problem; ListedUnverified is, however much it sounds like success.
bool ListingNeedsAttention(ListingState state);

} // namespace zx

#endif // ZX_LISTINGPROOF_COMPUTE_H
