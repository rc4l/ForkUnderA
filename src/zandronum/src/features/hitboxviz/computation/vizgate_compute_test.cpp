// [MGOOOOOO] Tests for the debug overlay's cheat gate and line-width clamp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#include "gtest/gtest.h"

#include "features/hitboxviz/computation/vizgate_compute.h"

using namespace zx::hitboxviz;

// ---- ShouldDraw ------------------------------------------------------------
//
// The whole truth table over (cvarEnabled, svCheats, offlineGame). sv_cheats is not the only way
// cheats become permitted: offline there is no server whose rules could be subverted and no other
// player to affect, which is why iddqd works there regardless of sv_cheats. The overlay follows the
// same rule -- offline it obeys the toggle alone; online sv_cheats is the sole authority.

TEST(HitboxVizGate, DrawsWhenEnabledAndCheatsOn)
{
	EXPECT_TRUE(ShouldDraw(/*cvarEnabled=*/true, /*svCheats=*/true, /*offlineGame=*/false));
}

TEST(HitboxVizGate, DrawsInOfflineSinglePlayerWithCheatsOff)
{
	// The point of the whole gate: a local test map is exactly where this overlay is most useful,
	// and sv_cheats is latched there -- demanding it meant a map change before anything appeared.
	EXPECT_TRUE(ShouldDraw(/*cvarEnabled=*/true, /*svCheats=*/false, /*offlineGame=*/true));
}

TEST(HitboxVizGate, DoesNotDrawWhenCheatsAreOffOnline)
{
	// Joined a server with sv_cheats 0: the toggle stays set, but nothing is drawn -- so
	// cl_fua_hitbox_xray cannot become a wallhack in someone else's game.
	EXPECT_FALSE(ShouldDraw(/*cvarEnabled=*/true, /*svCheats=*/false, /*offlineGame=*/false));
}

TEST(HitboxVizGate, DoesNotDrawWhenDisabled)
{
	// The toggle is not overridden by either permission route.
	EXPECT_FALSE(ShouldDraw(/*cvarEnabled=*/false, /*svCheats=*/true,  /*offlineGame=*/false));
	EXPECT_FALSE(ShouldDraw(/*cvarEnabled=*/false, /*svCheats=*/false, /*offlineGame=*/true));
	EXPECT_FALSE(ShouldDraw(/*cvarEnabled=*/false, /*svCheats=*/true,  /*offlineGame=*/true));
}

TEST(HitboxVizGate, DoesNotDrawWhenDisabledAndCheatsOff)
{
	EXPECT_FALSE(ShouldDraw(/*cvarEnabled=*/false, /*svCheats=*/false, /*offlineGame=*/false));
}

TEST(HitboxVizGate, OfflineIsEnoughRegardlessOfCheats)
{
	// Offline, sv_cheats is not consulted at all -- it is not the authority there.
	for (int cheats = 0; cheats < 2; ++cheats)
		EXPECT_TRUE(ShouldDraw(/*cvarEnabled=*/true, cheats != 0, /*offlineGame=*/true));
}

TEST(HitboxVizGate, OnlineCheatsOffAlwaysWins)
{
	// Restated as an invariant: with someone else's game to protect, nothing draws without cheats.
	for (int enabled = 0; enabled < 2; ++enabled)
		EXPECT_FALSE(ShouldDraw(enabled != 0, /*svCheats=*/false, /*offlineGame=*/false));
}

TEST(HitboxVizGate, TheToggleIsNeverBypassed)
{
	// Exhaustive over the remaining input space: cvarEnabled is a hard prerequisite.
	for (int cheats = 0; cheats < 2; ++cheats)
		for (int offline = 0; offline < 2; ++offline)
			EXPECT_FALSE(ShouldDraw(/*cvarEnabled=*/false, cheats != 0, offline != 0));
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
