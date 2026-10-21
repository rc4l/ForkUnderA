// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/tracedsprite_compute.h"

#include <gtest/gtest.h>

using namespace zx::hwrender;

namespace {

// [rc4l] A 32 wide, 64 tall sprite 100 north of the origin facing it, u left to right and v top to bottom.
TracedSprite Sample()
{
	const float tl[5] = { -16.f, 64.f, 100.f, 0.f, 0.f }, tr[5] = { 16.f, 64.f, 100.f, 1.f, 0.f }, bl[5] = { -16.f, 0.f, 100.f, 0.f, 1.f };
	TracedSprite s;
	EXPECT_TRUE(ComputeTracedSprite(tl, tr, bl, 7u, 3, s));
	return s;
}

} // namespace

TEST(TracedSprite, AQuadBecomesItsCornerEdgesAndTexture)
{
	const TracedSprite s = Sample();
	EXPECT_FLOAT_EQ(-16.f, s.origin[0]); EXPECT_FLOAT_EQ(64.f, s.origin[1]); EXPECT_FLOAT_EQ(100.f, s.origin[2]);
	EXPECT_FLOAT_EQ(32.f, s.edgeU[0]); EXPECT_FLOAT_EQ(0.f, s.edgeU[1]);
	EXPECT_FLOAT_EQ(0.f, s.edgeV[0]); EXPECT_FLOAT_EQ(-64.f, s.edgeV[1]);
	EXPECT_FLOAT_EQ(0.f, s.u0); EXPECT_FLOAT_EQ(1.f, s.u1); EXPECT_FLOAT_EQ(0.f, s.v0); EXPECT_FLOAT_EQ(1.f, s.v1);
	EXPECT_EQ(7u, s.piece); EXPECT_EQ(3, s.view);
	float mn[3], mx[3];
	ComputeTracedSpriteBox(s, mn, mx);
	EXPECT_FLOAT_EQ(-16.f, mn[0]); EXPECT_FLOAT_EQ(0.f, mn[1]); EXPECT_FLOAT_EQ(100.f, mn[2]);
	EXPECT_FLOAT_EQ(16.f, mx[0]); EXPECT_FLOAT_EQ(64.f, mx[1]); EXPECT_FLOAT_EQ(100.f, mx[2]);
	float r[kTracedSpriteFloats];
	ComputeTracedSpriteRecord(s, r);
	const float want[kTracedSpriteFloats] = { -16, 64, 100, 7, 32, 0, 0, 3, 0, -64, 0, 0, 0, 1, 0, 1 };
	for (int i = 0; i < kTracedSpriteFloats; i++) EXPECT_FLOAT_EQ(want[i], r[i]) << i;
}

TEST(TracedSprite, AQuadWithNoAreaIsNone)
{
	const float a[5] = { 0.f, 64.f, 100.f, 0.f, 0.f }, b[5] = { 0.f, 0.f, 100.f, 0.f, 1.f }, c[5] = { 0.f, 32.f, 100.f, 1.f, 1.f };
	TracedSprite s;
	EXPECT_FALSE(ComputeTracedSprite(a, a, b, 0u, 0, s));   // [rc4l] no width
	EXPECT_FALSE(ComputeTracedSprite(a, b, c, 0u, 0, s));   // [rc4l] three corners on one line
}

// [rc4l] Only an opaque sprite quad a mirror's walk took: the player's view has its own pass, and a ray cannot blend.
TEST(TracedSprite, OnlyAnOpaqueSpriteAMirrorsWalkTookIsTraced)
{
	EXPECT_TRUE(ComputeTracesRun(6, false, false, false, -1, -1, 0, 0));
	EXPECT_FALSE(ComputeTracesRun(36, false, false, false, -1, -1, 0, 0));   // [rc4l] a model
	EXPECT_FALSE(ComputeTracesRun(6, true, false, false, -1, -1, 0, 0));
	EXPECT_FALSE(ComputeTracesRun(6, false, true, false, -1, -1, 0, 0));    // [rc4l] a decal
	EXPECT_FALSE(ComputeTracesRun(6, false, false, true, -1, -1, 0, 0));
	EXPECT_FALSE(ComputeTracesRun(6, false, false, false, 2, -1, 0, 0));    // [rc4l] a stacked view's
	EXPECT_FALSE(ComputeTracesRun(6, false, false, false, -1, 4, 0, 0));    // [rc4l] on a mirror's face
	EXPECT_FALSE(ComputeTracesRun(6, false, false, false, -1, -1, -1, 0));  // [rc4l] the player's view's
	EXPECT_FALSE(ComputeTracesRun(6, false, false, false, -1, -1, 0, 1));   // [rc4l] translucent
}

