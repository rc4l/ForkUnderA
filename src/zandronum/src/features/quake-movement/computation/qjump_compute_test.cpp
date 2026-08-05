// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// Tests for the second-jump state machine. These pin the behaviours a player would actually notice
// going wrong -- "holding jump gives me two jumps", "landing refunds a jump early", "tapping
// forward-then-back counts as a double tap" -- because every one of those is a one-character change
// away and none of them shows up in a compiler.

#include "features/quake-movement/computation/qjump_compute.h"

#include <gtest/gtest.h>

using namespace zx::quakemove;

namespace {

const int TICRATE = 35;

// Mirrors of the engine's move-button bits; any distinct values work here.
const int BT_FWD = 0x1;
const int BT_BACK = 0x2;
const int BT_LEFT = 0x4;

JumpFlags NoFlags()
{
	JumpFlags f = { false, false, false, false, false, false, false };
	return f;
}

} // namespace

// ------------------------------------------------------- grounded state

TEST(ComputeGroundedState, LandingRefillsTheJumpAllowance) {
	const GroundedJumpState s = ComputeGroundedState(2, NoFlags(), 0, 0, 0);
	EXPECT_EQ(2, s.secondJumpsRemaining);
}

TEST(ComputeGroundedState, WithoutGroundSecondJumpLandingLeavesItUnarmed) {
	// Default behaviour: the second jump only arms in mid-air, so standing still does not let you
	// spend it from the floor.
	const GroundedJumpState s = ComputeGroundedState(1, NoFlags(), 0, 0, 0);
	EXPECT_EQ(SJ_NOT_AVAILABLE, s.state);
}

TEST(ComputeGroundedState, GroundSecondJumpArmsOnLandingButRespectsItsCooldown) {
	JumpFlags f = NoFlags();
	f.groundSecondJump = true;

	EXPECT_EQ(SJ_AVAILABLE, ComputeGroundedState(1, f, /*secondJumpTics*/ 0, 0, 0).state);
	// Still cooling down -- landing must not refund the jump early.
	EXPECT_EQ(SJ_NOT_AVAILABLE, ComputeGroundedState(1, f, /*secondJumpTics*/ 5, 0, 0).state);
}

TEST(ComputeGroundedState, JumpDelayReArmsOnTheSentinelOrOnARealLanding) {
	// -1 is the "no delay" sentinel left by the previous jump.
	EXPECT_TRUE(ComputeGroundedState(1, NoFlags(), 0, /*jumpTics*/ -1, /*velz*/ 0).resetJumpTics);
	// Arrived with real downward speed: actually landed.
	EXPECT_TRUE(ComputeGroundedState(1, NoFlags(), 0, /*jumpTics*/ 0, /*velz*/ -20).resetJumpTics);
	// Merely resting on the floor: nothing to re-arm.
	EXPECT_FALSE(ComputeGroundedState(1, NoFlags(), 0, /*jumpTics*/ 0, /*velz*/ 0).resetJumpTics);
	// A gentle touchdown is below the threshold.
	EXPECT_FALSE(ComputeGroundedState(1, NoFlags(), 0, /*jumpTics*/ 0, /*velz*/ -4).resetJumpTics);
}

// ------------------------------------------------------ airborne arming

TEST(ComputeAirborneArming, RequiresTheJumpButtonToHaveBeenReleased) {
	// THE bug this guards: without the release check, one long press spends both jumps on
	// consecutive tics and the double jump feels like it "just doesn't work".
	EXPECT_FALSE(ComputeAirborneArming(1, 0, NoFlags(), /*jumpHeld*/ true));
	EXPECT_TRUE(ComputeAirborneArming(1, 0, NoFlags(), /*jumpHeld*/ false));
}

TEST(ComputeAirborneArming, ADedicatedTriggerIgnoresTheJumpButtonEntirely) {
	// With double-tap or user4 as the trigger, holding jump is irrelevant -- so a player rising
	// with jump still held can still arm.
	JumpFlags tap = NoFlags();
	tap.doubleTapJump = true;
	EXPECT_TRUE(ComputeAirborneArming(1, 0, tap, /*jumpHeld*/ true));

	JumpFlags user4 = NoFlags();
	user4.user4Jump = true;
	EXPECT_TRUE(ComputeAirborneArming(1, 0, user4, /*jumpHeld*/ true));
}

TEST(ComputeAirborneArming, ZeroRemainingMeansSpentButNegativeMeansUnlimited) {
	// The distinction is load-bearing: SecondJumpAmount -1 is how a mod grants infinite jumps, and
	// a "> 0" test here would silently break it.
	EXPECT_FALSE(ComputeAirborneArming(0, 0, NoFlags(), false));
	EXPECT_TRUE(ComputeAirborneArming(-1, 0, NoFlags(), false));
	EXPECT_TRUE(ComputeAirborneArming(3, 0, NoFlags(), false));
}

