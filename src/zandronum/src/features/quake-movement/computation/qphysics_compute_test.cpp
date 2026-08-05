// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// Tests for the pure Quake movement arithmetic. These pin the *behaviour* that makes Quake movement
// feel like Quake movement -- strafe-jumping having headroom, excess speed bleeding off, a cap that
// keeps earned speed -- not just the algebra, so a refactor that quietly changes the feel fails.

#include "features/quake-movement/computation/qphysics_compute.h"

#include <gtest/gtest.h>
#include <cmath>

using namespace zx::quakemove;

namespace {

const float kEps = 1e-4f;

QVec3 V( float x, float y, float z = 0.0f )
{
	QVec3 v = { x, y, z };
	return v;
}

} // namespace

// ---------------------------------------------------------------- QFriction

TEST(QFriction, GroundBelowLimitDropsAFlatFrictionPerTic) {
	// Under the ground-speed limit the drop is friction*friction/TICRATE, independent of speed --
	// that flat term is what makes normal running speed stable rather than creeping downward.
	const float speed = 5.0f, friction = 6.0f, limit = 10.0f;
	const QFrictionResult r = QFriction(speed, speed, limit, friction, QFRICTION_GROUND);

	EXPECT_FALSE(r.stop);
	EXPECT_FALSE(r.scaleZ);
	const float expectedDrop = friction * friction / Q_TICRATE;
	EXPECT_NEAR((speed - expectedDrop) / speed, r.scale, kEps);
}

TEST(QFriction, GroundAboveLimitRemovesAConstantFractionPerTic) {
	// Past the limit the control term becomes the speed itself, so the drop is proportional and the
	// SCALE is speed-independent: excess speed decays exponentially toward the limit rather than
	// being subtracted away linearly. That difference is the reason a strafe-jump chain bleeds off
	// smoothly on landing instead of stopping dead.
	const float friction = 6.0f, limit = 10.0f;
	const QFrictionResult fast = QFriction(40.0f, 40.0f, limit, friction, QFRICTION_GROUND);
	const QFrictionResult faster = QFriction(100.0f, 100.0f, limit, friction, QFRICTION_GROUND);

	const float expectedScale = 1.0f - friction / Q_TICRATE;
	EXPECT_NEAR(expectedScale, fast.scale, kEps);
	EXPECT_NEAR(expectedScale, faster.scale, kEps);

	// The absolute loss, by contrast, does grow with speed.
	EXPECT_GT(100.0f * (1.0f - faster.scale), 40.0f * (1.0f - fast.scale));
}

TEST(QFriction, GroundBelowLimitRemovesAConstantAmountPerTic) {
	// The mirror image of the test above, and the reason the two regimes exist: below the limit the
	// ABSOLUTE drop is fixed, so the slower you go the larger the fraction removed -- which is what
	// brings a walking player to rest promptly instead of asymptotically.
	const float friction = 6.0f, limit = 10.0f;
	const QFrictionResult slow = QFriction(3.0f, 3.0f, limit, friction, QFRICTION_GROUND);
	const QFrictionResult quick = QFriction(9.0f, 9.0f, limit, friction, QFRICTION_GROUND);

	const float expectedDrop = friction * friction / Q_TICRATE;
	EXPECT_NEAR(expectedDrop, 3.0f * (1.0f - slow.scale), kEps);
	EXPECT_NEAR(expectedDrop, 9.0f * (1.0f - quick.scale), kEps);
	EXPECT_LT(slow.scale, quick.scale);
}

TEST(QFriction, AirborneAppliesNoDragAtAll) {
	// No air friction is the entire premise of the model -- momentum is conserved until you land.
	const QFrictionResult r = QFriction(30.0f, 30.0f, 10.0f, 6.0f, QFRICTION_AIRBORNE);

	EXPECT_FALSE(r.stop);
	EXPECT_NEAR(1.0f, r.scale, kEps);
}

