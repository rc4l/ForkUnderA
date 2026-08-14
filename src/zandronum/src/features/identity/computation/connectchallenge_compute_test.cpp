// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The cases here are the lossy-link ones, because a clean join exercises none of them.

#include <gtest/gtest.h>

#include "features/identity/computation/connectchallenge_compute.h"

using namespace zx;

// ---------------------------------------------------------------- minting versus replaying

TEST(ConnectChallenge, AFirstAttemptMints)
{
	EXPECT_EQ(CHALLENGE_MINT, ChallengeActionForAttempt(false, false));
}

TEST(ConnectChallenge, ARetryFromTheSameClientReplays)
{
	// The whole point of the unit, since a client whose challenge was dropped asks again and must
	// be given back the same bytes it would have signed the first time.
	EXPECT_EQ(CHALLENGE_REPLAY, ChallengeActionForAttempt(true, true));
}

TEST(ConnectChallenge, ARebindingPortIsStillTheSameClient)
{
	// Keyed on the nonce rather than the address for this case: a NAT that moves the port mid
	// connect carries the same nonce, so it replays instead of minting a second challenge.
	EXPECT_EQ(CHALLENGE_REPLAY, ChallengeActionForAttempt(true, true));
}

TEST(ConnectChallenge, ReplayingIsStableHoweverManyTimesItIsAsked)
{
	for (int i = 0; i < 5; ++i)
		EXPECT_EQ(CHALLENGE_REPLAY, ChallengeActionForAttempt(true, true));
}

TEST(ConnectChallenge, ANewOccupantOfTheSlotMintsAgain)
{
	// A freed slot handed to somebody else arrives with its own nonce, and must not be answered
	// with the previous player's challenge.
	EXPECT_EQ(CHALLENGE_MINT, ChallengeActionForAttempt(true, false));
}

TEST(ConnectChallenge, NoStoredChallengeMintsWhateverTheNonceSays)
{
	EXPECT_EQ(CHALLENGE_MINT, ChallengeActionForAttempt(false, true));
	EXPECT_EQ(CHALLENGE_MINT, ChallengeActionForAttempt(false, false));
}

// ---------------------------------------------------------------- what came off the wire

TEST(ConnectChallenge, AFullHelloIsWellFormed)
{
	EXPECT_TRUE(IsWellFormedHello(kNonceBytes, kKeyBytes));
}

TEST(ConnectChallenge, ATruncatedHelloIsRefused)
{
	EXPECT_FALSE(IsWellFormedHello(kNonceBytes - 1, kKeyBytes));
	EXPECT_FALSE(IsWellFormedHello(kNonceBytes, kKeyBytes - 1));
	EXPECT_FALSE(IsWellFormedHello(0, 0));
}

TEST(ConnectChallenge, AnOverLongHelloIsRefusedToo)
{
	// Not merely truncation. A field wider than it should be means the reader and the writer
	// disagree about the layout, and nothing after it can be trusted either.
	EXPECT_FALSE(IsWellFormedHello(kNonceBytes + 1, kKeyBytes));
	EXPECT_FALSE(IsWellFormedHello(kNonceBytes, kKeyBytes + 1));
}

TEST(ConnectChallenge, AFullProofAgainstAnIssuedChallengeIsJudged)
{
	EXPECT_EQ(PROOF_JUDGE, ProofReadiness(true, kKeyBytes, kSignatureBytes));
}

TEST(ConnectChallenge, AProofWithNothingIssuedHasNoMessageToBeAbout)
{
	EXPECT_EQ(PROOF_NOCHALLENGE, ProofReadiness(false, kKeyBytes, kSignatureBytes));
}

TEST(ConnectChallenge, AMalformedProofIsCalledThatRatherThanAWrongKey)
{
	// Told apart because they mean different things to whoever is reading the log: one is a bad
	// link or a tampered packet, the other is an identity that did not check out.
	EXPECT_EQ(PROOF_MALFORMED, ProofReadiness(true, kKeyBytes - 1, kSignatureBytes));
	EXPECT_EQ(PROOF_MALFORMED, ProofReadiness(true, kKeyBytes, kSignatureBytes - 1));
	EXPECT_EQ(PROOF_MALFORMED, ProofReadiness(true, 0, 0));
}

TEST(ConnectChallenge, WidthIsCheckedBeforeTheSlotIs)
{
	// A short packet is refused as malformed even when no challenge was issued, so the reason
	// reported is the one the sender can act on.
	EXPECT_EQ(PROOF_MALFORMED, ProofReadiness(false, 0, 0));
	EXPECT_EQ(PROOF_MALFORMED, ProofReadiness(false, kKeyBytes + 1, kSignatureBytes));
}

TEST(ConnectChallenge, TheWidthsAreTheOnesEd25519Uses)
{
	EXPECT_EQ(static_cast<size_t>(16), kNonceBytes);
	EXPECT_EQ(static_cast<size_t>(32), kKeyBytes);
	EXPECT_EQ(static_cast<size_t>(64), kSignatureBytes);
}
