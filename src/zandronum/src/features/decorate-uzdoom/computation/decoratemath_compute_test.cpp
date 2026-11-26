// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <initializer_list>

#include "features/decorate-uzdoom/computation/decoratemath_compute.h"

using namespace zx::decorateuzdoom;

namespace {

double One(MathOp op, double v)
{
	return ComputeMath(op, &v, 1);
}

double Many(MathOp op, std::initializer_list<double> args)
{
	return ComputeMath(op, args.begin(), args.size());
}

} // namespace

TEST(DecorateMathSpec, NamesEveryBuiltinCaseInsensitively)
{
	const char *names[] = { "exp", "LOG", "Log10", "ceil", "Floor", "round", "acos", "ASin", "atan", "tan", "cosh", "SINH",
		"tanh", "atan2", "vectorangle", "min", "MAX", "clamp" };
	for (const char *n : names)
		EXPECT_NE(ComputeMathSpec(n).op, kMathNone) << n;
}

TEST(DecorateMathSpec, ArgumentCounts)
{
	EXPECT_EQ(ComputeMathSpec("Floor").minArgs, 1u);
	EXPECT_EQ(ComputeMathSpec("Floor").maxArgs, 1u);
	EXPECT_EQ(ComputeMathSpec("ATan2").minArgs, 2u);
	EXPECT_EQ(ComputeMathSpec("VectorAngle").maxArgs, 2u);
	EXPECT_EQ(ComputeMathSpec("Min").minArgs, 2u);
	EXPECT_EQ(ComputeMathSpec("Max").maxArgs, kAnyArgs);
	EXPECT_EQ(ComputeMathSpec("Clamp").minArgs, 3u);
	EXPECT_EQ(ComputeMathSpec("Clamp").maxArgs, 3u);
}

TEST(DecorateMathSpec, UnknownNamesAreNone)
{
	EXPECT_EQ(ComputeMathSpec("Sin").op, kMathNone);
	EXPECT_EQ(ComputeMathSpec("Minimum").op, kMathNone);
	EXPECT_EQ(ComputeMathSpec("Mi").op, kMathNone);
	EXPECT_EQ(ComputeMathSpec("").op, kMathNone);
	EXPECT_EQ(ComputeMathSpec(nullptr).op, kMathNone);
}

TEST(DecorateMath, ResultIsIntOnlyForMinMaxClampOfInts)
{
	EXPECT_TRUE(ComputeMathResultIsInt(kMathMin, true));
	EXPECT_TRUE(ComputeMathResultIsInt(kMathMax, true));
	EXPECT_TRUE(ComputeMathResultIsInt(kMathClamp, true));
	EXPECT_FALSE(ComputeMathResultIsInt(kMathMin, false));
	EXPECT_FALSE(ComputeMathResultIsInt(kMathFloor, true));
	EXPECT_FALSE(ComputeMathResultIsInt(kMathRound, true));
}

TEST(DecorateMath, RoundingFunctions)
{
	EXPECT_EQ(One(kMathFloor, 1.5), 1);
	EXPECT_EQ(One(kMathFloor, -1.5), -2);
	EXPECT_EQ(One(kMathCeil, 1.25), 2);
	EXPECT_EQ(One(kMathCeil, 2.0), 2);
	EXPECT_EQ(One(kMathCeil, -1.5), -1);
	// [Doruk-Aksoy] C's round, halves away from zero, as UZDoom's FLOP_ROUND.
	EXPECT_EQ(One(kMathRound, 2.5), 3);
	EXPECT_EQ(One(kMathRound, -2.5), -3);
	EXPECT_EQ(One(kMathRound, 2.4), 2);
}

TEST(DecorateMath, ExponentsAndLogs)
{
	EXPECT_NEAR(One(kMathExp, 1), 2.718281828, 1e-9);
	EXPECT_NEAR(One(kMathLog, std::exp(2.0)), 2, 1e-12);
	EXPECT_NEAR(One(kMathLog10, 1000), 3, 1e-12);
	EXPECT_NEAR(One(kMathCosH, 0), 1, 1e-12);
	EXPECT_NEAR(One(kMathSinH, 0), 0, 1e-12);
	EXPECT_NEAR(One(kMathTanH, 100), 1, 1e-12);
}

