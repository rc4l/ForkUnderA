// [rc4l] Tests for fovinterp_compute. 100% coverage of the pure tween logic.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fovinterp_compute.h"
#include "gtest/gtest.h"

using namespace zx;

// The engine's historic hardcoded step, and the CVAR's default — the value at which this feature
// must reproduce the old behaviour exactly.
static const float DEFAULT_SPEED = 7.0f;

// ---------------------------------------------------------------------------
// FovChangeSpeedClamp
// ---------------------------------------------------------------------------

TEST(FovInterp, ChangeSpeedClampsAtOne)
{
	EXPECT_FLOAT_EQ(1.0f, FovChangeSpeedClamp(0.0f));
	EXPECT_FLOAT_EQ(1.0f, FovChangeSpeedClamp(-40.0f));
	EXPECT_FLOAT_EQ(1.0f, FovChangeSpeedClamp(1.0f));
	EXPECT_FLOAT_EQ(7.0f, FovChangeSpeedClamp(7.0f));
}

// ---------------------------------------------------------------------------
// FovClamp — matches R_SetFOV's range, which the interpolated value bypasses
// ---------------------------------------------------------------------------

TEST(FovInterp, ClampMatchesRSetFovRange)
{
	EXPECT_FLOAT_EQ(5.0f,   FovClamp(0.0f));
	EXPECT_FLOAT_EQ(5.0f,   FovClamp(-90.0f));
	EXPECT_FLOAT_EQ(5.0f,   FovClamp(5.0f));
	EXPECT_FLOAT_EQ(90.0f,  FovClamp(90.0f));
	EXPECT_FLOAT_EQ(170.0f, FovClamp(170.0f));
	EXPECT_FLOAT_EQ(170.0f, FovClamp(200.0f));
}

// ---------------------------------------------------------------------------
// FovTargetForWeapon
// ---------------------------------------------------------------------------

TEST(FovInterp, WeaponScaleAppliesWhenAliveAndArmed)
{
	EXPECT_FLOAT_EQ(45.0f, FovTargetForWeapon(90.0f, true, true, 0.5f));
}

TEST(FovInterp, WeaponScaleUsesMagnitude)
{
	// A negative scale is upstream's flag for "don't scale view-angle deltas" — the FOV effect
	// is the magnitude, so a -0.5 scope zooms in exactly like a +0.5 one.
	EXPECT_FLOAT_EQ(45.0f, FovTargetForWeapon(90.0f, true, true, -0.5f));
}

TEST(FovInterp, WeaponScaleIgnoredWhenItDoesNotApply)
{
	EXPECT_FLOAT_EQ(90.0f, FovTargetForWeapon(90.0f, false, true,  0.5f));  // dead
	EXPECT_FLOAT_EQ(90.0f, FovTargetForWeapon(90.0f, true,  false, 0.5f));  // no weapon
	// Zero means "this weapon makes no adjustment" — it must not collapse the target to zero.
	EXPECT_FLOAT_EQ(90.0f, FovTargetForWeapon(90.0f, true,  true,  0.0f));
}

// ---------------------------------------------------------------------------
// FovStepTic — the per-tic simulation step
// ---------------------------------------------------------------------------

TEST(FovInterp, StepIsAStandstillAtTheTarget)
{
	EXPECT_FLOAT_EQ(90.0f, FovStepTic(90.0f, 90.0f, DEFAULT_SPEED));
}

TEST(FovInterp, StepSnapsWhenInsideOneStep)
{
	// Remaining distance under the step lands exactly on the target rather than overshooting.
	EXPECT_FLOAT_EQ(90.0f, FovStepTic(87.0f, 90.0f, DEFAULT_SPEED));
	EXPECT_FLOAT_EQ(90.0f, FovStepTic(93.0f, 90.0f, DEFAULT_SPEED));
}

TEST(FovInterp, StepMovesByFlatSpeedWhenProportionalIsSmaller)
{
	// distance 100 -> proportional 2.5 < speed 7, so the flat step wins.
	EXPECT_FLOAT_EQ(83.0f, FovStepTic(90.0f, -10.0f, DEFAULT_SPEED));   // zooming in
	EXPECT_FLOAT_EQ(-3.0f, FovStepTic(-10.0f, 90.0f, DEFAULT_SPEED));   // zooming back out
}

