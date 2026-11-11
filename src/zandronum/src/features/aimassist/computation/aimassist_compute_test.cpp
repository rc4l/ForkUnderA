// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for fua_debug_aimassist's aim, turn, heading and target-order math.
#include "gtest/gtest.h"
#include "features/aimassist/computation/aimassist_compute.h"

#include <cmath>

using namespace zx::aimassist;

namespace
{
constexpr uint32_t kAngle90 = 0x40000000u;

// [Doruk-Aksoy] Applies a turn as P_PlayerThink does: yaw added, pitch taken away, each shifted up 16 bits.
void Apply(uint32_t &angle, int32_t &pitch, Turn turn)
{
	angle += (uint32_t)((int32_t)turn.yaw * 65536);
	pitch -= (int32_t)turn.pitch * 65536;
}
}

// [Doruk-Aksoy] A point east and level is yaw 0 pitch 0; one below and north looks down and north.
TEST(AimAssistCompute, AimAtPointsYawAndPitch)
{
	Aim aim = ComputeAimAt(0, 0, 0, 100, 0, 0);
	EXPECT_NEAR(aim.yawDeg, 0.0, 1e-9);
	EXPECT_NEAR(aim.pitchDeg, 0.0, 1e-9);
	aim = ComputeAimAt(0, 0, 100, 0, 100, 0);
	EXPECT_NEAR(aim.yawDeg, 90.0, 1e-9);
	EXPECT_NEAR(aim.pitchDeg, 45.0, 1e-9);
}

// [Doruk-Aksoy] Straight down or up is held to the engine's 56 and 32 degree limits.
TEST(AimAssistCompute, AimAtHoldsPitchLimits)
{
	EXPECT_NEAR(ComputeAimAt(0, 0, 1000, 1, 0, 0).pitchDeg, kPitchDownDeg, 1e-9);
	EXPECT_NEAR(ComputeAimAt(0, 0, 0, 1, 0, 1000).pitchDeg, -kPitchUpDeg, 1e-9);
}

// [Doruk-Aksoy] Degrees wrap into the 32-bit angle, negatives and full turns included.
TEST(AimAssistCompute, DegreesToAngle)
{
	EXPECT_EQ(ComputeDegToAngle(0), 0u);
	EXPECT_EQ(ComputeDegToAngle(90), kAngle90);
	EXPECT_EQ(ComputeDegToAngle(-90), 3u * kAngle90);
	EXPECT_EQ(ComputeDegToAngle(450), kAngle90);
	EXPECT_EQ(ComputeDegToAngle(360), 0u);
	EXPECT_EQ(ComputePitchDegToAngle(90), (int32_t)kAngle90);
	EXPECT_EQ(ComputePitchDegToAngle(-45), -(int32_t)(kAngle90 / 2));
}

// [Doruk-Aksoy] One tic's turn lands on the wanted view to within the 16 bits a ticcmd drops, the short way round.
TEST(AimAssistCompute, TicTurnLandsOnTheWantedView)
{
	const uint32_t starts[] = { 0u, kAngle90, 0xF0000000u, 0x12345678u };
	const double wants[] = { 0.0, 179.0, -179.0, 33.3, 270.0 };
	for (uint32_t start : starts)
		for (double want : wants)
		{
			uint32_t angle = start;
			int32_t pitch = ComputePitchDegToAngle(10);
			const uint32_t wantAngle = ComputeDegToAngle(want);
			const int32_t wantPitch = ComputePitchDegToAngle(-20);
			Apply(angle, pitch, ComputeTicTurn(angle, pitch, wantAngle, wantPitch));
			EXPECT_LE(std::abs((int32_t)(angle - wantAngle)), 1 << 15);
			EXPECT_LE(std::abs(pitch - wantPitch), 1 << 15);
		}
	const Turn shortWay = ComputeTicTurn(ComputeDegToAngle(350), 0, ComputeDegToAngle(10), 0);
	EXPECT_GT(shortWay.yaw, 0);
	EXPECT_LT(shortWay.yaw, 4000);
}