TEST(DecorateMath, TrigTakesAndGivesDegrees)
{
	EXPECT_NEAR(One(kMathTan, 45), 1, 1e-12);
	EXPECT_NEAR(One(kMathACos, 0), 90, 1e-12);
	EXPECT_NEAR(One(kMathASin, 1), 90, 1e-12);
	EXPECT_NEAR(One(kMathATan, 1), 45, 1e-12);
}

TEST(DecorateMath, ATan2TakesYThenXAndVectorAngleXThenY)
{
	EXPECT_NEAR(Many(kMathATan2, { 1, 0 }), 90, 1e-12);
	EXPECT_NEAR(Many(kMathATan2, { 0, -1 }), 180, 1e-12);
	EXPECT_NEAR(Many(kMathVectorAngle, { 0, 1 }), 90, 1e-12);
	EXPECT_NEAR(Many(kMathVectorAngle, { -1, -1 }), -135, 1e-12);
}

TEST(DecorateMath, MinMaxOverAnyCount)
{
	EXPECT_EQ(Many(kMathMin, { 3, -2, 7 }), -2);
	EXPECT_EQ(Many(kMathMax, { 3, -2, 7, 4 }), 7);
	EXPECT_EQ(Many(kMathMin, { 1.5, 1.25 }), 1.25);
}

TEST(DecorateMath, ClampIsMinOfMaxSoAnInvertedRangeGivesTheTop)
{
	EXPECT_EQ(Many(kMathClamp, { 5, 0, 10 }), 5);
	EXPECT_EQ(Many(kMathClamp, { -5, 0, 10 }), 0);
	EXPECT_EQ(Many(kMathClamp, { 15, 0, 10 }), 10);
	EXPECT_EQ(Many(kMathClamp, { 5, 10, 0 }), 0);
}

TEST(DecorateMath, TooFewArgumentsAnswerSafely)
{
	EXPECT_EQ(ComputeMath(kMathFloor, nullptr, 0), 0);
	const double one = 3;
	EXPECT_EQ(ComputeMath(kMathATan2, &one, 1), 0);
	EXPECT_EQ(ComputeMath(kMathVectorAngle, &one, 1), 0);
	EXPECT_EQ(ComputeMath(kMathClamp, &one, 1), 3);
	EXPECT_EQ(ComputeMath(kMathNone, &one, 1), 0);
}

TEST(DecorateMath, Normalize180)
{
	EXPECT_EQ(ComputeNormalize180(0), 0);
	EXPECT_EQ(ComputeNormalize180(190), -170);
	EXPECT_EQ(ComputeNormalize180(-190), 170);
	EXPECT_EQ(ComputeNormalize180(180), 180);
	EXPECT_EQ(ComputeNormalize180(-180), 180);
	EXPECT_EQ(ComputeNormalize180(720 + 90), 90);
}

TEST(DecorateQueries, GetDistanceIs2DWithoutCheckzAndBetweenMiddlesWithIt)
{
	EXPECT_EQ(ComputeGetDistance(3, 4, 100, 56, 56, false), 5);
	EXPECT_EQ(ComputeGetDistance(3, 4, 0, 56, 56, true), 5);
	// [Doruk-Aksoy] A target 12 units up and 24 taller has its middle 24 above self's.
	EXPECT_EQ(ComputeGetDistance(0, 7, 12, 16, 40, true), 25);
}

TEST(DecorateQueries, GetAngleAbsoluteAndRelative)
{
	EXPECT_NEAR(ComputeGetAngle(0, 0, 0, 10, 0, 0), 90, 1e-12);
	EXPECT_NEAR(ComputeGetAngle(0, 0, -10, 0, 0, 0), 180, 1e-12);
	EXPECT_NEAR(ComputeGetAngle(0, 0, 0, 10, 45, kGafRelative), 45, 1e-12);
	EXPECT_NEAR(ComputeGetAngle(0, 0, 0, -10, 135, kGafRelative), 135, 1e-12);
	EXPECT_NEAR(ComputeGetAngle(0, 0, 10, 0, 350, kGafRelative), 10, 1e-9);
}

