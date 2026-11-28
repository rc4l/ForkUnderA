// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "features/acs-parity/computation/acsparity_compute.h"

using namespace zx::acsparity;

namespace {

constexpr int32_t kOne = 0x10000;

// [Doruk-Aksoy] One candidate of a CheckProximity scan, standing at `x`, `y` on the floor with height 56.
struct Candidate
{
	double x, y, z;
	bool killed;
	bool visible;
};

struct Scanned
{
	int result;
	const Candidate* chosen;
	int sightAsks;
};

Scanned Scan(const std::vector<Candidate>& all, double distance, int count, int flags, bool counting = false)
{
	size_t at = 0;
	int asks = 0;
	const ProximityPoint ref = { 0, 0, 0, 56 };
	const auto next = [&](const Candidate*& handle, ProximityPoint& point, bool& killed) {
		if (at == all.size())
			return false;
		handle = &all[at++];
		point = { handle->x, handle->y, handle->z, handle->z + 56 };
		killed = handle->killed;
		return true;
	};
	const auto sight = [&](const Candidate* c) { ++asks; return c->visible; };
	const ProximityScan<const Candidate*> scan = ComputeProximityScan<const Candidate*>(next, sight, ref, distance, count, flags, counting);
	return { scan.result, scan.chosen, asks };
}

} // namespace

// [Doruk-Aksoy] Counting, as CountProximity, scans past count and answers how many counted.
TEST(AcsParity, ProximityCountingAnswersHowManyCounted)
{
	const std::vector<Candidate> all = { { 10, 0, 0, false, true }, { 20, 0, 0, false, true }, { 30, 0, 0, true, true },
		{ 500, 0, 0, false, true } };
	EXPECT_EQ(Scan(all, 100, 0, 0, true).result, 2);
	EXPECT_EQ(Scan(all, 100, 0, kCpxCountDead, true).result, 3);
	EXPECT_EQ(Scan(all, 0, 0, 0, true).result, 0);
	EXPECT_EQ(Scan(all, 100, 0, 0, false).result, 1);
}

// [Doruk-Aksoy] Compiled mods hold UZDoom's numbers (src/playsim/p_acs.cpp, EACSFunctions).
TEST(AcsParity, FunctionNumbersAreUZDooms)
{
	EXPECT_EQ(kGetMaxInventory, 93);
	EXPECT_EQ(kCheckProximity, 98);
	EXPECT_EQ(kCheckActorState, 99);
	EXPECT_EQ(kCheckClass, 200);
	EXPECT_EQ(kDamageActor, 201);
	EXPECT_EQ(kGetActorFloorTerrain, 205);
	EXPECT_EQ(kStrArg, 206);
	EXPECT_EQ(kFloor, 207);
	EXPECT_EQ(kRound, 208);
	EXPECT_EQ(kCeil, 209);
	EXPECT_EQ(kGetNetID, 215);
	EXPECT_EQ(kSetActivatorByNetID, 216);
	EXPECT_EQ(kGetLineX, 300);
	EXPECT_EQ(kGetLineY, 301);
	EXPECT_EQ(kSetActorFlag, 202);
	EXPECT_EQ(kSetTranslation, 203);
	EXPECT_EQ(kSetTeamScore, 19621);
	EXPECT_EQ(kAPropFriendlySeeBlocks, 48);
	EXPECT_EQ(kAPropWaterDepth, 49);
	EXPECT_EQ(kCpxSetOnPtr, 0x800);
	EXPECT_EQ(kCpxCheckSight, 0x1000);
}

// [Doruk-Aksoy] UZDoom's ACSF_Floor: args[0] & ~0xffff.
TEST(AcsParity, FloorDropsTheFraction)
{
	EXPECT_EQ(ComputeFixedFloor(kOne + kOne / 2), kOne);
	EXPECT_EQ(ComputeFixedFloor(2 * kOne), 2 * kOne);
	EXPECT_EQ(ComputeFixedFloor(-(kOne + kOne / 2)), -2 * kOne);
	EXPECT_EQ(ComputeFixedFloor(INT32_MAX), 0x7FFF0000);
}

