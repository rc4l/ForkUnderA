// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-traces/computation/traces_compute.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace zx::acstraces;

namespace {

const int64_t kOne = 65536;
const Shooter kMonster = { false, false, false, false };

RawHit Raw(int type)
{
	RawHit r = {};
	r.type = type;
	r.actor = 77;
	r.pos = Fixed3{ 100 * kOne, -20 * kOne, 48 * kOne };
	r.dir = Fixed3{ kOne, 0, -kOne / 2 };
	r.distance = 300 * kOne;
	r.fraction = kOne / 4;
	r.line = 12;
	r.lineId = 5;
	r.side = 1;
	r.tier = kTierLower;
	r.sector = 3;
	r.sectorTag = 9;
	r.ffloorSector = 6;
	r.crossedWater = -1;
	r.crossed3DWater = 4;
	r.texture = 1234;
	return r;
}

Hit Numbered(int32_t base)
{
	Hit h;
	for (int i = 0; i < kTrPropCount; ++i)
		h.v[i] = base + i;
	return h;
}

} // namespace

TEST(AcsTraces, LineTraceFlagsBuildUZDoomsMasks)
{
	TraceMasks m = ComputeLineTraceMasks(0, kMonster);
	EXPECT_EQ(m.actors, kMfShootable);
	EXPECT_EQ(m.lines, kMlBlockEverything | kMlBlockHitscan);
	EXPECT_EQ(m.trace, 0u);

	m = ComputeLineTraceMasks(kTrfAllActors | kTrfSolidActors | kTrfBlockUse | kTrfNoSky | kTrfFuaPhysicalSize, kMonster);
	EXPECT_EQ(m.actors, 0xFFFFFFFFu);
	EXPECT_EQ(m.lines, kMlBlockEverything | kMlBlockHitscan | kMlBlockUse);
	EXPECT_EQ(m.trace, kTraceNoSky | kTraceNoPassWidth);

	EXPECT_EQ(ComputeLineTraceMasks(kTrfSolidActors, kMonster).actors, kMfShootable | kMfSolid);
	EXPECT_EQ(ComputeLineTraceMasks(kTrfThruBlock | kTrfThruHitscan, kMonster).lines, 0u);
}

TEST(AcsTraces, BlockSelfBlocksTheLinesTheShooterWouldBumpInto)
{
	EXPECT_EQ(ComputeLineTraceMasks(kTrfBlockSelf | kTrfThruBlock | kTrfThruHitscan, kMonster).lines, kMlBlocking | kMlBlockMonsters);
	const Shooter missile = { true, true, false, false };
	EXPECT_EQ(ComputeLineTraceMasks(kTrfBlockSelf | kTrfThruBlock | kTrfThruHitscan, missile).lines, kMlBlockProjectile);
	const Shooter player = { false, true, true, false };
	EXPECT_EQ(ComputeLineTraceMasks(kTrfBlockSelf | kTrfThruBlock | kTrfThruHitscan, player).lines, kMlBlocking | kMlBlockPlayers);
	const Shooter floater = { false, true, false, true };
	EXPECT_EQ(ComputeLineTraceMasks(kTrfBlockSelf | kTrfThruBlock | kTrfThruHitscan, floater).lines, kMlBlocking | kMlBlockFloaters);
}

TEST(AcsTraces, AcsAnglesIndexTheFineTable)
{
	EXPECT_EQ(ComputeAcsAngleToFine(0), 0u);
	EXPECT_EQ(ComputeAcsAngleToFine(0x4000), 2048u);
	EXPECT_EQ(ComputeAcsAngleToFine(0x8000), 4096u);
	EXPECT_EQ(ComputeAcsAngleToFine(-0x4000), 6144u);
	EXPECT_EQ(ComputeAcsAngleToFine(0x10000), 0u);
}

TEST(AcsTraces, TheDirectionTiltsByPitchAndLooksDownForAPositivePitch)
{
	const Fixed3 flat = ComputeTraceDirection(kOne, 0, 0, kOne);
	EXPECT_EQ(flat.x, 0);
	EXPECT_EQ(flat.y, kOne);
	EXPECT_EQ(flat.z, 0);
	const Fixed3 down = ComputeTraceDirection(kOne / 2, kOne / 2, kOne, 0);
	EXPECT_EQ(down.x, kOne / 2);
	EXPECT_EQ(down.y, 0);
	EXPECT_EQ(down.z, -kOne / 2);
}

