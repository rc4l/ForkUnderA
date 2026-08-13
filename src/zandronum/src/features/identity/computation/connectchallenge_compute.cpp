// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/identity/computation/connectchallenge_compute.h"

namespace zx
{

const size_t kNonceBytes = 16;
const size_t kKeyBytes = 32;
const size_t kSignatureBytes = 64;

ChallengeAction ChallengeActionForAttempt(bool hasStoredChallenge, bool nonceMatchesStored)
{
	if (!hasStoredChallenge)
		return CHALLENGE_MINT;

	return nonceMatchesStored ? CHALLENGE_REPLAY : CHALLENGE_MINT;
}

bool IsWellFormedHello(size_t nonceLength, size_t ephemeralLength)
{
	return (nonceLength == kNonceBytes) && (ephemeralLength == kKeyBytes);
}

ProofVerdict ProofReadiness(bool hasStoredChallenge, size_t accountKeyLength, size_t signatureLength)
{
	// [rc4l] Width first, because a short packet is the cheaper thing to notice and says nothing
	// about the slot.
	if ((accountKeyLength != kKeyBytes) || (signatureLength != kSignatureBytes))
		return PROOF_MALFORMED;

	if (!hasStoredChallenge)
		return PROOF_NOCHALLENGE;

	return PROOF_JUDGE;
}

} // namespace zx