TEST(QFriction, WaterScalesAllThreeAxes) {
	const QFrictionResult r = QFriction(4.0f, 4.0f, 0.0f, 2.0f, QFRICTION_WATER_OR_FLY);

	EXPECT_TRUE(r.scaleZ);
	const float expectedDrop = 4.0f * 2.0f / Q_TICRATE;
	EXPECT_NEAR((4.0f - expectedDrop) / 4.0f, r.scale, kEps);
}

TEST(QFriction, GroundStopsBelowOneUnitOfHorizontalSpeed) {
	// Scaling can never reach exactly zero, so without an explicit stop the pawn creeps forever.
	const QFrictionResult r = QFriction(0.9f, 0.9f, 10.0f, 6.0f, QFRICTION_GROUND);
	EXPECT_TRUE(r.stop);
}

TEST(QFriction, GroundUsesTheHorizontalSpeedForTheStopTest) {
	// A pawn falling fast but barely moving sideways must still be snapped to rest horizontally;
	// using the 3D speed here would leave it sliding.
	const QFrictionResult r = QFriction(/*speed3D*/ 20.0f, /*speed2D*/ 0.5f, 10.0f, 6.0f,
		QFRICTION_GROUND);
	EXPECT_TRUE(r.stop);
}

TEST(QFriction, WaterStopsAtAHalfUnitNotAFullOne) {
	// Water drag scales rather than subtracts, so a swimmer coasting at 0.9 would never reach the
	// ground threshold. Pin the lower bound, and pin that 0.4 does stop.
	EXPECT_FALSE(QFriction(0.9f, 0.9f, 0.0f, 2.0f, QFRICTION_WATER_OR_FLY).stop);
	EXPECT_TRUE(QFriction(0.4f, 0.4f, 0.0f, 2.0f, QFRICTION_WATER_OR_FLY).stop);
}

TEST(QFriction, RunawaySpeedIsLeftAloneEntirely) {
	// A pawn wedged in a corner produces absurd speeds; scaling them is a division-by-zero-shaped
	// failure, so the velocity is returned untouched.
	const QFrictionResult r = QFriction(50000.0f, 50000.0f, 10.0f, 6.0f, QFRICTION_GROUND);

	EXPECT_FALSE(r.stop);
	EXPECT_NEAR(1.0f, r.scale, kEps);
}

TEST(QFriction, DropLargerThanSpeedClampsToAFullStopNotNegativeScale) {
	// A huge friction value must not invert the velocity.
	const QFrictionResult r = QFriction(2.0f, 2.0f, 1.0f, 500.0f, QFRICTION_GROUND);
	EXPECT_NEAR(0.0f, r.scale, kEps);
	EXPECT_GE(r.scale, 0.0f);
}

TEST(QFriction, ZeroSpeedInAModeThatSkipsTheStopTestDoesNotDivideByZero) {
	// Airborne never sets `stop`, so a zero speed reaches the division guard.
	const QFrictionResult r = QFriction(0.0f, 0.0f, 10.0f, 6.0f, QFRICTION_AIRBORNE);
	EXPECT_NEAR(1.0f, r.scale, kEps);
	EXPECT_FALSE(std::isnan(r.scale));
}

// ----------------------------------------------------------- QAccelerationSpeed

TEST(QAccelerationSpeed, GivesNothingWhenAlreadyAtWishSpeed) {
	EXPECT_NEAR(0.0f, QAccelerationSpeed(/*current*/ 10.0f, /*wish*/ 10.0f, 10.0f), kEps);
	EXPECT_NEAR(0.0f, QAccelerationSpeed(/*current*/ 25.0f, /*wish*/ 10.0f, 10.0f), kEps);
}

TEST(QAccelerationSpeed, IsCappedByTheRemainingHeadroom) {
	// Close to the wish speed, only the shortfall is granted -- never an overshoot.
	const float granted = QAccelerationSpeed(9.9f, 10.0f, 100.0f);
	EXPECT_NEAR(0.1f, granted, kEps);
}

