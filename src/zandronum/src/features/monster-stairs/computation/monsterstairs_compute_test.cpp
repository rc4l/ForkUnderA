// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/monster-stairs/computation/monsterstairs_compute.h"

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

using namespace zx::monsterstairs;

namespace {
const int64_t UNIT = 65536;   // [Doruk-Aksoy] One map unit in 48.16 fixed point.
const int64_t DROP = 24 * UNIT; // [Doruk-Aksoy] The default MaxDropOffHeight.
}

// [Doruk-Aksoy] Either boolean value of a key is a choice, never Unset.
TEST(MonsterStairs, AKeyAlwaysChooses)
{
	EXPECT_EQ(Setting::On, ComputeSettingFromBool(true));
	EXPECT_EQ(Setting::Off, ComputeSettingFromBool(false));
	EXPECT_EQ(0, static_cast<int>(Setting::Unset));
}

// [Doruk-Aksoy] The map's own choice wins over GAMEINFO and the cvar, whichever way each points.
TEST(MonsterStairs, TheMapDecidesFirst)
{
	for (Setting game : { Setting::Unset, Setting::On, Setting::Off })
		for (bool cvar : { false, true })
		{
			EXPECT_TRUE(ComputeMonsterStairsEnabled(Setting::On, game, cvar));
			EXPECT_FALSE(ComputeMonsterStairsEnabled(Setting::Off, game, cvar));
		}
}

// [Doruk-Aksoy] With the map silent, GAMEINFO decides over the cvar.
TEST(MonsterStairs, GameInfoDecidesForASilentMap)
{
	EXPECT_TRUE(ComputeMonsterStairsEnabled(Setting::Unset, Setting::On, false));
	EXPECT_FALSE(ComputeMonsterStairsEnabled(Setting::Unset, Setting::Off, true));
}

// [Doruk-Aksoy] With both silent, the server's cvar decides.
TEST(MonsterStairs, TheCvarDecidesLast)
{
	EXPECT_TRUE(ComputeMonsterStairsEnabled(Setting::Unset, Setting::Unset, true));
	EXPECT_FALSE(ComputeMonsterStairsEnabled(Setting::Unset, Setting::Unset, false));
}

// [Doruk-Aksoy] A line's drop is the gap between its floors in either order, so the slope fudge that can hand the lower one back as `bottom` still measures it.
TEST(MonsterStairs, ALineDropIsTheGapBetweenItsFloors)
{
	EXPECT_EQ(16 * UNIT, ComputeLineFloorDrop(32 * UNIT, 16 * UNIT));
	EXPECT_EQ(16 * UNIT, ComputeLineFloorDrop(16 * UNIT, 32 * UNIT));
	EXPECT_EQ(0, ComputeLineFloorDrop(8 * UNIT, 8 * UNIT));
	EXPECT_EQ(40 * UNIT, ComputeLineFloorDrop(-8 * UNIT, -48 * UNIT));
}

// [Doruk-Aksoy] An opening left unbounded on either side measures nothing rather than overflowing.
TEST(MonsterStairs, AnUnboundedSideMeasuresNothing)
{
	const int64_t lo = std::numeric_limits<int64_t>::min(), hi = std::numeric_limits<int64_t>::max();
	EXPECT_EQ(0, ComputeLineFloorDrop(lo, 0));
	EXPECT_EQ(0, ComputeLineFloorDrop(hi, 0));
	EXPECT_EQ(0, ComputeLineFloorDrop(0, lo));
	EXPECT_EQ(0, ComputeLineFloorDrop(0, hi));
}

// [Doruk-Aksoy] Off, the box's whole spread decides as Doom decided: three 16-high steps under an Imp read as a 32-unit cliff.
TEST(MonsterStairs, OffTheSpreadDecides)
{
	EXPECT_TRUE(ComputeDropoffBlocked(32 * UNIT, 0, 16 * UNIT, DROP, false));
	EXPECT_FALSE(ComputeDropoffBlocked(24 * UNIT, 0, 8 * UNIT, DROP, false));
}

// [Doruk-Aksoy] On, the same three steps pass, since no single line drops more than 16.
TEST(MonsterStairs, OnNarrowStepsPass)
{
	EXPECT_FALSE(ComputeDropoffBlocked(32 * UNIT, 0, 16 * UNIT, DROP, true));
	EXPECT_FALSE(ComputeDropoffBlocked(96 * UNIT, 0, DROP, DROP, true));
}

// [Doruk-Aksoy] On, a real ledge still blocks: one line drops past MaxDropOffHeight.
TEST(MonsterStairs, OnARealLedgeStillBlocks)
{
	EXPECT_TRUE(ComputeDropoffBlocked(64 * UNIT, 0, 64 * UNIT, DROP, true));
	EXPECT_TRUE(ComputeDropoffBlocked(64 * UNIT, 0, DROP + 1, DROP, true));
}

// [Doruk-Aksoy] A spawn or teleport reads only the floor at the centre, so an Arachnotron placed on 16-deep steps sat 64 below the highest one under it and every move was too big a step up.
TEST(MonsterStairs, ASunkWalkerIsLiftedOntoStairsItCouldClimb)
{
	EXPECT_TRUE(ComputeStairLift(-112 * UNIT, -48 * UNIT, 16 * UNIT, DROP));
	EXPECT_TRUE(ComputeStairLift(0, 1, DROP, DROP));
}

// [Doruk-Aksoy] Not onto a ledge it could not have climbed, and not when it already stands on the highest floor.
TEST(MonsterStairs, NoLiftOntoALedgeOrWhenStandingOnTop)
{
	EXPECT_FALSE(ComputeStairLift(0, 64 * UNIT, 64 * UNIT, DROP));
	EXPECT_FALSE(ComputeStairLift(0, 48 * UNIT, DROP + 1, DROP));
	EXPECT_FALSE(ComputeStairLift(-48 * UNIT, -48 * UNIT, 16 * UNIT, DROP));
	EXPECT_FALSE(ComputeStairLift(0, -16 * UNIT, 16 * UNIT, DROP));
}

// [Doruk-Aksoy] On never blocks more than off: a spread the caller already narrowed (a 3D-midtex bridge zeroes it) wins over a larger line drop.
TEST(MonsterStairs, OnNeverBlocksMoreThanOff)
{
	EXPECT_FALSE(ComputeDropoffBlocked(64 * UNIT, 64 * UNIT, 64 * UNIT, DROP, true));
	EXPECT_FALSE(ComputeDropoffBlocked(20 * UNIT, 0, 40 * UNIT, DROP, true));
}