TEST(ComputeAirborneArming, RespectsTheSecondJumpCooldown) {
	EXPECT_FALSE(ComputeAirborneArming(1, /*secondJumpTics*/ 4, NoFlags(), false));
	EXPECT_TRUE(ComputeAirborneArming(1, /*secondJumpTics*/ 0, NoFlags(), false));
	// A negative value is the double-tap window, not a cooldown, so it must not block arming.
	EXPECT_TRUE(ComputeAirborneArming(1, /*secondJumpTics*/ -7, NoFlags(), false));
}

// ------------------------------------------------------------- triggers

TEST(ComputeSecondJumpTriggered, PlainJumpButtonOnAFreshPress) {
	EXPECT_TRUE(ComputeSecondJumpTriggered(NoFlags(), false, false, /*jumpJustPressed*/ true));
	EXPECT_FALSE(ComputeSecondJumpTriggered(NoFlags(), false, false, /*jumpJustPressed*/ false));
}

TEST(ComputeSecondJumpTriggered, User4ModeTakesTheJumpButtonAwayAsATrigger) {
	// Q-Zandronum's asymmetry, preserved deliberately: with +USER4JUMP the jump button no longer
	// spends the second jump, so the two are not interchangeable.
	JumpFlags f = NoFlags();
	f.user4Jump = true;

	EXPECT_FALSE(ComputeSecondJumpTriggered(f, false, /*user4*/ false, /*jumpJustPressed*/ true));
	EXPECT_TRUE(ComputeSecondJumpTriggered(f, false, /*user4*/ true, /*jumpJustPressed*/ false));
}

TEST(ComputeSecondJumpTriggered, DoubleTapModeAcceptsATapOrUser4ButNotJump) {
	JumpFlags f = NoFlags();
	f.doubleTapJump = true;

	EXPECT_TRUE(ComputeSecondJumpTriggered(f, /*tap*/ true, false, false));
	EXPECT_TRUE(ComputeSecondJumpTriggered(f, false, /*user4*/ true, false));
	EXPECT_FALSE(ComputeSecondJumpTriggered(f, false, false, /*jumpJustPressed*/ true));
}

// ------------------------------------------------------------ double tap

TEST(ComputeDoubleTap, PressThenReleaseThenRePressFires) {
	// The full gesture, driven exactly as the engine would across three tics.
	int lastTap = 0, sjTics = 0, lastBefore = 0;

	// Tic 1: press forward. Opens the window, does not fire.
	DoubleTapResult r = ComputeDoubleTap(50, lastTap, sjTics, BT_FWD, 0, lastBefore, 12);
	EXPECT_FALSE(r.fired);
	EXPECT_EQ(-12, r.secondJumpTics);
	lastTap = r.lastTapValue; sjTics = r.secondJumpTics; lastBefore = r.lastMoveButtonsBefore;

	// Tic 2: release. Remembers which direction was held.
	r = ComputeDoubleTap(0, lastTap, sjTics, 0, BT_FWD, lastBefore, 12);
	EXPECT_FALSE(r.fired);
	EXPECT_EQ(BT_FWD, r.lastMoveButtonsBefore);
	lastTap = r.lastTapValue; sjTics = r.secondJumpTics; lastBefore = r.lastMoveButtonsBefore;

	// Tic 3: press the SAME direction again, still inside the window.
	r = ComputeDoubleTap(50, lastTap, sjTics, BT_FWD, 0, lastBefore, 12);
	EXPECT_TRUE(r.fired);
}

TEST(ComputeDoubleTap, ADifferentDirectionOnTheRePressDoesNotFire) {
	// Forward-then-back is a change of direction, not a double tap. Firing here would make the
	// dash go off constantly during ordinary strafing.
	const DoubleTapResult r = ComputeDoubleTap(50, 0, /*window open*/ -6, BT_BACK,
		/*moveButtonsOld*/ 0, /*lastMoveButtonsBefore*/ BT_FWD, 12);
	EXPECT_FALSE(r.fired);
	// ...and it restarts the window rather than leaving a stale one armed.
	EXPECT_EQ(-12, r.secondJumpTics);
}

TEST(ComputeDoubleTap, ARePressAfterTheWindowClosedDoesNotFire) {
	// secondJumpTics >= 0 means the window has expired.
	const DoubleTapResult r = ComputeDoubleTap(50, 0, /*window closed*/ 0, BT_FWD, 0, BT_FWD, 12);
	EXPECT_FALSE(r.fired);
	EXPECT_EQ(-12, r.secondJumpTics);
}