TEST(QAccelerationSpeed, IsOtherwiseAccelTimesWishSpeedPerTic) {
	const float wish = 10.0f, accel = 7.0f;
	EXPECT_NEAR(accel * wish / Q_TICRATE, QAccelerationSpeed(0.0f, wish, accel), kEps);
}

TEST(QAccelerationSpeed, StrafeJumpingHasHeadroomBecauseCurrentSpeedIsProjected) {
	// The core of the whole model: `currentSpeed` is the dot of velocity onto the WISH direction.
	// Moving fast forward while wishing sideways projects to ~0, so a sideways wish still
	// accelerates even though the pawn is already over the wish speed outright. If this ever
	// returns 0, strafe-jumping is dead.
	const float sidewaysProjection = 0.0f;   // travelling +X at speed 30, wishing +Y
	EXPECT_GT(QAccelerationSpeed(sidewaysProjection, 10.0f, 7.0f), 0.0f);
	// ...whereas wishing straight ahead at that speed gives nothing.
	EXPECT_NEAR(0.0f, QAccelerationSpeed(30.0f, 10.0f, 7.0f), kEps);
}

// -------------------------------------------------------------- velocity cap

TEST(QLocalVelocityCap, ZeroCapMeansUncapped) {
	EXPECT_NEAR(0.0f, QLocalVelocityCap(0.0f, 25.0f), kEps);
	EXPECT_NEAR(1.0f, QVelocityCapScale(25.0f, 0.0f), kEps);
}

TEST(QLocalVelocityCap, IsStickySoEarnedSpeedIsKept) {
	// A player who rocket-jumped to 40 keeps 40 even though the cap is 20 -- the cap stops them
	// ADDING speed, it does not yank them to a halt in mid-air.
	EXPECT_NEAR(40.0f, QLocalVelocityCap(20.0f, 40.0f), kEps);
	EXPECT_NEAR(20.0f, QLocalVelocityCap(20.0f, 5.0f), kEps);
}

TEST(QVelocityCapScale, ScalesDownOnlyWhenOverTheCap) {
	EXPECT_NEAR(1.0f, QVelocityCapScale(10.0f, 20.0f), kEps);
	EXPECT_NEAR(1.0f, QVelocityCapScale(20.0f, 20.0f), kEps);
	EXPECT_NEAR(0.5f, QVelocityCapScale(40.0f, 20.0f), kEps);
}

TEST(QVelocityCapScale, ZeroSpeedDoesNotDivideByZero) {
	const float scale = QVelocityCapScale(0.0f, 20.0f);
	EXPECT_NEAR(1.0f, scale, kEps);
	EXPECT_FALSE(std::isnan(scale));
}

// -------------------------------------------------------------- move tiers

TEST(QWalkCrouchTier, CoversTheFourCombinations) {
	EXPECT_EQ(QTIER_WALK, QWalkCrouchTier(false, false));
	EXPECT_EQ(QTIER_RUN, QWalkCrouchTier(true, false));
	EXPECT_EQ(QTIER_CROUCH_WALK, QWalkCrouchTier(false, true));
	EXPECT_EQ(QTIER_CROUCH_RUN, QWalkCrouchTier(true, true));
}

TEST(QWalkCrouchTier, TheOrderIsTheDecorateContract) {
	// Player.ForwardMove/SideMove/FootstepsEnabled are addressed POSITIONALLY by mods, so these
	// indices are API. Rearranging them would silently repoint every authored value.
	EXPECT_EQ(0, QTIER_WALK);
	EXPECT_EQ(1, QTIER_RUN);
	EXPECT_EQ(2, QTIER_CROUCH_WALK);
	EXPECT_EQ(3, QTIER_CROUCH_RUN);
}

