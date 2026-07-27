// [MGOOOOOO] Tests for the pure A_JumpIfInput decision logic: button matching (any/all, edge,
// invert, combinations, empty-mask guard) and the full-button transmission predicate. Covers
// every branch.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#include "gtest/gtest.h"
#include "features/jumpifinput/computation/jumpifinput_compute.h"

namespace
{
// [MGOOOOOO] Mirror of the BT_* bits used in these tests (see d_event.h / constants.txt).
enum
{
	BT_ATTACK    = 1,
	BT_JUMP      = 4,
	BT_ALTATTACK = 32,
	BT_USER1     = 1 << 21,
};

// ---- ComputeInputMatch: empty mask guard ----

// [MGOOOOOO] A zero mask never matches, even under JIF_ALL (0 == 0) or JIF_NOT (guard precedes inversion).
TEST(ComputeInputMatch, EmptyMaskNeverMatches)
{
	EXPECT_FALSE(ComputeInputMatch(0xFFFFFFFFu, 0, 0, 0));
	EXPECT_FALSE(ComputeInputMatch(0xFFFFFFFFu, 0, 0, JIF_ALL));
	EXPECT_FALSE(ComputeInputMatch(0, 0, 0, JIF_NOT));
}

// ---- ComputeInputMatch: default (any, held) ----

// [MGOOOOOO] Default mode matches when any listed button is currently held.
TEST(ComputeInputMatch, AnyHeldMatches)
{
	EXPECT_TRUE(ComputeInputMatch(BT_ALTATTACK, 0, BT_ALTATTACK, 0));
	EXPECT_TRUE(ComputeInputMatch(BT_ATTACK, 0, BT_ATTACK | BT_ALTATTACK, 0)); // one of two
}

// [MGOOOOOO] Default mode does not match when none of the listed buttons are held.
TEST(ComputeInputMatch, AnyNotHeldDoesNotMatch)
{
	EXPECT_FALSE(ComputeInputMatch(BT_JUMP, 0, BT_ATTACK | BT_ALTATTACK, 0));
}

// ---- ComputeInputMatch: JIF_ALL ----

// [MGOOOOOO] JIF_ALL requires every listed button to be held.
TEST(ComputeInputMatch, AllRequiresEveryButton)
{
	EXPECT_TRUE(ComputeInputMatch(BT_ATTACK | BT_ALTATTACK, 0, BT_ATTACK | BT_ALTATTACK, JIF_ALL));
	EXPECT_FALSE(ComputeInputMatch(BT_ATTACK, 0, BT_ATTACK | BT_ALTATTACK, JIF_ALL)); // only one held
}

// ---- ComputeInputMatch: JIF_EDGE (any mode) ----

// [MGOOOOOO] Edge+any fires on the tic a listed button is newly pressed, not while held.
TEST(ComputeInputMatch, EdgeAnyRisingOnly)
{
	EXPECT_TRUE(ComputeInputMatch(BT_ALTATTACK, 0, BT_ALTATTACK, JIF_EDGE));          // just pressed
	EXPECT_FALSE(ComputeInputMatch(BT_ALTATTACK, BT_ALTATTACK, BT_ALTATTACK, JIF_EDGE)); // held since last tic
}

// [MGOOOOOO] Edge+any with nothing held stays false (exercises the match==false path into the edge block).
TEST(ComputeInputMatch, EdgeAnyNothingHeld)
{
	EXPECT_FALSE(ComputeInputMatch(0, 0, BT_ALTATTACK, JIF_EDGE));
}

// ---- ComputeInputMatch: JIF_EDGE + JIF_ALL ----

// [MGOOOOOO] Edge+all fires the tic the combo becomes complete, not once it's already complete.
TEST(ComputeInputMatch, EdgeAllCompletionOnly)
{
	// Was only attack last tic, now both -> combo completed this tic.
	EXPECT_TRUE(ComputeInputMatch(BT_ATTACK | BT_ALTATTACK, BT_ATTACK, BT_ATTACK | BT_ALTATTACK,
		JIF_EDGE | JIF_ALL));
	// Both held last tic and this tic -> already complete, no edge.
	EXPECT_FALSE(ComputeInputMatch(BT_ATTACK | BT_ALTATTACK, BT_ATTACK | BT_ALTATTACK,
		BT_ATTACK | BT_ALTATTACK, JIF_EDGE | JIF_ALL));
}

// [MGOOOOOO] Edge+all with the combo incomplete this tic stays false (match==false into the edge block).
TEST(ComputeInputMatch, EdgeAllIncomplete)
{
	EXPECT_FALSE(ComputeInputMatch(BT_ATTACK, BT_ATTACK, BT_ATTACK | BT_ALTATTACK,
		JIF_EDGE | JIF_ALL));
}

// ---- ComputeInputMatch: JIF_NOT ----

// [MGOOOOOO] JIF_NOT inverts: matching -> false, non-matching -> true.
TEST(ComputeInputMatch, NotInverts)
{
	EXPECT_FALSE(ComputeInputMatch(BT_JUMP, 0, BT_JUMP, JIF_NOT));      // held -> inverted to false
	EXPECT_TRUE(ComputeInputMatch(0, 0, BT_JUMP, JIF_NOT));             // not held -> inverted to true
}

// ---- ComputeShouldSendFullButtons ----

// [MGOOOOOO] forceFull always sends the full set, even with no buttons pressed.
TEST(ComputeShouldSendFullButtons, ForcedAlwaysTrue)
{
	EXPECT_TRUE(ComputeShouldSendFullButtons(0, true));
}

// [MGOOOOOO] A script/user button above the low byte requires the full transmission.
TEST(ComputeShouldSendFullButtons, HighBitNeedsFull)
{
	EXPECT_TRUE(ComputeShouldSendFullButtons(BT_USER1, false));
}

// [MGOOOOOO] Only low gameplay bits (0-7) fit in the byte and don't need the full set.
TEST(ComputeShouldSendFullButtons, LowBitsOnly)
{
	EXPECT_FALSE(ComputeShouldSendFullButtons(0xFFu, false));
	EXPECT_FALSE(ComputeShouldSendFullButtons(0, false));
}
} // namespace