TEST(DecorateQueries, GetZAtPointAbsolute)
{
	double x, y;
	ComputeGetZAtPoint(100, 200, 90, 5, 6, 30, kGzfAbsolutePos, &x, &y);
	EXPECT_EQ(x, 5);
	EXPECT_EQ(y, 6);
}

TEST(DecorateQueries, GetZAtPointTurnsByTheActorsYaw)
{
	double x, y;
	// [Doruk-Aksoy] 64 ahead of an actor facing north is 64 north of it.
	ComputeGetZAtPoint(100, 200, 90, 64, 0, 0, 0, &x, &y);
	EXPECT_NEAR(x, 100, 1e-9);
	EXPECT_NEAR(y, 264, 1e-9);
	// [Doruk-Aksoy] A positive second offset goes to the actor's right, as UZDoom's px*s - py*c puts it.
	ComputeGetZAtPoint(0, 0, 0, 0, 32, 0, 0, &x, &y);
	EXPECT_NEAR(x, 0, 1e-9);
	EXPECT_NEAR(y, -32, 1e-9);
}

TEST(DecorateQueries, NextLowestFloorPicksTheFloorUnderFoot)
{
	// [Doruk-Aksoy] Two solid slabs listed top first, 128-144 and 64-80, over a floor at 0.
	const Plane3D floors[] = { { 144, 128, true }, { 80, 64, true } };
	EXPECT_EQ(ComputeNextLowestFloor(0, floors, 2, 200, 24, false), 144);
	EXPECT_EQ(ComputeNextLowestFloor(0, floors, 2, 100, 24, false), 80);
	EXPECT_EQ(ComputeNextLowestFloor(0, floors, 2, 40, 24, false), 0);
	// [Doruk-Aksoy] Feet 10 into the upper slab are within a step, unless restricted.
	EXPECT_EQ(ComputeNextLowestFloor(0, floors, 2, 134, 24, false), 144);
	EXPECT_EQ(ComputeNextLowestFloor(0, floors, 2, 134, 24, true), 80);
}

TEST(DecorateQueries, NextLowestFloorSkipsNonSolidAndSunkenSlabs)
{
	const Plane3D floors[] = { { 144, 128, false }, { -8, -16, true } };
	EXPECT_EQ(ComputeNextLowestFloor(0, floors, 2, 200, 24, false), 0);
	EXPECT_EQ(ComputeNextLowestFloor(0, nullptr, 0, 200, 24, false), 0);
}

TEST(DecorateQueries, NextHighestCeilingPicksTheSlabAboveTheActor)
{
	const Plane3D floors[] = { { 144, 128, true }, { 80, 64, true } };
	// [Doruk-Aksoy] An actor from 0 to 56 is nearer the lower slab's middle at its top, so that slab's bottom is its ceiling.
	EXPECT_EQ(ComputeNextHighestCeiling(256, floors, 2, 0, 56), 64);
	// [Doruk-Aksoy] One from 90 to 110 has the lower slab beneath it, so the upper one's bottom is.
	EXPECT_EQ(ComputeNextHighestCeiling(256, floors, 2, 90, 110), 128);
	EXPECT_EQ(ComputeNextHighestCeiling(256, floors, 2, 150, 200), 256);
	const Plane3D ghost[] = { { 144, 128, false } };
	EXPECT_EQ(ComputeNextHighestCeiling(256, ghost, 1, 90, 110), 256);
	const Plane3D above[] = { { 300, 280, true } };
	EXPECT_EQ(ComputeNextHighestCeiling(256, above, 1, 0, 56), 256);
}

TEST(DecorateQueries, GetZAtPointAbsoluteAngleIgnoresYaw)
{
	double x, y;
	ComputeGetZAtPoint(0, 0, 90, 64, 0, 180, kGzfAbsoluteAng, &x, &y);
	EXPECT_NEAR(x, -64, 1e-9);
	EXPECT_NEAR(y, 0, 1e-9);
}
