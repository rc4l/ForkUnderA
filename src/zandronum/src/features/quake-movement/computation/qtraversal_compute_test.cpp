// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// Tests for the traversal charge bookkeeping. The signed crouch-slide counter is the interesting
// part: one variable carries both "charge remaining" and "locked out", and getting a sign flip
// wrong yields a slide that silently refuses to start or one that never runs out.

#include "features/quake-movement/computation/qtraversal_compute.h"

#include <gtest/gtest.h>

using namespace zx::quakemove;

namespace {
const float kEps = 1e-4f;
}

// ------------------------------------------------- crouch slide charge

TEST(RegenSlideCharge, RegeneratesTowardTheCapWhileAirborne) {
	EXPECT_NEAR(12.0f, RegenSlideCharge(10.0f, 35.0f, 2.0f), kEps);
	EXPECT_NEAR(35.0f, RegenSlideCharge(34.0f, 35.0f, 2.0f), kEps);
	EXPECT_NEAR(35.0f, RegenSlideCharge(35.0f, 35.0f, 2.0f), kEps);
}

TEST(RegenSlideCharge, LeavingTheGroundReleasesALockout) {
	// A negative counter is a lockout. Going airborne flips it positive -- that is the whole
	// "you get your slide back by leaving the ground" rule.
	EXPECT_NEAR(12.0f, RegenSlideCharge(-10.0f, 35.0f, 2.0f), kEps);
}

TEST(RegenSlideCharge, ALockoutIsNotRefundedInFullMoreThanItWasBanked) {
	// Flipping preserves magnitude, so a deep lockout comes back as a correspondingly small
	// charge -- releasing it must not hand back a full meter.
	const float released = RegenSlideCharge(-30.0f, 35.0f, 2.0f);
	EXPECT_NEAR(32.0f, released, kEps);
	EXPECT_LT(released, 35.0f);
}

TEST(DrainSlideCharge, StandingUpConvertsChargeIntoLockout) {
	// Not merely "stops regenerating": a usable charge is pushed negative, so standing still on the
	// ground actively costs you the slide rather than preserving it.
	EXPECT_NEAR(-12.0f, DrainSlideCharge(10.0f, 35.0f, 2.0f), kEps);
}

TEST(DrainSlideCharge, LockoutDeepensButIsFlooredAtTheCap) {
	EXPECT_NEAR(-12.0f, DrainSlideCharge(-10.0f, 35.0f, 2.0f), kEps);
	EXPECT_NEAR(-35.0f, DrainSlideCharge(-34.0f, 35.0f, 2.0f), kEps);
	EXPECT_NEAR(-35.0f, DrainSlideCharge(-35.0f, 35.0f, 2.0f), kEps);
}

TEST(CanCrouchSlide, NeedsFlagCrouchAndPositiveCharge) {
	EXPECT_TRUE(CanCrouchSlide(true, true, 5.0f));
	EXPECT_FALSE(CanCrouchSlide(false, true, 5.0f));
	EXPECT_FALSE(CanCrouchSlide(true, false, 5.0f));
	EXPECT_FALSE(CanCrouchSlide(true, true, 0.0f));
}

TEST(CanCrouchSlide, ALockedOutChargeIsNotCharge) {
	// The bug this guards: treating the counter as a magnitude would let a fully locked-out player
	// slide indefinitely.
	EXPECT_FALSE(CanCrouchSlide(true, true, -20.0f));
}

TEST(CrouchSlideCharge, RoundTripsThroughAFullUseCycle) {
	// Drive the whole loop the engine would: slide it down, stand up to lock out, go airborne to
	// release, and confirm it is usable again.
	float tics = 20.0f;
	for (int i = 0; i < 20; ++i) tics = SpendCharge(tics);
	EXPECT_NEAR(0.0f, tics, kEps);
	EXPECT_FALSE(CanCrouchSlide(true, true, tics));

	tics = DrainSlideCharge(tics, 35.0f, 2.0f);      // stand up
	EXPECT_LT(tics, 0.0f);
	EXPECT_FALSE(CanCrouchSlide(true, true, tics));

	tics = RegenSlideCharge(tics, 35.0f, 2.0f);      // leave the ground
	EXPECT_GT(tics, 0.0f);
	EXPECT_TRUE(CanCrouchSlide(true, true, tics));
}

// ------------------------------------------- wall climb / air wall run

TEST(RegenSimpleCharge, RegeneratesAndCaps) {
	EXPECT_NEAR(7.0f, RegenSimpleCharge(5.0f, 20.0f, 2.0f), kEps);
	EXPECT_NEAR(20.0f, RegenSimpleCharge(19.5f, 20.0f, 2.0f), kEps);
}

TEST(RegenSimpleCharge, HasNoLockoutSignTrickery) {
	// Unlike the slide meter these simply bottom out at zero, so a negative never appears and a
	// negative input would just regenerate normally.
	EXPECT_NEAR(1.0f, RegenSimpleCharge(-1.0f, 20.0f, 2.0f), kEps);
}

TEST(SpendAndHasCharge, SpendOneTicAndGateAtZero) {
	EXPECT_NEAR(4.0f, SpendCharge(5.0f), kEps);
	EXPECT_TRUE(HasCharge(0.5f));
	EXPECT_FALSE(HasCharge(0.0f));
	EXPECT_FALSE(HasCharge(-3.0f));
}

// --------------------------------------------------- air wall run test

TEST(AirWallRunEngages, NeedsToBeTravellingAlongTheWall) {
	// Parallel to the wall: engages.
	EXPECT_TRUE(AirWallRunEngages(0.99f));
	// Straight into it: does not -- that is a collision, not a wall run.
	EXPECT_FALSE(AirWallRunEngages(0.0f));
	EXPECT_FALSE(AirWallRunEngages(0.5f));
}

TEST(AirWallRunEngages, IsDirectionAgnostic) {
	// Running the wall left-to-right and right-to-left are equally valid, so only the magnitude of
	// the dot matters. Testing the sign would make the move work on only one side of every wall.
	EXPECT_TRUE(AirWallRunEngages(-0.99f));
	EXPECT_EQ(AirWallRunEngages(0.8f), AirWallRunEngages(-0.8f));
}

TEST(AirWallRunEngages, TheThresholdIsExclusive) {
	EXPECT_FALSE(AirWallRunEngages(0.75f));
	EXPECT_TRUE(AirWallRunEngages(0.7501f));
}

// ------------------------------------------------------ effect cadence

TEST(ShouldEmitEffect, EmitsOnZeroThenReloadsAndCountsDown) {
	int tics = 0;
	EXPECT_TRUE(ShouldEmitEffect(tics, 3));
	EXPECT_EQ(3, tics);

	EXPECT_FALSE(ShouldEmitEffect(tics, 3));
	EXPECT_EQ(2, tics);
	EXPECT_FALSE(ShouldEmitEffect(tics, 3));
	EXPECT_FALSE(ShouldEmitEffect(tics, 3));
	EXPECT_EQ(0, tics);

	// ...and emits again exactly on the reload boundary.
	EXPECT_TRUE(ShouldEmitEffect(tics, 3));
}

TEST(ShouldEmitEffect, AZeroIntervalEmitsEveryTic) {
	// Reachable via Player.CrouchSlideEffectInterval 0; must not divide by anything or stall.
	int tics = 0;
	EXPECT_TRUE(ShouldEmitEffect(tics, 0));
	EXPECT_TRUE(ShouldEmitEffect(tics, 0));
}