TEST(QTierScale, RunUsesItsEntryAsAuthoredAndTheOthersScaleDown) {
	EXPECT_FLOAT_EQ(1.0f, QTierScale(QTIER_RUN));
	EXPECT_FLOAT_EQ(0.5f, QTierScale(QTIER_WALK));
	EXPECT_FLOAT_EQ(0.25f, QTierScale(QTIER_CROUCH_WALK));
	EXPECT_FLOAT_EQ(0.5f, QTierScale(QTIER_CROUCH_RUN));
}

TEST(QTierScale, CrouchWalkingIsTheSlowestAndRunningTheFastest) {
	// The ordering is what a player feels; pin it rather than only the literals.
	EXPECT_LT(QTierScale(QTIER_CROUCH_WALK), QTierScale(QTIER_WALK));
	EXPECT_LT(QTierScale(QTIER_WALK), QTierScale(QTIER_RUN));
	EXPECT_LT(QTierScale(QTIER_CROUCH_RUN), QTierScale(QTIER_RUN));
}

TEST(QTierScale, AnOutOfRangeTierFailsOpenRatherThanToZero) {
	// Unreachable through QWalkCrouchTier, but returning 0 here would freeze the pawn outright,
	// which is a far worse failure than moving at full speed.
	EXPECT_FLOAT_EQ(1.0f, QTierScale(99));
	EXPECT_FLOAT_EQ(1.0f, QTierScale(-1));
}

TEST(QCrouchHalfWay, SitsMidwayBetweenStandingAndTheAuthoredCrouchDepth) {
	EXPECT_FLOAT_EQ(0.75f, QCrouchHalfWay(0.5f));   // the engine default
	EXPECT_FLOAT_EQ(0.9f, QCrouchHalfWay(0.8f));    // a shallow crouch
	// A pawn that cannot crouch at all has its threshold at standing height, so nothing counts as
	// crouched -- crouch slide simply never engages rather than engaging always.
	EXPECT_FLOAT_EQ(1.0f, QCrouchHalfWay(1.0f));
}

// ------------------------------------------------------------ floor friction

TEST(QFloorFriction, DefaultFloorIsExactlyNeutralForBothCurves) {
	// The two magic numbers are the engine's defaults; a default floor must not alter the model.
	EXPECT_NEAR(1.0f, QFloorFrictionForAccel(2048), kEps);
	EXPECT_NEAR(1.0f, QFloorFrictionForFriction(59392), kEps);
}

TEST(QFloorFrictionForAccel, EighthRootBluntsTheEffect) {
	// Half the move factor is nowhere near half the acceleration -- that blunting is deliberate.
	const float half = QFloorFrictionForAccel(1024);
	EXPECT_NEAR(std::pow(0.5f, 0.125f), half, kEps);
	EXPECT_GT(half, 0.9f);
}

TEST(QFloorFrictionForFriction, SixteenthPowerAmplifiesAnIcyFloor) {
	// An icier floor (lower friction value) must produce markedly LESS drag, or custom ice does
	// nothing at all against Quake's much higher base friction.
	EXPECT_LT(QFloorFrictionForFriction(59392 * 2), 1.0f);
	EXPECT_GT(QFloorFrictionForFriction(59392 / 2), 1.0f);
}

TEST(QFloorFrictionForFriction, IsClampedSoItCanNeverStallTheModel) {
	// THE regression this exists for. Unclamped, a floor twice as icy as default gives 2^16 =
	// 65536; multiplying GroundFriction by that makes one tic's friction drop exceed any speed the
	// pawn can reach, so it is pinned in place and cannot move at all. The clamp is what keeps an
	// authored floor expressive without being able to break movement outright.
	EXPECT_LE(QFloorFrictionForFriction(59392 / 4), Q_FLOOR_FRICTION_MAX);
	EXPECT_GE(QFloorFrictionForFriction(59392 * 4), Q_FLOOR_FRICTION_MIN);
	// Even an absurd authored value stays inside the band rather than reaching infinity.
	const float extreme = QFloorFrictionForFriction(1);
	EXPECT_LE(extreme, Q_FLOOR_FRICTION_MAX);
	EXPECT_EQ(extreme, extreme);   // not NaN
}