// [Doruk-Aksoy] UZDoom's ACSF_Round: (args[0] + 32768) & ~0xffff, halves up and wrapping at the top.
TEST(AcsParity, RoundGoesToTheNearestWholeNumber)
{
	EXPECT_EQ(ComputeFixedRound(kOne + kOne / 2), 2 * kOne);
	EXPECT_EQ(ComputeFixedRound(kOne + kOne / 2 - 1), kOne);
	EXPECT_EQ(ComputeFixedRound(-(kOne + kOne / 2)), -kOne);
	EXPECT_EQ(ComputeFixedRound(INT32_MAX), INT32_MIN);
}

// [Doruk-Aksoy] UZDoom's ACSF_Ceil is (args[0] & ~0xffff) + 0x10000, which takes 2.0 to 3.0; ours keeps a whole number.
TEST(AcsParity, CeilRoundsUpAndKeepsWholeNumbers)
{
	EXPECT_EQ(ComputeFixedCeil(kOne + kOne / 4), 2 * kOne);
	EXPECT_EQ(ComputeFixedCeil(2 * kOne), 2 * kOne);
	EXPECT_EQ(ComputeFixedCeil(0), 0);
	EXPECT_EQ(ComputeFixedCeil(-(kOne + kOne / 2)), -kOne);
	EXPECT_EQ(ComputeFixedCeil(1), kOne);
	EXPECT_EQ(ComputeFixedCeil(INT32_MAX), INT32_MIN);
}

// [Doruk-Aksoy] UZDoom's DoubleToACS is xs_Fix<16>::ToFix, which rounds half to even.
TEST(AcsParity, FixedFromDoubleRoundsHalfToEven)
{
	EXPECT_EQ(ComputeFixedFromDouble(64), 64 * kOne);
	EXPECT_EQ(ComputeFixedFromDouble(-0.5), -kOne / 2);
	EXPECT_EQ(ComputeFixedFromDouble(2.5 / 65536.0), 2);
	EXPECT_EQ(ComputeFixedFromDouble(3.5 / 65536.0), 4);
	EXPECT_EQ(ComputeFixedFromDouble(40000), INT32_MAX);
	EXPECT_EQ(ComputeFixedFromDouble(-40000), INT32_MIN);
	EXPECT_EQ(ComputeFixedFromDouble(std::nan("")), INT32_MIN);
}

// [Doruk-Aksoy] UZDoom's ACSF_GetLineX/Y: v1 + delta * along, plus the unit normal (delta.Y, -delta.X) times offset.
TEST(AcsParity, LineCoordWalksTheLineAndItsNormal)
{
	EXPECT_DOUBLE_EQ(ComputeLineCoord(0, 0, 0, 128, 0, 0.5, 0), 64);
	EXPECT_DOUBLE_EQ(ComputeLineCoord(1, 0, 0, 128, 0, 0.5, 8), -8);
	EXPECT_DOUBLE_EQ(ComputeLineCoord(0, 64, 64, 0, 128, 0.25, 8), 72);
	EXPECT_DOUBLE_EQ(ComputeLineCoord(1, 64, 64, 0, 128, 0.25, 8), 96);
	EXPECT_DOUBLE_EQ(ComputeLineCoord(0, 0, 0, 30, 40, 1, 5), 34);
	EXPECT_DOUBLE_EQ(ComputeLineCoord(1, 0, 0, 30, 40, 1, 5), 37);
}

// [Doruk-Aksoy] A line of no length has no normal, as UZDoom's DVector2::Unit of a zero vector is zero.
TEST(AcsParity, LineCoordOfAPointIgnoresTheOffset)
{
	EXPECT_DOUBLE_EQ(ComputeLineCoord(0, 16, 32, 0, 0, 0.5, 8), 16);
	EXPECT_DOUBLE_EQ(ComputeLineCoord(1, 16, 32, 0, 0, 0.5, 8), 32);
}

