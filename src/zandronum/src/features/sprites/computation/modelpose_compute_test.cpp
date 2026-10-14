// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/modelpose_compute.h"

#include <gtest/gtest.h>

using namespace zx::sprites;

namespace {

ModelPose Still()
{
	ModelPose p = {};
	p.scale[0] = p.scale[1] = p.scale[2] = 1.f;
	p.stretch = 1.f;
	p.spinAxis[1] = 1.f;
	return p;
}

// [rc4l] A point through a column-major matrix.
void Apply(const float *m, float x, float y, float z, float *out)
{
	for (int r = 0; r < 3; r++) out[r] = m[r] * x + m[4 + r] * y + m[8 + r] * z + m[12 + r];
}

} // namespace

TEST(ModelPose, AStillPoseLeavesThePointWhereItIs)
{
	float m[16];
	ComputeModelMatrix(Still(), m);
	for (int i = 0; i < 16; i++) EXPECT_FLOAT_EQ(m[i], (i % 5) == 0 ? 1.f : 0.f);
}

TEST(ModelPose, PositionThenTurnThenScaleThenOffset)
{
	ModelPose p = Still();
	p.position[0] = 100; p.position[1] = 20; p.position[2] = -50;
	p.yaw = 90;
	p.scale[0] = 2; p.scale[1] = 3; p.scale[2] = 4;
	p.offset[0] = 1;
	float m[16], o[3];
	ComputeModelMatrix(p, m);
	// [rc4l] The offset is scaled, then turned a quarter about up, x going to -y (GL's third axis), then moved.
	Apply(m, 0, 0, 0, o);
	EXPECT_NEAR(o[0], 100.f, 1e-4f);
	EXPECT_NEAR(o[1], 20.f, 1e-4f);
	EXPECT_NEAR(o[2], -52.f, 1e-4f);
	Apply(m, 0, 1, 0, o);
	EXPECT_NEAR(o[1], 23.f, 1e-4f);
}

TEST(ModelPose, PitchRollAndTheOffsetsTurnAboutTheirOwnAxes)
{
	ModelPose p = Still();
	p.pitch = 90;
	float m[16], o[3];
	ComputeModelMatrix(p, m);
	Apply(m, 1, 0, 0, o);   // [rc4l] about z: x goes to up
	EXPECT_NEAR(o[1], 1.f, 1e-6f);
	p = Still();
	p.roll = 90;
	ComputeModelMatrix(p, m);
	Apply(m, 0, 1, 0, o);   // [rc4l] about x: up goes to z
	EXPECT_NEAR(o[2], 1.f, 1e-6f);
	p = Still();
	p.yawOffset = 90; p.pitchOffset = 90; p.rollOffset = 90;
	ComputeModelMatrix(p, m);
	Apply(m, 0, 0, 1, o);
	EXPECT_NEAR(o[0] * o[0] + o[1] * o[1] + o[2] * o[2], 1.f, 1e-5f);
}

TEST(ModelPose, ASpinTurnsAboutItsCentreAndTheStretchIsLast)
{
	ModelPose p = Still();
	p.rotating = true;
	p.spin = 180;
	p.spinCenter[0] = 10;
	p.spinAxis[0] = 0; p.spinAxis[1] = 2; p.spinAxis[2] = 0;   // [rc4l] not unit length: rotate normalises it
	p.stretch = 1.2f;
	float m[16], o[3];
	ComputeModelMatrix(p, m);
	Apply(m, 10, 0, 0, o);   // [rc4l] the centre stays put
	EXPECT_NEAR(o[0], 10.f, 1e-4f);
	Apply(m, 0, 0, 0, o);    // [rc4l] the origin swings to the far side of it
	EXPECT_NEAR(o[0], 20.f, 1e-4f);
	Apply(m, 0, 1, 0, o);
	EXPECT_NEAR(o[1], 1.2f, 1e-5f);
}

TEST(ModelPose, MomentumPitchFollowsTheClimbAndTurnsOverBackwards)
{
	EXPECT_NEAR(ComputeMomentumPitch(1, 0, 1, 0), 45.f, 1e-4f);
	EXPECT_NEAR(ComputeMomentumPitch(-1, 0, 1, 0), -45.f, 1e-4f);
	EXPECT_NEAR(ComputeMomentumPitch(0, 1, -1, 90), -45.f, 1e-4f);
	EXPECT_NEAR(ComputeMomentumPitch(0, 0, -5, 0), 90.f, 1e-4f);   // [rc4l] straight down reads as its size
	EXPECT_EQ(ComputeMomentumPitch(0, 0, 0, 0), 0.f);
}