TEST(AcsTraces, TheStartTurnsOffsetsWithTheActorUnlessAbsolute)
{
	const Fixed3 actor = { 1000 * kOne, 2000 * kOne, 64 * kOne };
	const int32_t z = 32 << 16, forward = 10 << 16, side = 4 << 16;

	// [Doruk-Aksoy] Facing north (angle 90): forward is +y and side is +x, from the feet less the floor clip.
	Fixed3 s = ComputeLineTraceStart(0, actor, 8 * kOne, 0, kOne, z, forward, side);
	EXPECT_EQ(s.x, 1004 * kOne);
	EXPECT_EQ(s.y, 2010 * kOne);
	EXPECT_EQ(s.z, 88 * kOne);

	s = ComputeLineTraceStart(kTrfAbsOffset, actor, 0, 0, kOne, z, forward, side);
	EXPECT_EQ(s.x, 1010 * kOne);
	EXPECT_EQ(s.y, 2004 * kOne);
	EXPECT_EQ(s.z, 96 * kOne);

	s = ComputeLineTraceStart(kTrfAbsPosition | kTrfAbsOffset, actor, 8 * kOne, 0, kOne, z, forward, side);
	EXPECT_EQ(s.x, 10 * kOne);
	EXPECT_EQ(s.y, 4 * kOne);
	EXPECT_EQ(s.z, 32 * kOne);
}

TEST(AcsTraces, OnlyActorsTheFlagsLetThroughAreSkipped)
{
	EXPECT_EQ(ComputeLineTraceDecision(kHitWall, true, true, true), kTraceStop);
	EXPECT_EQ(ComputeLineTraceDecision(kHitFloor, true, false, false), kTraceStop);
	EXPECT_EQ(ComputeLineTraceDecision(kHitActor, false, false, true), kTraceStop);
	EXPECT_EQ(ComputeLineTraceDecision(kHitActor, true, false, false), kTraceSkip);
	EXPECT_EQ(ComputeLineTraceDecision(kHitActor, false, true, true), kTraceSkip);
	EXPECT_EQ(ComputeLineTraceDecision(kHitActor, false, true, false), kTraceStop);
}

TEST(AcsTraces, WallPartsAndTexturesFollowUZDoom)
{
	EXPECT_EQ(ComputeLinePart(kTierUpper), 0);
	EXPECT_EQ(ComputeLinePart(kTierMiddle), 1);
	EXPECT_EQ(ComputeLinePart(kTierLower), 2);
	EXPECT_EQ(ComputeLinePart(kTierFFloor), 1);
	EXPECT_EQ(ComputeWallTextureSlot(kTierLower, kFfUpperTexture), 2);
	EXPECT_EQ(ComputeWallTextureSlot(kTierFFloor, 0), 1);
	EXPECT_EQ(ComputeWallTextureSlot(kTierFFloor, kFfUpperTexture), 0);
	EXPECT_EQ(ComputeWallTextureSlot(kTierFFloor, kFfLowerTexture), 2);
}

TEST(AcsTraces, AWallHitKeepsItsLineAndCutsPositionsToAcs)
{
	const Hit h = ComputeTraceHit(Raw(kHitWall));
	EXPECT_EQ(h.v[kTrHitType], kHitWall);
	EXPECT_EQ(h.v[kTrHitActor], 0);
	EXPECT_EQ(h.v[kTrHitX], 100 << 16);
	EXPECT_EQ(h.v[kTrHitY], -(20 << 16));
	EXPECT_EQ(h.v[kTrHitZ], 48 << 16);
	EXPECT_EQ(h.v[kTrDirX], 1 << 16);
	EXPECT_EQ(h.v[kTrDirZ], -(1 << 15));
	EXPECT_EQ(h.v[kTrDistance], 300 << 16);
	EXPECT_EQ(h.v[kTrFraction], 1 << 14);
	EXPECT_EQ(h.v[kTrHitLine], 12);
	EXPECT_EQ(h.v[kTrLineId], 5);
	EXPECT_EQ(h.v[kTrLineSide], 1);
	EXPECT_EQ(h.v[kTrLinePart], 2);
	EXPECT_EQ(h.v[kTrHitSector], 3);
	EXPECT_EQ(h.v[kTrSectorTag], 9);
	EXPECT_EQ(h.v[kTrSectorPlane], 0);
	EXPECT_EQ(h.v[kTr3DFloor], -1);
	EXPECT_EQ(h.v[kTrCrossedWater], -1);
	EXPECT_EQ(h.v[kTrCrossed3DWater], 4);
	EXPECT_EQ(h.v[kTrHitTexture], 1234);

	RawHit far = Raw(kHitWall);
	far.pos.x = (int64_t(1) << 32) + 5 * kOne;
	EXPECT_EQ(ComputeTraceHit(far).v[kTrHitX], 5 << 16);
}