TEST(AcsParity, NetIdsAreUnsignedShortsAboveZero)
{
	EXPECT_FALSE(ComputeNetIdValid(0));
	EXPECT_TRUE(ComputeNetIdValid(1));
	EXPECT_TRUE(ComputeNetIdValid(0xFFFF));
	EXPECT_FALSE(ComputeNetIdValid(0x10000));
	EXPECT_FALSE(ComputeNetIdValid(-1));
}

// [Doruk-Aksoy] A CLIENTSIDE script's DamageActor hurts only the client's own actors, the server holding every synced actor's health.
TEST(AcsParity, ClientDamagesOnlyItsOwnActors)
{
	EXPECT_TRUE(ComputeClientMayDamage(false, false));
	EXPECT_TRUE(ComputeClientMayDamage(false, true));
	EXPECT_TRUE(ComputeClientMayDamage(true, true));
	EXPECT_FALSE(ComputeClientMayDamage(true, false));
}

// [Doruk-Aksoy] UZDoom's SelectActorFromTID: the index'th actor of a tid, none past the last.
TEST(AcsParity, NthPicksByIndex)
{
	const int items[] = { 10, 20, 30 };
	const auto from = [&items]() {
		size_t at = 0;
		return [&items, at]() mutable -> const int* { return at < 3 ? &items[at++] : nullptr; };
	};
	EXPECT_EQ(ComputeNth(from(), 0), &items[0]);
	EXPECT_EQ(ComputeNth(from(), 2), &items[2]);
	EXPECT_EQ(ComputeNth(from(), 3), nullptr);
	EXPECT_EQ(ComputeNth(from(), -1), nullptr);
}

// [Doruk-Aksoy] UZDoom's P_Thing_CheckProximity range test, from src/playsim/p_things.cpp.
TEST(AcsParity, ProximityRangeIsGroundThenBodies)
{
	const ProximityPoint ref = { 0, 0, 0, 56 };
	EXPECT_TRUE(ComputeProximityInRange(ref, { 30, 40, 0, 56 }, 51, 0));
	EXPECT_FALSE(ComputeProximityInRange(ref, { 30, 40, 0, 56 }, 50, 0));
	EXPECT_TRUE(ComputeProximityInRange(ref, { 0, 10, 100, 156 }, 50, 0));
	EXPECT_FALSE(ComputeProximityInRange(ref, { 0, 10, 106, 162 }, 50, 0));
	EXPECT_TRUE(ComputeProximityInRange(ref, { 0, 10, 106, 162 }, 50, kCpxNoZ));
	EXPECT_TRUE(ComputeProximityInRange({ 0, 0, 100, 156 }, { 0, 10, 0, 56 }, 50, 0));
	EXPECT_FALSE(ComputeProximityInRange({ 0, 0, 107, 163 }, { 0, 10, 0, 56 }, 50, 0));
}

TEST(AcsParity, ProximityLifeFlags)
{
	EXPECT_TRUE(ComputeProximityLifeCounts(false, 0));
	EXPECT_FALSE(ComputeProximityLifeCounts(true, 0));
	EXPECT_TRUE(ComputeProximityLifeCounts(true, kCpxCountDead));
	EXPECT_TRUE(ComputeProximityLifeCounts(false, kCpxCountDead));
	EXPECT_TRUE(ComputeProximityLifeCounts(true, kCpxDeadOnly));
	EXPECT_FALSE(ComputeProximityLifeCounts(false, kCpxDeadOnly));
}

