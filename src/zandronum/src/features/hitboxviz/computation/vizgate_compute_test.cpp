// [MGOOOOOO] Tests for the debug overlay's cheat gate and line-width clamp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#include "gtest/gtest.h"

#include "features/hitboxviz/computation/vizgate_compute.h"

using namespace zx::hitboxviz;

// ---- ShouldDraw ------------------------------------------------------------
//
// The whole truth table. The gate takes sv_cheats directly rather than CheckCheatmode(), whose
// contract is "are cheats permitted here" -- in single-player that is yes regardless of sv_cheats,
// which is why iddqd works offline. Wiring the overlay to it made boxes appear in single-player
// with cheats off; these cases pin the stricter rule.

TEST(HitboxVizGate, DrawsOnlyWhenEnabledAndCheatsOn)
{
	EXPECT_TRUE(ShouldDraw(/*cvarEnabled=*/true, /*svCheats=*/true));
}

TEST(HitboxVizGate, DoesNotDrawWhenCheatsAreOff)
{
	// Joined a server with sv_cheats 0: the toggle stays set, but nothing is drawn.
	EXPECT_FALSE(ShouldDraw(/*cvarEnabled=*/true, /*svCheats=*/false));
}

TEST(HitboxVizGate, DoesNotDrawInSinglePlayerWithCheatsOff)
{
	// Regression: single-player is exactly where CheckCheatmode() would have said "permitted".
	// There is no game-mode input any more -- sv_cheats false means no drawing, everywhere.
	EXPECT_FALSE(ShouldDraw(/*cvarEnabled=*/true, /*svCheats=*/false));
}

TEST(HitboxVizGate, DoesNotDrawWhenDisabled)
{
	EXPECT_FALSE(ShouldDraw(/*cvarEnabled=*/false, /*svCheats=*/true));
}

TEST(HitboxVizGate, DoesNotDrawWhenDisabledAndCheatsOff)
{
	EXPECT_FALSE(ShouldDraw(/*cvarEnabled=*/false, /*svCheats=*/false));
}

TEST(HitboxVizGate, CheatsOffAlwaysWins)
{
	// Restated as an invariant: no combination draws while cheats are off.
	for (int enabled = 0; enabled < 2; ++enabled)
		EXPECT_FALSE(ShouldDraw(enabled != 0, /*svCheats=*/false));
}

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