TEST(ModelPose, InheritedDegreesReadTheWholeAndFractionalParts)
{
	EXPECT_FLOAT_EQ(ComputeInheritedDegrees(8192LL << 16), 45.f);
	EXPECT_FLOAT_EQ(ComputeInheritedDegrees(-(8192LL << 16)), -45.f);
	EXPECT_FLOAT_EQ(ComputeInheritedDegrees(0x8000), float(32768.0 / (1 << 29) * 45));
}

TEST(ModelPose, TheSpinWrapsIntoOneTurn)
{
	EXPECT_FLOAT_EQ(ComputeModelSpin(200, 1.25f), 90.f);
	EXPECT_FLOAT_EQ(ComputeModelSpin(-200, 1.25f), 270.f);
}

TEST(ModelPose, RollAgainstAngleKeepsABeamEdgeOn)
{
	EXPECT_FLOAT_EQ(ComputeRollAgainstAngle(10, 0, false), -10.f);
	EXPECT_FLOAT_EQ(ComputeRollAgainstAngle(10, 0, true), 10.f);
	EXPECT_FLOAT_EQ(ComputeRollAgainstAngle(200, 0, false), -160.f);
	EXPECT_FLOAT_EQ(ComputeRollAgainstAngle(0, 200, true), -160.f);
}

TEST(ModelPose, AlignPitchLooksFromTheEye)
{
	EXPECT_NEAR(ComputeAlignPitch(1, 1), 45.f, 1e-4f);
	EXPECT_NEAR(ComputeAlignPitch(-1, 0), -90.f, 1e-4f);
}

TEST(ModelPose, InterIsHowFarIntoTheStateTheActorIs)
{
	EXPECT_EQ(ComputeModelInter(4, 4, 0.f, false, false), 0.);
	EXPECT_DOUBLE_EQ(ComputeModelInter(4, 2, 0.5f, false, false), 0.625);
	EXPECT_EQ(ComputeModelInter(4, 5, 0.5f, false, false), 0.);   // [rc4l] longer than its state: no blend
	EXPECT_DOUBLE_EQ(ComputeModelInter(4, 2, 0.f, true, false), 0.75);
	EXPECT_DOUBLE_EQ(ComputeModelInter(4, 2, 0.f, false, true), 0.25);
	EXPECT_DOUBLE_EQ(ComputeModelInter(4, 2, 0.f, true, true), 0.375);
}

TEST(ComputeHUDModelMatrix, ScalesOffsetsThenTurnsAQuarterAboutUp)
{
	zx::sprites::HUDModelPose p = { { 2.f, 3.f, 4.f }, { 1.f, 0.f, 0.f }, 0.f, 0.f, 0.f, 0.f, 0.f };
	float m[16];
	zx::sprites::ComputeHUDModelMatrix(p, m);
	// [rc4l] The offset lands scaled; the quarter turn takes the model's x onto -z, its z onto x.
	EXPECT_NEAR(2.f, m[12], 1e-5f);
	EXPECT_NEAR(0.f, m[0], 1e-5f);
	EXPECT_NEAR(-4.f, m[2], 1e-5f);
	EXPECT_NEAR(2.f, m[8], 1e-5f);
	EXPECT_NEAR(3.f, m[5], 1e-5f);
	// [rc4l] The model's own turn about up cancels the quarter turn.
	p.angleOffset = 90.f;
	zx::sprites::ComputeHUDModelMatrix(p, m);
	EXPECT_NEAR(2.f, m[0], 1e-5f);
	EXPECT_NEAR(0.f, m[2], 1e-5f);
	// [rc4l] The bob turns it a quarter as many degrees as pixels: 360 across adds a quarter turn about up, 360 down tips up onto -z.
	p.angleOffset = 0.f;
	p.bobX = 360.f;
	zx::sprites::ComputeHUDModelMatrix(p, m);
	EXPECT_NEAR(-2.f, m[0], 1e-4f);
	p.bobX = 0.f;
	p.bobY = 360.f;
	zx::sprites::ComputeHUDModelMatrix(p, m);
	EXPECT_NEAR(0.f, m[5], 1e-4f);
	EXPECT_NEAR(-4.f, m[6], 1e-4f);
}

// [rc4l] GL blended a slot only for a model that interpolates, and the derived models blended every one.
TEST(ModelPose, ASlotBlendsOnlyBetweenDifferentFramesOfAModelThatInterpolates)
{
	EXPECT_FLOAT_EQ(0.25f, ComputeModelSlotBlend(true, true, 0.25));
	EXPECT_FLOAT_EQ(0.f, ComputeModelSlotBlend(false, true, 0.25));
	EXPECT_FLOAT_EQ(0.f, ComputeModelSlotBlend(true, false, 0.25));
}