TEST(AcsParity, ProximityResultByCount)
{
	EXPECT_EQ(ComputeProximityResult(1, 1, 0, false), 1);
	EXPECT_EQ(ComputeProximityResult(0, 1, 0, false), 0);
	EXPECT_EQ(ComputeProximityResult(0, 1, kCpxLessOrEqual, false), 1);
	EXPECT_EQ(ComputeProximityResult(0, 1, kCpxLessOrEqual | kCpxExact, false), 0);
	EXPECT_EQ(ComputeProximityResult(2, 1, 0, true), 1);
	EXPECT_EQ(ComputeProximityResult(2, 1, kCpxLessOrEqual, true), 0);
	EXPECT_EQ(ComputeProximityResult(2, 1, kCpxExact, true), 0);
	// [Doruk-Aksoy] A negative count with nothing near never stops early, so UZDoom answers 0.
	EXPECT_EQ(ComputeProximityResult(0, -1, 0, false), 0);
}

TEST(AcsParity, ProximityScanCountsWhatIsInRange)
{
	const std::vector<Candidate> all = { { 32, 0, 0, false, true }, { 500, 0, 0, false, true }, { 0, 48, 0, true, true } };
	EXPECT_EQ(Scan(all, 100, 1, 0).result, 1);
	EXPECT_EQ(Scan(all, 100, 2, 0).result, 0);
	EXPECT_EQ(Scan(all, 100, 2, kCpxCountDead).result, 1);
	EXPECT_EQ(Scan(all, 100, 1, kCpxDeadOnly).result, 1);
	EXPECT_EQ(Scan(all, 100, 0, 0).result, 1);
	EXPECT_EQ(Scan(all, 100, 0, kCpxExact).result, 0);
	EXPECT_EQ(Scan(all, 100, 3, kCpxLessOrEqual | kCpxCountDead).result, 1);
	EXPECT_EQ(Scan(all, 0, 0, 0).result, 0);
	EXPECT_EQ(Scan(all, -5, 0, 0).result, 0);
}

TEST(AcsParity, ProximityScanAsksSightOnlyWhenFlagged)
{
	const std::vector<Candidate> all = { { 32, 0, 0, false, false }, { 0, 40, 0, false, true } };
	const Scanned seen = Scan(all, 100, 2, kCpxCheckSight);
	EXPECT_EQ(seen.result, 0);
	EXPECT_EQ(seen.sightAsks, 2);
	const Scanned blind = Scan(all, 100, 2, 0);
	EXPECT_EQ(blind.result, 1);
	EXPECT_EQ(blind.sightAsks, 0);
}

// [Doruk-Aksoy] Without CPXF_CLOSEST or CPXF_FARTHEST the first match is chosen and the scan stops past `count`.
TEST(AcsParity, ProximityScanChoosesTheFirstWithoutAPreference)
{
	const std::vector<Candidate> all = { { 60, 0, 0, false, true }, { 20, 0, 0, false, true }, { 80, 0, 0, false, true } };
	const Scanned first = Scan(all, 100, 0, kCpxSetTarget);
	EXPECT_EQ(first.chosen, &all[0]);
	EXPECT_EQ(first.result, 1);
	EXPECT_EQ(Scan(all, 100, 0, 0).chosen, nullptr);
}

// [Doruk-Aksoy] A SET flag with a preference scans every candidate even past `count`.
TEST(AcsParity, ProximityScanChoosesTheClosestOrFarthest)
{
	const std::vector<Candidate> all = { { 60, 0, 0, false, true }, { 20, 0, 0, false, true }, { 80, 0, 0, false, true }, { 70, 0, 0, false, true } };
	EXPECT_EQ(Scan(all, 100, 0, kCpxSetMaster | kCpxClosest).chosen, &all[1]);
	EXPECT_EQ(Scan(all, 100, 0, kCpxSetTracer | kCpxFarthest).chosen, &all[2]);
	EXPECT_EQ(Scan(all, 100, 0, kCpxSetTracer | kCpxFarthest).result, 1);
	EXPECT_EQ(Scan(all, 100, 1, kCpxSetTarget | kCpxClosest | kCpxExact).result, 0);
	EXPECT_EQ(Scan(all, 100, 4, kCpxSetTarget | kCpxClosest).result, 1);
}