TEST(ComputeDoubleTap, HoldingADirectionSteadilyDoesNothing) {
	// Equal tap values mean no edge, so nothing changes -- a held direction must never fire.
	const DoubleTapResult r = ComputeDoubleTap(50, 50, -6, BT_FWD, BT_FWD, BT_FWD, 12);
	EXPECT_FALSE(r.fired);
	EXPECT_EQ(50, r.lastTapValue);
	EXPECT_EQ(-6, r.secondJumpTics);
	EXPECT_EQ(BT_FWD, r.lastMoveButtonsBefore);
}

TEST(ComputeDoubleTap, TapValueIsDirectionAgnostic) {
	// |forwardmove| + |sidemove| means a left tap and a right tap look the same, which is what lets
	// one detector serve every direction.
	const DoubleTapResult left = ComputeDoubleTap(50, 0, -6, BT_LEFT, 0, BT_LEFT, 12);
	EXPECT_TRUE(left.fired);
}

// ------------------------------------------------------------- velocities

TEST(ComputeJumpTics, DefaultIsTheNoDelaySentinel) {
	EXPECT_EQ(-1, ComputeJumpTics(false, false, false, TICRATE));
}

TEST(ComputeJumpTics, SkulltagJumpingUsesTheLegacyDelay) {
	EXPECT_EQ(18, ComputeJumpTics(true, false, false, TICRATE));
}

TEST(ComputeJumpTics, HighJumpDoublesTheDelay) {
	EXPECT_EQ(36, ComputeJumpTics(true, /*highJump*/ true, false, TICRATE));
}

TEST(ComputeJumpTics, SpringPadWinsOverEverythingElse) {
	// A spring pad must remove the delay outright, including the high-jump doubling.
	EXPECT_EQ(0, ComputeJumpTics(true, true, /*springPad*/ true, TICRATE));
	EXPECT_EQ(0, ComputeJumpTics(false, false, /*springPad*/ true, TICRATE));
}

TEST(ComputeVelZ, WorkInRawFixedUnitsSoFractionalJumpZSurvives) {
	// The regression this guards: these once took whole map units, so `Player.JumpZ 8.5` was
	// truncated to 8 and every fractional jump height in every mod silently changed.
	const long long FRACUNIT = 65536;
	const long long halfUnit = FRACUNIT / 2;

	// 8.5 units of jump velocity must come back as 8.5, not 8.
	EXPECT_EQ(8 * FRACUNIT + halfUnit,
		ComputeMainJumpVelZ(0, 8 * FRACUNIT + halfUnit, false));
	// An edge jump adds a fractional rise to a fractional jump without losing either.
	EXPECT_EQ(10 * FRACUNIT + halfUnit,
		ComputeMainJumpVelZ(2 * FRACUNIT, 8 * FRACUNIT + halfUnit, true));
	// And the floor comparison is exact at sub-unit differences.
	EXPECT_EQ(8 * FRACUNIT + halfUnit,
		ComputeSecondJumpVelZ(8 * FRACUNIT + halfUnit, 8 * FRACUNIT, false));
}

TEST(ComputeSecondJumpVelZ, IsAFloorNotAnAddition) {
	// Using a double jump while already rising faster must not SLOW you -- that would make the
	// double jump a punishment at the top of a rocket jump.
	EXPECT_EQ(100, ComputeSecondJumpVelZ(/*current*/ 100, /*secondJumpZ*/ 60, false));
	EXPECT_EQ(60, ComputeSecondJumpVelZ(/*current*/ 10, /*secondJumpZ*/ 60, false));
	// Falling: the second jump cancels the fall entirely.
	EXPECT_EQ(60, ComputeSecondJumpVelZ(/*current*/ -200, /*secondJumpZ*/ 60, false));
}

TEST(ComputeSecondJumpVelZ, HighJumpDoublesTheGrant) {
	EXPECT_EQ(120, ComputeSecondJumpVelZ(0, 60, /*highJump*/ true));
}

TEST(ComputeMainJumpVelZ, AnOrdinaryJumpReplacesVerticalVelocity) {
	EXPECT_EQ(60, ComputeMainJumpVelZ(/*current*/ 40, /*jumpVelZ*/ 60, /*edge*/ false));
	EXPECT_EQ(60, ComputeMainJumpVelZ(/*current*/ -40, /*jumpVelZ*/ 60, /*edge*/ false));
}

TEST(ComputeMainJumpVelZ, AnEdgeJumpKeepsExistingUpwardVelocity) {
	// The whole point of +EDGEJUMP: running off a rising ledge and jumping adds to the launch
	// instead of throwing it away.
	EXPECT_EQ(100, ComputeMainJumpVelZ(/*current*/ 40, /*jumpVelZ*/ 60, /*edge*/ true));
	// ...but downward velocity is still discarded, so it is never a penalty.
	EXPECT_EQ(60, ComputeMainJumpVelZ(/*current*/ -40, /*jumpVelZ*/ 60, /*edge*/ true));
}