TEST(AcsTraces, A3DFloorSideNamesItsControlSector)
{
	RawHit r = Raw(kHitWall);
	r.tier = kTierFFloor;
	const Hit h = ComputeTraceHit(r);
	EXPECT_EQ(h.v[kTr3DFloor], 6);
	EXPECT_EQ(h.v[kTrLinePart], 1);
}

TEST(AcsTraces, ActorAndPlaneHitsLeaveTheLineOut)
{
	Hit h = ComputeTraceHit(Raw(kHitActor));
	EXPECT_EQ(h.v[kTrHitActor], 77);
	EXPECT_EQ(h.v[kTrHitLine], -1);
	EXPECT_EQ(h.v[kTrLineId], 0);
	EXPECT_EQ(h.v[kTrLineSide], 0);
	EXPECT_EQ(h.v[kTrLinePart], 0);
	EXPECT_EQ(h.v[kTrHitTexture], 0);
	EXPECT_EQ(h.v[kTr3DFloor], -1);

	h = ComputeTraceHit(Raw(kHitCeiling));
	EXPECT_EQ(h.v[kTrSectorPlane], 1);
	EXPECT_EQ(h.v[kTrHitTexture], 1234);
	EXPECT_EQ(h.v[kTrHitActor], 0);

	RawHit none = Raw(kHitNone);
	none.sector = -1;
	h = ComputeTraceHit(none);
	EXPECT_EQ(h.v[kTrHitSector], -1);
	EXPECT_EQ(h.v[kTrSectorTag], 0);
}

TEST(AcsTraces, PropertiesReadByNumberAndTheTextureOnlyAsAString)
{
	const Hit h = Numbered(100);
	EXPECT_EQ(ComputeTraceProp(h, kTrHitType), 100);
	EXPECT_EQ(ComputeTraceProp(h, kTrCrossed3DWater), 100 + kTrCrossed3DWater);
	EXPECT_EQ(ComputeTraceProp(h, kTrHitTexture), 0);
	EXPECT_EQ(ComputeTraceProp(h, -1), 0);
	EXPECT_EQ(ComputeTraceProp(h, kTrPropCount), 0);
	EXPECT_TRUE(ComputeTracePropValid(kTrHitTexture));
	EXPECT_FALSE(ComputeTracePropValid(kTrPropCount));

	const int32_t info[3] = { 4, 5, 6 };
	EXPECT_EQ(ComputeInfoProp(info, 3, 2), 6);
	EXPECT_EQ(ComputeInfoProp(info, 3, 3), 0);
	EXPECT_EQ(ComputeInfoProp(info, 3, -1), 0);
}

TEST(AcsTraces, AnEmptyHitHasNoLineSectorOrWater)
{
	const Hit h = ComputeEmptyHit();
	EXPECT_EQ(h.v[kTrHitType], kHitNone);
	EXPECT_EQ(h.v[kTrHitLine], -1);
	EXPECT_EQ(h.v[kTrHitSector], -1);
	EXPECT_EQ(h.v[kTr3DFloor], -1);
	EXPECT_EQ(h.v[kTrCrossedWater], -1);
	EXPECT_EQ(h.v[kTrCrossed3DWater], -1);
	EXPECT_EQ(h.v[kTrDistance], 0);
}

TEST(AcsTraces, AScriptHoldsSixteenTracersFoundByTheirIds)
{
	ScriptTracers s;
	for (int32_t id = 1; id <= 16; ++id)
		EXPECT_TRUE(s.Create(id));
	EXPECT_FALSE(s.Create(17));
	EXPECT_EQ(s.Count(), 16u);
	EXPECT_TRUE(s.Free(4));
	EXPECT_FALSE(s.Free(4));
	EXPECT_FALSE(s.Free(0));
	EXPECT_FALSE(s.Create(5));
	EXPECT_TRUE(s.Create(17));
	EXPECT_EQ(s.Find(0), nullptr);
	EXPECT_EQ(s.Find(4), nullptr);
	ASSERT_NE(s.Find(17), nullptr);
	const ScriptTracers &c = s;
	EXPECT_EQ(c.Find(17)->id, 17);
	EXPECT_EQ(ComputeHighestTracerId(s), 17);
	EXPECT_EQ(ComputeHighestTracerId(ScriptTracers()), 0);
}

