// [MGOOOOOO] Tests for the debug overlay's line-width clamp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#include "gtest/gtest.h"

#include "features/hitboxviz/computation/vizgate_compute.h"

using namespace zx::hitboxviz;

// ---- ResolveLineWidth ------------------------------------------------------

TEST(HitboxVizLineWidth, PassesThroughWidthsInsideTheDriverRange)
{
	EXPECT_FLOAT_EQ(3.f, ResolveLineWidth(3.f, 1.f, 10.f));
	EXPECT_FLOAT_EQ(1.f, ResolveLineWidth(1.f, 1.f, 10.f));
	EXPECT_FLOAT_EQ(10.f, ResolveLineWidth(10.f, 1.f, 10.f));
}

TEST(HitboxVizLineWidth, ClampsAboveTheDriverMaximum)
{
	// An out-of-range glLineWidth raises GL_INVALID_VALUE and is ignored, which would silently
	// leave the overlay at whatever width was last set.
	EXPECT_FLOAT_EQ(10.f, ResolveLineWidth(64.f, 1.f, 10.f));
}

TEST(HitboxVizLineWidth, ClampsBelowTheDriverMinimum)
{
	EXPECT_FLOAT_EQ(1.f, ResolveLineWidth(0.f, 1.f, 10.f));
	EXPECT_FLOAT_EQ(1.f, ResolveLineWidth(-5.f, 1.f, 10.f));
}

TEST(HitboxVizLineWidth, CoreProfileOnlySupportsHairlines)
{
	// A core profile may report a range of exactly [1, 1]; every request collapses to 1.
	EXPECT_FLOAT_EQ(1.f, ResolveLineWidth(1.f, 1.f, 1.f));
	EXPECT_FLOAT_EQ(1.f, ResolveLineWidth(8.f, 1.f, 1.f));
	EXPECT_FLOAT_EQ(1.f, ResolveLineWidth(0.5f, 1.f, 1.f));
}

TEST(HitboxVizLineWidth, InvertedRangeFallsBackToTheMinimum)
{
	// Defensive: a driver reporting nonsense must not produce a width we then pass to GL.
	EXPECT_FLOAT_EQ(4.f, ResolveLineWidth(8.f, 4.f, 2.f));
	EXPECT_FLOAT_EQ(4.f, ResolveLineWidth(1.f, 4.f, 2.f));
}

TEST(HitboxVizLineWidth, SupportsFractionalRanges)
{
	EXPECT_FLOAT_EQ(0.5f, ResolveLineWidth(0.25f, 0.5f, 7.5f));
	EXPECT_FLOAT_EQ(7.5f, ResolveLineWidth(9.f, 0.5f, 7.5f));
	EXPECT_FLOAT_EQ(2.5f, ResolveLineWidth(2.5f, 0.5f, 7.5f));
}