// [Doruk-Aksoy] A FARTHEST scan whose only match stands on the reference still takes it, as UZDoom falls back to the first.
TEST(AcsParity, ProximityScanFallsBackToTheFirstMatch)
{
	const std::vector<Candidate> all = { { 0, 0, 0, false, true } };
	EXPECT_EQ(Scan(all, 100, 1, kCpxSetTarget | kCpxFarthest).chosen, &all[0]);
}

// [Doruk-Aksoy] UZDoom's CheckInventory reads "Armor" as BasicArmor and "Health" as the actor's health, either case.
TEST(AcsParity, InventoryKindByName)
{
	EXPECT_EQ(ComputeInventoryKind("armor"), kInventoryArmor);
	EXPECT_EQ(ComputeInventoryKind("HEALTH"), kInventoryHealth);
	EXPECT_EQ(ComputeInventoryKind("Clip"), kInventoryItem);
	EXPECT_EQ(ComputeInventoryKind("Arm"), kInventoryItem);
	EXPECT_EQ(ComputeInventoryKind("Armory"), kInventoryItem);
	EXPECT_EQ(ComputeInventoryKind(nullptr), kInventoryItem);
}

// [Doruk-Aksoy] UZDoom's CheckInventory with `max`: the carried item's MaxAmount, else the class default's, else 0.
TEST(AcsParity, InventoryAmountAndMax)
{
	EXPECT_EQ(ComputeInventoryAmount({ true, 12, 50, true, 50 }, false), 12);
	EXPECT_EQ(ComputeInventoryAmount({ true, 12, 100, true, 50 }, true), 100);
	EXPECT_EQ(ComputeInventoryAmount({ false, 0, 0, true, 50 }, true), 50);
	EXPECT_EQ(ComputeInventoryAmount({ false, 0, 0, true, 50 }, false), 0);
	EXPECT_EQ(ComputeInventoryAmount({ false, 0, 0, false, 0 }, true), 0);
}

TEST(AcsParity, SectorDamageTypesAreTheOldMeansOfDeathNumbersBothWays)
{
	EXPECT_EQ(ComputeDamageTypeMod("Fire"), 14);
	EXPECT_EQ(ComputeDamageTypeMod("fIrE"), 14);
	EXPECT_EQ(ComputeDamageTypeMod("Massacre"), 1000);
	EXPECT_EQ(ComputeDamageTypeMod("BFGSplash"), 9);
	EXPECT_EQ(ComputeDamageTypeMod(nullptr), 0);
	EXPECT_EQ(ComputeDamageTypeMod(""), 0);
	EXPECT_EQ(ComputeDamageTypeMod("none"), 0);
	EXPECT_EQ(ComputeDamageTypeMod("Plasma"), -1);
	EXPECT_EQ(ComputeDamageTypeMod("Fir"), -1);
	EXPECT_EQ(ComputeDamageTypeMod("Fires"), -1);
	for (int mod : { 9, 12, 13, 14, 15, 16, 17, 18, 20, 22, 23, 24, 25, 26, 27, 1000 })
		EXPECT_EQ(ComputeDamageTypeMod(ComputeModDamageType(mod)), mod) << mod;
	EXPECT_EQ(ComputeModDamageType(0), nullptr);
	EXPECT_EQ(ComputeModDamageType(10), nullptr);
}

TEST(AcsParity, SectorDamageAmountsFitTheShortSectorsKeep)
{
	EXPECT_EQ(ComputeSectorDamageAmount(10), 10);
	EXPECT_EQ(ComputeSectorDamageAmount(0), 0);
	EXPECT_EQ(ComputeSectorDamageAmount(-5), 0);
	EXPECT_EQ(ComputeSectorDamageAmount(100000), 32767);
}