TEST(AcsTraces, AResultKeepsSixtyFourHitsAndClearingDropsThem)
{
	ScriptTracers s;
	s.Create(3);
	std::vector<Hit> hits(70, Numbered(1));
	hits[63] = Numbered(500);
	EXPECT_TRUE(s.SetResult(3, Numbered(9), hits));
	EXPECT_FALSE(s.SetResult(8, Numbered(9), hits));
	const Tracer &t = *s.Find(3);
	EXPECT_EQ(t.hits.size(), kMaxHits);
	EXPECT_EQ(ComputeTracerHit(t, -1)->v[0], 9);
	EXPECT_EQ(ComputeTracerHit(t, 63)->v[0], 500);
	EXPECT_EQ(ComputeTracerHit(t, 64), nullptr);
	EXPECT_EQ(ComputeTracerHit(t, -2), nullptr);

	EXPECT_TRUE(s.Clear(3));
	EXPECT_FALSE(s.Clear(8));
	EXPECT_TRUE(s.Find(3)->hits.empty());
	EXPECT_EQ(s.Find(3)->last.v[kTrHitLine], -1);
}

TEST(AcsTraces, TracersSaveWithTheirHitsAndTexturesRemap)
{
	ScriptTracers s;
	s.Create(11);
	s.Create(40);
	s.SetResult(40, Numbered(10), { Numbered(200), Numbered(300) });

	ComputeRemapTracerNames(s, [](int32_t name) { return name + 1; });
	EXPECT_EQ(s.Find(40)->last.v[kTrHitTexture], 10 + kTrHitTexture + 1);
	EXPECT_EQ(s.Find(40)->hits[1].v[kTrHitTexture], 300 + kTrHitTexture + 1);

	const std::vector<int32_t> flat = ComputeFlattenTracers(s);
	ScriptTracers back;
	ASSERT_TRUE(ComputeUnflattenTracers(flat.data(), flat.size(), back));
	ASSERT_EQ(back.Count(), 2u);
	EXPECT_EQ(back.Find(11)->hits.size(), 0u);
	EXPECT_EQ(back.Find(11)->last.v[kTrHitLine], -1);
	EXPECT_EQ(back.Find(40)->last.v[kTrHitX], 10 + kTrHitX);
	EXPECT_EQ(back.Find(40)->hits[0].v[kTrDistance], 200 + kTrDistance);
	EXPECT_EQ(back.Find(40)->hits[1].v[kTrHitType], 300);
}

TEST(AcsTraces, ADamagedSaveLoadsNothing)
{
	ScriptTracers s;
	s.Create(2);
	s.SetResult(2, Numbered(1), { Numbered(2) });
	const std::vector<int32_t> good = ComputeFlattenTracers(s);
	ScriptTracers keep;
	keep.Create(99);

	const auto refused = [&](std::vector<int32_t> bad) {
		ScriptTracers out = keep;
		return !ComputeUnflattenTracers(bad.data(), bad.size(), out) && out.Count() == 1 && out.Find(99) != nullptr;
	};
	EXPECT_TRUE(refused({}));
	EXPECT_TRUE(refused({ 17 }));
	EXPECT_TRUE(refused({ -1 }));
	EXPECT_TRUE(refused({ 1 }));
	EXPECT_TRUE(refused({ 1, 0 }));
	EXPECT_TRUE(refused({ 2, 5 }));
	EXPECT_TRUE(refused(std::vector<int32_t>(good.begin(), good.end() - 1)));
	std::vector<int32_t> extra = good;
	extra.push_back(0);
	EXPECT_TRUE(refused(extra));
	std::vector<int32_t> tooMany = good;
	tooMany[2 + kTrPropCount] = int32_t(kMaxHits) + 1;
	EXPECT_TRUE(refused(tooMany));
	std::vector<int32_t> twice = { 2 };
	twice.insert(twice.end(), good.begin() + 1, good.end());
	twice.insert(twice.end(), good.begin() + 1, good.end());
	EXPECT_TRUE(refused(twice));
	std::vector<int32_t> shortLast = { 1, 2, 0 };
	EXPECT_TRUE(refused(shortLast));

	ScriptTracers out;
	EXPECT_TRUE(ComputeUnflattenTracers(good.data(), good.size(), out));
	EXPECT_EQ(out.Find(2)->hits.size(), 1u);
}