TEST(QFloorFrictionForFriction, TheNeutralFloorIsExactByConstruction) {
	// Short-circuited rather than computed, so the ordinary floor never depends on how the
	// exponential path happens to round -- 1.0 in, exactly 1.0 out.
	EXPECT_EQ(1.0f, QFloorFrictionForFriction(Q_DEFAULT_FLOOR_FRICTION));
	EXPECT_EQ(1.0f, QFloorFrictionForAccel(Q_DEFAULT_FLOOR_MOVEFACTOR));
}

TEST(QFloorFriction, DegenerateInputsDoNotProduceInfinitiesOrNaNs) {
	EXPECT_NEAR(0.0f, QFloorFrictionForAccel(0), kEps);
	EXPECT_NEAR(0.0f, QFloorFrictionForAccel(-100), kEps);
	EXPECT_NEAR(0.0f, QFloorFrictionForFriction(0), kEps);
}

TEST(QFloorFrictionForFriction, AModestlyIcyFloorLandsBetweenTheClampsUntouched) {
	// The clamp tests above only pin the ENDS. A floor a little off default has to come back as the
	// computed curve rather than a bound, or the clamps would be silently swallowing the whole
	// range and every non-default floor would feel identical.
	const int slightlyIcy = static_cast<int>(Q_DEFAULT_FLOOR_FRICTION / 1.1f);
	const float value = QFloorFrictionForFriction(slightlyIcy);

	EXPECT_GT(value, Q_FLOOR_FRICTION_MIN);
	EXPECT_LT(value, Q_FLOOR_FRICTION_MAX);
	// ratio^16 for a ratio of about 1.1.
	EXPECT_NEAR(4.59f, value, 0.2f);
}

TEST(QFriction, ZeroSpeedPastTheStopTestIsNotDividedBy) {
	// speed2D clears the ground stop test while the 3D speed is zero. The engine cannot produce
	// that pairing -- 3D speed is never below 2D -- but this is a public function and the guard is
	// what stops a degenerate caller turning into a division by zero and an infinite drop.
	const QFrictionResult result = QFriction(0.0f, 5.0f, 10.0f, 6.0f, QFRICTION_GROUND);

	EXPECT_FALSE(result.stop);
	EXPECT_EQ(1.0f, result.scale) << "an untouched result must not scale velocity";
}

// ------------------------------------------------------------- vector helpers

TEST(QVectorRotate, RotatesNinetyDegreesCounterClockwise) {
	float x = 1.0f, y = 0.0f;
	QVectorRotate(x, y, 90.0f);
	EXPECT_NEAR(0.0f, x, kEps);
	EXPECT_NEAR(1.0f, y, kEps);
}

TEST(QVectorRotate, ZeroDegreesIsIdentityAndPreservesLength) {
	float x = 3.0f, y = 4.0f;
	QVectorRotate(x, y, 0.0f);
	EXPECT_NEAR(3.0f, x, kEps);
	EXPECT_NEAR(4.0f, y, kEps);

	QVectorRotate(x, y, 137.0f);
	EXPECT_NEAR(5.0f, QLength2D(x, y), kEps);
}

TEST(QVectorRotate, FullTurnReturnsToTheStart) {
	float x = 2.0f, y = -1.0f;
	QVectorRotate(x, y, 360.0f);
	EXPECT_NEAR(2.0f, x, 1e-3f);
	EXPECT_NEAR(-1.0f, y, 1e-3f);
}

TEST(QVectorHelpers, DotAndLengths) {
	EXPECT_NEAR(32.0f, QDotProduct(V(1, 2, 3), V(4, 5, 6)), kEps);
	EXPECT_NEAR(5.0f, QLength2D(3.0f, 4.0f), kEps);
	EXPECT_NEAR(13.0f, QLength3D(V(3, 4, 12)), kEps);
}