TEST(FovInterp, StepMovesProportionallyOverLongDistances)
{
	// distance 400 -> proportional 10 > speed 7, so the proportional term takes over and a long
	// zoom does not crawl.
	EXPECT_FLOAT_EQ(390.0f, FovStepTic(400.0f, 0.0f, DEFAULT_SPEED));
	EXPECT_FLOAT_EQ(10.0f,  FovStepTic(0.0f, 400.0f, DEFAULT_SPEED));
}

TEST(FovInterp, StepHonoursACustomSpeed)
{
	EXPECT_FLOAT_EQ(70.0f, FovStepTic(90.0f, 10.0f, 20.0f));
	// ...and refuses a speed that would stall the view: 0 clamps to 1, and over this distance
	// the proportional term (80 * 0.025 = 2) is larger anyway, so it is what actually applies.
	EXPECT_FLOAT_EQ(88.0f, FovStepTic(90.0f, 10.0f, 0.0f));
	// A short hop is where the clamped floor itself decides the step.
	EXPECT_FLOAT_EQ(90.0f, FovStepTic(90.5f, 90.0f, 0.0f));   // 0.5 < 1 -> snap
}

TEST(FovInterp, StepReproducesTheHistoricHardcodedBehaviour)
{
	// The pre-feature engine moved by exactly this rule with a hardcoded 7.0. Walk a full zoom
	// and assert it still terminates on the target, never past it.
	float fov = 90.0f;
	const float target = 20.0f;
	for (int tics = 0; tics < 200 && fov != target; ++tics)
	{
		const float next = FovStepTic(fov, target, DEFAULT_SPEED);
		EXPECT_LT(next, fov);            // strictly monotonic while zooming in
		EXPECT_GE(next, target);         // never overshoots
		fov = next;
	}
	EXPECT_FLOAT_EQ(target, fov);
}

// ---------------------------------------------------------------------------
// FovRenderDelta — the sub-tic render offset
// ---------------------------------------------------------------------------

TEST(FovInterp, RenderDeltaIsZeroWhenNotInterpolating)
{
	// Paused / menu open: hold the view exactly where the sim left it.
	EXPECT_FLOAT_EQ(0.0f, FovRenderDelta(90.0f, 10.0f, DEFAULT_SPEED, 0.5f, false));
}

TEST(FovInterp, RenderDeltaIsZeroAtTheTarget)
{
	EXPECT_FLOAT_EQ(0.0f, FovRenderDelta(90.0f, 90.0f, DEFAULT_SPEED, 0.5f, true));
}

TEST(FovInterp, RenderDeltaIsAFractionOfTheNextStep)
{
	// step is -7 (flat speed); half a tic in, half of it has been rendered.
	EXPECT_FLOAT_EQ(-3.5f, FovRenderDelta(90.0f, -10.0f, DEFAULT_SPEED, 0.5f, true));
	EXPECT_FLOAT_EQ(-1.75f, FovRenderDelta(90.0f, -10.0f, DEFAULT_SPEED, 0.25f, true));
}

TEST(FovInterp, RenderDeltaLandsExactlyOnTheSimAtFullTic)
{
	// At ticFrac 1 the rendered FOV must equal what the sim is about to set, or the view jumps
	// on the tic boundary — the whole failure this feature exists to remove.
	const float current = 90.0f, target = 10.0f;
	const float rendered = current + FovRenderDelta(current, target, DEFAULT_SPEED, 1.0f, true);
	EXPECT_FLOAT_EQ(FovStepTic(current, target, DEFAULT_SPEED), rendered);
}

TEST(FovInterp, RenderDeltaClampsTicFrac)
{
	const float full = FovRenderDelta(90.0f, 10.0f, DEFAULT_SPEED, 1.0f, true);
	EXPECT_FLOAT_EQ(full, FovRenderDelta(90.0f, 10.0f, DEFAULT_SPEED, 1.5f, true));
	EXPECT_FLOAT_EQ(0.0f, FovRenderDelta(90.0f, 10.0f, DEFAULT_SPEED, -0.5f, true));
}

TEST(FovInterp, RenderDeltaSnapsWithinTheFinalStep)
{
	// Inside one step of the target the sim snaps, so the rendered delta interpolates the snap
	// rather than a fixed-size step.
	EXPECT_FLOAT_EQ(1.5f, FovRenderDelta(87.0f, 90.0f, DEFAULT_SPEED, 0.5f, true));
}

TEST(FovInterp, RenderDeltaZoomsOutwardToo)
{
	EXPECT_FLOAT_EQ(3.5f, FovRenderDelta(10.0f, 90.0f, DEFAULT_SPEED, 0.5f, true));
}
