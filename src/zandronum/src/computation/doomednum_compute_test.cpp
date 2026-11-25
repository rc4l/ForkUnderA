// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO

#include <gtest/gtest.h>

#include "computation/doomednum_compute.h"

using zx::ComputeDecorateEdNumClaim;
using zx::DecorateEdNumClaim;

TEST(DoomEdNum, AFreeNumberIsTaken)
{
	EXPECT_EQ(ComputeDecorateEdNumClaim(false, false, 0), DecorateEdNumClaim::Take);
}

TEST(DoomEdNum, AMapinfoClassEntryIsTaken)
{
	// [MGOOOOOO] `11 = "Foo"` in a mod's MAPINFO, and the same with an action special, both yield to DECORATE as before.
	EXPECT_EQ(ComputeDecorateEdNumClaim(true, true, 0), DecorateEdNumClaim::Take);
	EXPECT_EQ(ComputeDecorateEdNumClaim(true, true, 80), DecorateEdNumClaim::Take);
}

TEST(DoomEdNum, ASecondDecorateClaimIsTakenWithTheWarning)
{
	EXPECT_EQ(ComputeDecorateEdNumClaim(true, true, -2), DecorateEdNumClaim::TakeAndWarnTwice);
}

TEST(DoomEdNum, ASpecialMapthingKeepsItsNumber)
{
	// [MGOOOOOO] MM8BDM's `actor DeathmatchStart 11` and `actor Player1Start : DeathmatchStart 1` took every start of its maps.
	EXPECT_EQ(ComputeDecorateEdNumClaim(true, false, 1), DecorateEdNumClaim::KeepSpecial);
	EXPECT_EQ(ComputeDecorateEdNumClaim(true, false, 9), DecorateEdNumClaim::KeepSpecial);
	EXPECT_EQ(ComputeDecorateEdNumClaim(true, false, 25), DecorateEdNumClaim::KeepSpecial);
}

TEST(DoomEdNum, AnEntryWithNeitherClassNorSpecialMapthingIsTaken)
{
	EXPECT_EQ(ComputeDecorateEdNumClaim(true, false, 0), DecorateEdNumClaim::Take);
	EXPECT_EQ(ComputeDecorateEdNumClaim(true, false, -1), DecorateEdNumClaim::Take);
}