// [Doruk-Aksoy] A half turn stays in the short's range and a pitch that would read as the centre-view value is moved off it.
TEST(AimAssistCompute, TicTurnStaysInRangeAndNeverCentresTheView)
{
	EXPECT_EQ(ComputeTicTurn(0, 0, 0x80000000u, 0).yaw, -32768);
	EXPECT_EQ(ComputeTicTurn(0x80000000u, 0, 0, 0).yaw, -32768);
	EXPECT_EQ(ComputeTicTurn(0, 0, 0x7FFF8000u, 0).yaw, 32767);
	EXPECT_EQ(ComputeTicTurn(0, -(int32_t)kAngle90, 0, (int32_t)kAngle90).pitch, -32767);
	EXPECT_EQ(ComputeTicTurn(0, (int32_t)kAngle90, 0, -(int32_t)kAngle90).pitch, 32767);
}

// [Doruk-Aksoy] Input meant along the heading becomes the same world motion for any view: along a view 90 degrees right of it is a move to the left.
TEST(AimAssistCompute, MoveKeepsTheHeadingsWorldMotion)
{
	Move m = ComputeMoveOnHeading(1000, 0, 0, 0);
	EXPECT_EQ(m.forward, 1000);
	EXPECT_EQ(m.side, 0);
	m = ComputeMoveOnHeading(1000, 0, 90, 0);
	EXPECT_EQ(m.forward, 0);
	EXPECT_EQ(m.side, -1000);
	m = ComputeMoveOnHeading(0, 1000, 90, 0);
	EXPECT_EQ(m.forward, 1000);
	EXPECT_EQ(m.side, 0);
	for (double view = -180; view <= 180; view += 15)
	{
		const double f = 900, s = -400;
		const Move r = ComputeMoveOnHeading((int)f, (int)s, 30, view);
		const double h = 30 * 3.14159265358979323846 / 180, v = view * 3.14159265358979323846 / 180;
		// [Doruk-Aksoy] World motion: forward along the angle, side along the angle less 90 degrees.
		EXPECT_NEAR(r.forward * std::cos(v) + r.side * std::sin(v), f * std::cos(h) + s * std::sin(h), 1.5);
		EXPECT_NEAR(r.forward * std::sin(v) - r.side * std::cos(v), f * std::sin(h) - s * std::cos(h), 1.5);
	}
}

// [Doruk-Aksoy] A rotated move past a short's range is held to it.
TEST(AimAssistCompute, MoveHoldsShortRange)
{
	const Move m = ComputeMoveOnHeading(32767, 32767, 45, 0);
	EXPECT_EQ(m.forward, 32767);
	EXPECT_EQ(m.side, 0);
}

// [Doruk-Aksoy] Off-view degrees wrap both ways round.
TEST(AimAssistCompute, OffDegWraps)
{
	EXPECT_NEAR(ComputeOffDeg(0, 1, 0), 0.0, 1e-9);
	EXPECT_NEAR(ComputeOffDeg(170, -1, -0.17632698), 20.0, 1e-6);
	EXPECT_NEAR(ComputeOffDeg(-170, -1, 0.17632698), 20.0, 1e-6);
	EXPECT_NEAR(ComputeOffDeg(90, 0, -1), 180.0, 1e-9);
}

// [Doruk-Aksoy] Out of range scores -1; within, the score grows with distance and with how far off the view.
TEST(AimAssistCompute, TargetScore)
{
	EXPECT_EQ(ComputeTargetScore(1600, 0, 1500), -1.0);
	EXPECT_DOUBLE_EQ(ComputeTargetScore(1500, 0, 1500), 1500.0);
	EXPECT_DOUBLE_EQ(ComputeTargetScore(100, 45, 1500), 200.0);
	EXPECT_LT(ComputeTargetScore(300, 0, 1500), ComputeTargetScore(200, 90, 1500));
}

// [Doruk-Aksoy] Order is best first, ties kept in place, out-of-range left out.
TEST(AimAssistCompute, TargetOrder)
{
	const std::vector<size_t> order = ComputeTargetOrder({ 50.0, -1.0, 10.0, 50.0, 0.0 });
	ASSERT_EQ(order.size(), 4u);
	EXPECT_EQ(order[0], 4u);
	EXPECT_EQ(order[1], 2u);
	EXPECT_EQ(order[2], 0u);
	EXPECT_EQ(order[3], 3u);
	EXPECT_TRUE(ComputeTargetOrder({}).empty());
	EXPECT_TRUE(ComputeTargetOrder({ -1.0 }).empty());
}