TEST(QMakeUnit, NormalisesAndSurvivesAZeroVector) {
	const QVec3 unit = QMakeUnit(V(0, 3, 4));
	EXPECT_NEAR(1.0f, QLength3D(unit), kEps);
	EXPECT_NEAR(0.6f, unit.y, kEps);

	// A player standing still has no wish direction; that must be {0,0,0}, not a NaN that would
	// poison every downstream dot product.
	const QVec3 zero = QMakeUnit(V(0, 0, 0));
	EXPECT_NEAR(0.0f, QLength3D(zero), kEps);
	EXPECT_FALSE(std::isnan(zero.x));
}

// -------------------------------------------------------- CPM forward clamp

TEST(QCpmClampForwardWish, LeavesAWishInsideTheConeAlone) {
	bool applied = true;
	const QVec3 vel = V(1, 0, 0);
	const QVec3 wish = V(1, 0, 0);
	const QVec3 out = QCpmClampForwardWish(vel, wish, 0.122173f, applied);

	EXPECT_FALSE(applied);
	EXPECT_NEAR(1.0f, out.x, kEps);
	EXPECT_NEAR(0.0f, out.y, kEps);
}

TEST(QCpmClampForwardWish, ClampsAWideTurnToExactlyTheMaximumAngle) {
	// Asking for 90 degrees with a 7-degree limit must yield exactly 7 degrees off the CURRENT
	// heading -- this is what stops CPM air control being an instant 180.
	bool applied = false;
	const float maxAngle = 0.122173f;   // 7 degrees
	const QVec3 vel = V(1, 0, 0);
	const QVec3 wish = V(0, 1, 0);
	const QVec3 out = QCpmClampForwardWish(vel, wish, maxAngle, applied);

	EXPECT_TRUE(applied);
	EXPECT_NEAR(1.0f, QLength2D(out.x, out.y), kEps);
	const float angle = std::acos(QDotProduct(vel, out));
	EXPECT_NEAR(maxAngle, angle, 1e-3f);
}

TEST(QCpmClampForwardWish, TurnsTheShortWayInBothDirections) {
	// Left and right must be mirror images; picking the wrong sign sends the player the long way
	// round, which reads as the controls inverting at speed.
	bool applied = false;
	const float maxAngle = 0.122173f;
	const QVec3 vel = V(1, 0, 0);

	const QVec3 left = QCpmClampForwardWish(vel, V(0, 1, 0), maxAngle, applied);
	EXPECT_TRUE(applied);
	EXPECT_GT(left.y, 0.0f);

	const QVec3 right = QCpmClampForwardWish(vel, V(0, -1, 0), maxAngle, applied);
	EXPECT_TRUE(applied);
	EXPECT_LT(right.y, 0.0f);

	EXPECT_NEAR(left.x, right.x, kEps);
	EXPECT_NEAR(left.y, -right.y, kEps);
}

TEST(QCpmClampForwardWish, PreservesTheWishZComponent) {
	// The clamp is a horizontal turn limiter; it must not flatten a vertical wish.
	bool applied = false;
	const QVec3 out = QCpmClampForwardWish(V(1, 0, 0), V(0, 1, 0.5f), 0.122173f, applied);
	EXPECT_NEAR(0.5f, out.z, kEps);
}

TEST(QCpmClampForwardWish, AZeroAngleLimitPinsTheWishToTheCurrentHeading) {
	// Degenerate but reachable via Player.CpmMaxForwardAngleRad 0 -- must mean "no turning", not a
	// NaN or an unchanged wish.
	bool applied = false;
	const QVec3 vel = V(1, 0, 0);
	const QVec3 out = QCpmClampForwardWish(vel, V(0, 1, 0), 0.0f, applied);

	EXPECT_TRUE(applied);
	EXPECT_NEAR(1.0f, out.x, kEps);
	EXPECT_NEAR(0.0f, out.y, kEps);
}