TEST(AcsParity, FriendlySeeBlocksKeepsAnActorsOwnValueOverItsClasss)
{
	EXPECT_EQ(ComputeSeeBlocksRead(0, 10), 10);
	EXPECT_EQ(ComputeSeeBlocksRead(ComputeSeeBlocksStore(25), 10), 25);
	EXPECT_EQ(ComputeSeeBlocksRead(ComputeSeeBlocksStore(0), 10), 0);
	EXPECT_EQ(ComputeSeeBlocksRead(ComputeSeeBlocksStore(-4), 10), -4);
	EXPECT_EQ(ComputeSeeBlocksRead(ComputeSeeBlocksStore(INT32_MAX), 10), INT32_MAX);
	EXPECT_EQ(ComputeSeeBlocksRead(ComputeSeeBlocksStore(INT32_MIN), 10), 10);
	EXPECT_EQ(ComputeSeeBlocksRead(ComputeSeeBlocksStore(500), 10), 500);
}

TEST(AcsParity, AFriendlysSearchIsCappedAndNeverNegative)
{
	EXPECT_EQ(ComputeSeeBlocksSearch(10), 10);
	EXPECT_EQ(ComputeSeeBlocksSearch(0), 0);
	EXPECT_EQ(ComputeSeeBlocksSearch(-3), 0);
	EXPECT_EQ(ComputeSeeBlocksSearch(kMaxFriendlySeeBlocks), kMaxFriendlySeeBlocks);
	EXPECT_EQ(ComputeSeeBlocksSearch(500), kMaxFriendlySeeBlocks);
}

namespace {

// [Doruk-Aksoy] A dry actor 56 high standing at z 0.
WaterDepthIn Dry()
{
	WaterDepthIn in = {};
	in.z = 0;
	in.height = 56 * kOne;
	return in;
}

} // namespace

TEST(AcsParity, AnUnderwaterSectorIsAsDeepAsTheActorIsTall)
{
	WaterDepthIn in = Dry();
	EXPECT_EQ(ComputeWaterDepth(in), 0);
	in.underwater = true;
	in.heightSec = true;
	in.heightSecWater = true;
	in.heightSecFloor = 8 * kOne;
	EXPECT_EQ(ComputeWaterDepth(in), 56 * kOne);
}

TEST(AcsParity, AHeightsecIsWaterOnlyWhenItSaysSo)
{
	WaterDepthIn in = Dry();
	in.heightSec = true;
	in.heightSecFloor = 24 * kOne;
	in.heightSecCeiling = 128 * kOne;
	EXPECT_EQ(ComputeWaterDepth(in), 0);
	in.heightSecWater = true;
	EXPECT_EQ(ComputeWaterDepth(in), 24 * kOne);
	in.z = 30 * kOne;
	EXPECT_EQ(ComputeWaterDepth(in), 0);
	in.heightSecCeiling = 64 * kOne;
	EXPECT_EQ(ComputeWaterDepth(in), 56 * kOne);
	in.fakeFloorOnly = true;
	EXPECT_EQ(ComputeWaterDepth(in), 0);
}

TEST(AcsParity, ASwimmable3DFloorIsDeepToItsTop)
{
	WaterDepthIn in = Dry();
	in.swimFloor = true;
	in.swimTop = 40 * kOne;
	EXPECT_EQ(ComputeWaterDepth(in), 40 * kOne);
	in.z = 50 * kOne;
	EXPECT_EQ(ComputeWaterDepth(in), 0);
	in.heightSec = true;
	in.heightSecWater = true;
	in.heightSecFloor = 100 * kOne;
	in.heightSecCeiling = 200 * kOne;
	EXPECT_EQ(ComputeWaterDepth(in), 50 * kOne);
}

TEST(AcsParity, TeamScoresAreFragsWinsOrPointsByTheGameMode)
{
	EXPECT_EQ(ComputeTeamScoreKind(true, false), kTeamScoreFrags);
	EXPECT_EQ(ComputeTeamScoreKind(true, true), kTeamScoreFrags);
	EXPECT_EQ(ComputeTeamScoreKind(false, true), kTeamScoreWins);
	EXPECT_EQ(ComputeTeamScoreKind(false, false), kTeamScorePoints);
}