TEST(TracedSprite, TheHeaderCountsTheStepsAndHoldsOneAVec4)
{
	EXPECT_EQ(4u, ComputeTracedSpriteHeaderFloats(0));
	EXPECT_EQ(12u, ComputeTracedSpriteHeaderFloats(2));
	const int steps[6] = { 10, 20, 9000, 9000, 10, 9001 };
	float out[12];
	for (int i = 0; i < 12; i++) out[i] = -5.f;
	ComputeTracedSpriteHeader(steps, 2, out);
	const float want[12] = { 2, 0, 0, 0, 10, 20, 9000, 0, 9000, 10, 9001, 0 };
	for (int i = 0; i < 12; i++) EXPECT_FLOAT_EQ(want[i], out[i]) << i;
}

// [rc4l] Square on through its middle, to the right of the middle where u runs on toward u1, and from behind, since a mirror can show either face.
TEST(TracedSprite, ARayMeetsTheQuadFromEitherSide)
{
	const TracedSprite s = Sample();
	const float o[3] = { 0.f, 32.f, 0.f }, ahead[3] = { 0.f, 0.f, 1.f }, right[3] = { 8.f, 0.f, 100.f };
	float t, u, v;
	ASSERT_TRUE(ComputeTracedSpriteHit(s, o, ahead, 0.1f, 1000.f, t, u, v));
	EXPECT_FLOAT_EQ(100.f, t); EXPECT_FLOAT_EQ(0.5f, u); EXPECT_FLOAT_EQ(0.5f, v);
	ASSERT_TRUE(ComputeTracedSpriteHit(s, o, right, 0.f, 1000.f, t, u, v));
	EXPECT_FLOAT_EQ(1.f, t); EXPECT_FLOAT_EQ(0.75f, u);
	const float behind[3] = { 0.f, 48.f, 200.f }, back[3] = { 0.f, 0.f, -1.f };
	ASSERT_TRUE(ComputeTracedSpriteHit(s, behind, back, 0.1f, 1000.f, t, u, v));
	EXPECT_FLOAT_EQ(100.f, t); EXPECT_FLOAT_EQ(0.5f, u); EXPECT_FLOAT_EQ(0.25f, v);
}

// [rc4l] A quad leaning over, as a sprite tilted to the eye is, maps along its own edges rather than across the screen.
TEST(TracedSprite, ALeaningQuadMapsAlongItsEdges)
{
	const float tl[5] = { -16.f, 64.f, 100.f, 0.f, 0.f }, tr[5] = { 16.f, 64.f, 100.f, 1.f, 0.f }, bl[5] = { 0.f, 0.f, 100.f, 0.f, 1.f };
	TracedSprite s;
	ASSERT_TRUE(ComputeTracedSprite(tl, tr, bl, 0u, 0, s));
	const float o[3] = { 0.f, 32.f, 0.f }, ahead[3] = { 0.f, 0.f, 1.f };
	float t, u, v;
	ASSERT_TRUE(ComputeTracedSpriteHit(s, o, ahead, 0.f, 1000.f, t, u, v));
	EXPECT_FLOAT_EQ(0.25f, u); EXPECT_FLOAT_EQ(0.5f, v);
}

TEST(TracedSprite, ARayPastItOrOutOfRangeMisses)
{
	const TracedSprite s = Sample();
	float t, u, v;
	const float o[3] = { 0.f, 32.f, 0.f };
	const float left[3] = { -20.f, 0.f, 100.f }, wide[3] = { 20.f, 0.f, 100.f }, high[3] = { 0.f, 40.f, 100.f }, low[3] = { 0.f, -40.f, 100.f };
	EXPECT_FALSE(ComputeTracedSpriteHit(s, o, left, 0.f, 1000.f, t, u, v));
	EXPECT_FALSE(ComputeTracedSpriteHit(s, o, wide, 0.f, 1000.f, t, u, v));
	EXPECT_FALSE(ComputeTracedSpriteHit(s, o, high, 0.f, 1000.f, t, u, v));
	EXPECT_FALSE(ComputeTracedSpriteHit(s, o, low, 0.f, 1000.f, t, u, v));
	const float along[3] = { 1.f, 0.f, 0.f };
	EXPECT_FALSE(ComputeTracedSpriteHit(s, o, along, 0.f, 1000.f, t, u, v));   // [rc4l] parallel to its face
	const float ahead[3] = { 0.f, 0.f, 1.f };
	EXPECT_FALSE(ComputeTracedSpriteHit(s, o, ahead, 0.1f, 50.f, t, u, v));    // [rc4l] past the nearer hit already found
	EXPECT_FALSE(ComputeTracedSpriteHit(s, o, ahead, 150.f, 1000.f, t, u, v));
}
