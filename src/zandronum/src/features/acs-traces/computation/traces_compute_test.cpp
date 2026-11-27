// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-traces/computation/traces_compute.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

using namespace zx::acstraces;

namespace {

const int64_t kOne = 65536;
const Shooter kMonster = { false, false, false, false };

// [Doruk-Aksoy] Each name one up and no name left none, one type for every remap test so ComputeRemapTracerNames has a single instantiation to cover.
struct NextName
{
	int32_t operator()(int32_t name) const { return name == 0 ? 0 : name + 1; }
};

using RuleTest = std::function<bool(const Rule &)>;

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
	EXPECT_EQ(ComputeLineTraceDecision(kHitWall, false, true, true, true), kTraceStop);
	EXPECT_EQ(ComputeLineTraceDecision(kHitFloor, false, true, false, false), kTraceStop);
	EXPECT_EQ(ComputeLineTraceDecision(kHitActor, false, false, false, true), kTraceStop);
	EXPECT_EQ(ComputeLineTraceDecision(kHitActor, false, true, false, false), kTraceSkip);
	EXPECT_EQ(ComputeLineTraceDecision(kHitActor, false, false, true, true), kTraceSkip);
	EXPECT_EQ(ComputeLineTraceDecision(kHitActor, false, false, true, false), kTraceStop);
}

TEST(AcsTraces, ASnapshotMirrorIsAlwaysPassedThrough)
{
	EXPECT_EQ(ComputeLineTraceDecision(kHitActor, true, false, false, false), kTraceSkip);
	EXPECT_EQ(ComputeLineTraceDecision(kHitActor, true, true, true, true), kTraceSkip);
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

	ComputeRemapTracerNames(s, NextName());
	EXPECT_EQ(s.Find(40)->last.v[kTrHitTexture], 10 + kTrHitTexture + 1);
	EXPECT_EQ(s.Find(40)->hits[1].v[kTrHitTexture], 300 + kTrHitTexture + 1);

	const std::vector<int32_t> flat = ComputeFlattenTracers(s);
	ScriptTracers back;
	ASSERT_TRUE(ComputeUnflattenTracers(flat.data(), flat.size(), back, true));
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
		return !ComputeUnflattenTracers(bad.data(), bad.size(), out, true) && out.Count() == 1 && out.Find(99) != nullptr;
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
	EXPECT_TRUE(refused(std::vector<int32_t>(good.begin(), good.begin() + 2 + kTrPropCount + 1 + 3)));

	ScriptTracers out;
	EXPECT_TRUE(ComputeUnflattenTracers(good.data(), good.size(), out, true));
	EXPECT_EQ(out.Find(2)->hits.size(), 1u);
}

TEST(AcsTraces, EachHitHasOneKindAndA3DFloorSideIsNotAWall)
{
	EXPECT_EQ(ComputeHitKind(kHitFloor, kTierUpper), kKindFloor);
	EXPECT_EQ(ComputeHitKind(kHitCeiling, kTierMiddle), kKindCeiling);
	EXPECT_EQ(ComputeHitKind(kHitWall, kTierLower), kKindWall);
	EXPECT_EQ(ComputeHitKind(kHitWall, kTierFFloor), kKind3DFloor);
	EXPECT_EQ(ComputeHitKind(kHitActor, kTierFFloor), kKindActor);
	EXPECT_EQ(ComputeHitKind(kHitNone, kTierMiddle), 0);
}

TEST(AcsTraces, RuleKindsActionsAndFiltersAreTheirOwnNumbers)
{
	EXPECT_TRUE(ComputeRuleKindValid(kKindAny));
	EXPECT_TRUE(ComputeRuleKindValid(kKind3DFloor));
	EXPECT_FALSE(ComputeRuleKindValid(kHitActor));
	EXPECT_FALSE(ComputeRuleKindValid(kKind3DFloor + 1));
	EXPECT_TRUE(ComputeRuleActionValid(kActStop));
	EXPECT_TRUE(ComputeRuleActionValid(kActRecord));
	EXPECT_FALSE(ComputeRuleActionValid(kActRecord + 1));
	EXPECT_FALSE(ComputeRuleActionValid(-1));

	EXPECT_EQ(ComputeRuleFilterArg(0), zx::acsqueries::kArgNone);
	EXPECT_EQ(ComputeRuleFilterArg(kTqLineFlag), zx::acsqueries::kArgNumber);
	EXPECT_EQ(ComputeRuleFilterArg(kTqSpecial), zx::acsqueries::kArgNumber);
	EXPECT_EQ(ComputeRuleFilterArg(kTqTexture), zx::acsqueries::kArgName);
	EXPECT_EQ(ComputeRuleFilterArg(zx::acsqueries::kFqClass), zx::acsqueries::kArgName);
	EXPECT_EQ(ComputeRuleFilterArg(zx::acsqueries::kFqSight), zx::acsqueries::kArgHandle);
	EXPECT_EQ(ComputeRuleFilterArg(zx::acsqueries::kFq2D), zx::acsqueries::kArgInvalid);
	EXPECT_EQ(ComputeRuleFilterArg(zx::acsqueries::kFqEdges), zx::acsqueries::kArgInvalid);
	EXPECT_EQ(ComputeRuleFilterArg(kTqTexture + 1), zx::acsqueries::kArgInvalid);

	EXPECT_EQ(ComputeRuleFilterDefaultB(kTqSpecial), 1);
	EXPECT_EQ(ComputeRuleFilterDefaultB(zx::acsqueries::kFqFlag), 1);
	EXPECT_EQ(ComputeRuleFilterDefaultB(zx::acsqueries::kFqTid), 0);
}

TEST(AcsTraces, FiltersFitTheKindsTheyCanTest)
{
	EXPECT_TRUE(ComputeRuleFilterFits(kKindWall, 0));
	EXPECT_TRUE(ComputeRuleFilterFits(kKindActor, zx::acsqueries::kFqTid));
	EXPECT_TRUE(ComputeRuleFilterFits(kKindAny, zx::acsqueries::kFqTid));
	EXPECT_FALSE(ComputeRuleFilterFits(kKindWall, zx::acsqueries::kFqTid));
	EXPECT_TRUE(ComputeRuleFilterFits(kKindWall, kTqSpecial));
	EXPECT_TRUE(ComputeRuleFilterFits(kKind3DFloor, kTqLineFlag));
	EXPECT_TRUE(ComputeRuleFilterFits(kKindAny, kTqLineFlag));
	EXPECT_FALSE(ComputeRuleFilterFits(kKindFloor, kTqSpecial));
	EXPECT_FALSE(ComputeRuleFilterFits(kKindActor, kTqLineFlag));
	EXPECT_TRUE(ComputeRuleFilterFits(kKindFloor, kTqTexture));
	EXPECT_FALSE(ComputeRuleFilterFits(kKindActor, kTqTexture));
}

TEST(AcsTraces, TheFirstMatchingRuleDecidesAndNoneStops)
{
	const std::vector<Rule> rules = {
		{ kKindActor, kActRecord, { zx::acsqueries::kFqTid, 5, 0 } },
		{ kKindActor, kActSkip, { 0, 0, 0 } },
		{ kKindAny, kActContinue, { kTqTexture, 9, 1 } },
	};
	int tested = 0;
	// [Doruk-Aksoy] Every test is one std::function type, so ComputeRuleAction has a single instantiation to cover.
	const RuleTest tid5 = [&](const Rule &r) { ++tested; return r.filter.a == 5; };
	EXPECT_EQ(ComputeRuleAction(rules, kKindActor, tid5), kActRecord);
	EXPECT_EQ(tested, 1);
	const RuleTest none = [](const Rule &) { return false; };
	EXPECT_EQ(ComputeRuleAction(rules, kKindActor, none), kActSkip);
	EXPECT_EQ(ComputeRuleAction(rules, kKindWall, none), kActStop);
	EXPECT_EQ(ComputeRuleAction(rules, kKindWall, RuleTest([](const Rule &) { return true; })), kActContinue);
	EXPECT_EQ(ComputeRuleAction(std::vector<Rule>(), kKindWall, none), kActStop);
}

TEST(AcsTraces, ActionsTellTraceWhatToDoAndRecordingSkips)
{
	EXPECT_EQ(ComputeRuleStatus(kActStop, kKindWall), kTraceStop);
	EXPECT_EQ(ComputeRuleStatus(kActContinue, kKindActor), kTraceContinue);
	EXPECT_EQ(ComputeRuleStatus(kActSkip, kKind3DFloor), kTraceSkip);
	EXPECT_EQ(ComputeRuleStatus(kActRecord, kKindWall), kTraceSkip);
	EXPECT_EQ(ComputeRuleStatus(kActAbort, kKindActor), kTraceAbort);
}

TEST(AcsTraces, AFloorOrCeilingEndsTheTraceUnlessItAborts)
{
	EXPECT_EQ(ComputeRuleStatus(kActSkip, kKindFloor), kTraceStop);
	EXPECT_EQ(ComputeRuleStatus(kActRecord, kKindCeiling), kTraceStop);
	EXPECT_EQ(ComputeRuleStatus(kActContinue, kKindFloor), kTraceStop);
	EXPECT_EQ(ComputeRuleStatus(kActAbort, kKindCeiling), kTraceAbort);
}

TEST(AcsTraces, LineFiltersTestFlagsAndSpecialsAndTurnRoundWithB)
{
	using zx::acsqueries::Filter;
	EXPECT_TRUE(ComputeLineFilterMatch(Filter{ kTqLineFlag, 0x9, 1 }, 0xB, 0));
	EXPECT_FALSE(ComputeLineFilterMatch(Filter{ kTqLineFlag, 0x9, 1 }, 0x8, 0));
	EXPECT_TRUE(ComputeLineFilterMatch(Filter{ kTqLineFlag, 0x9, 0 }, 0x8, 0));
	EXPECT_TRUE(ComputeLineFilterMatch(Filter{ kTqSpecial, 80, 1 }, 0, 80));
	EXPECT_FALSE(ComputeLineFilterMatch(Filter{ kTqSpecial, 80, 1 }, 0, 12));
	EXPECT_TRUE(ComputeLineFilterMatch(Filter{ kTqSpecial, 80, 0 }, 0, 12));
}

TEST(AcsTraces, StopsAndRecordsKeepHitsUntilAnAbortDropsThem)
{
	RuleRun run;
	ComputeRuleRecord(run, kActRecord, Numbered(1));
	ComputeRuleRecord(run, kActSkip, Numbered(2));
	ComputeRuleRecord(run, kActContinue, Numbered(3));
	ComputeRuleRecord(run, kActStop, Numbered(4));
	ASSERT_EQ(run.hits.size(), 2u);
	EXPECT_EQ(run.hits[1].v[0], 4);
	ComputeRuleRecord(run, kActAbort, Numbered(5));
	EXPECT_TRUE(run.aborted);
	EXPECT_TRUE(run.hits.empty());
	ComputeRuleRecord(run, kActStop, Numbered(6));
	EXPECT_TRUE(run.hits.empty());

	RuleRun full;
	for (int i = 0; i < 70; ++i)
		ComputeRuleRecord(full, kActRecord, Numbered(i));
	EXPECT_EQ(full.hits.size(), kMaxHits);
	EXPECT_EQ(full.hits.back().v[0], 63);
}

TEST(AcsTraces, ADirectionBecomesUnitLength)
{
	Fixed3 dir;
	ASSERT_TRUE(ComputeTraceUnit(3 << 16, 0, -(4 << 16), dir));
	EXPECT_EQ(dir.x, 39322);
	EXPECT_EQ(dir.y, 0);
	EXPECT_EQ(dir.z, -52429);
	ASSERT_TRUE(ComputeTraceUnit(0, 7, 0, dir));
	EXPECT_EQ(dir.y, 65536);
	EXPECT_FALSE(ComputeTraceUnit(0, 0, 0, dir));
}

TEST(AcsTraces, ATracerTakesThirtyTwoFittingRulesAndClearingDropsThem)
{
	ScriptTracers s;
	s.Create(4);
	const Rule good = { kKindWall, kActRecord, { kTqSpecial, 80, 1 } };
	EXPECT_FALSE(s.AddRule(9, good));
	EXPECT_FALSE(s.AddRule(4, Rule{ kHitWall, kActStop, { 0, 0, 0 } }));
	EXPECT_FALSE(s.AddRule(4, Rule{ kKindWall, kActRecord + 1, { 0, 0, 0 } }));
	EXPECT_FALSE(s.AddRule(4, Rule{ kKindActor, kActStop, { zx::acsqueries::kFq2D, 0, 0 } }));
	EXPECT_FALSE(s.AddRule(4, Rule{ kKindWall, kActStop, { zx::acsqueries::kFqTid, 3, 0 } }));
	for (size_t i = 0; i < kMaxRules; ++i)
		EXPECT_TRUE(s.AddRule(4, good));
	EXPECT_FALSE(s.AddRule(4, good));
	EXPECT_EQ(s.Find(4)->rules.size(), kMaxRules);
	EXPECT_TRUE(s.Clear(4));
	EXPECT_TRUE(s.Find(4)->rules.empty());
}

TEST(AcsTraces, RulesSaveWithTheirNamesAndAnOldRunHasNone)
{
	ScriptTracers s;
	s.Create(6);
	s.AddRule(6, Rule{ kKindActor, kActStop, { zx::acsqueries::kFqClass, 300, 1 } });
	s.AddRule(6, Rule{ kKindWall, kActRecord, { kTqTexture, 400, 0 } });
	s.AddRule(6, Rule{ kKindWall, kActSkip, { kTqSpecial, 80, 1 } });
	ComputeRemapTracerNames(s, NextName());
	EXPECT_EQ(s.Find(6)->rules[0].filter.a, 301);
	EXPECT_EQ(s.Find(6)->rules[1].filter.a, 401);
	EXPECT_EQ(s.Find(6)->rules[2].filter.a, 80);

	const std::vector<int32_t> flat = ComputeFlattenTracers(s);
	ScriptTracers back;
	ASSERT_TRUE(ComputeUnflattenTracers(flat.data(), flat.size(), back, true));
	ASSERT_EQ(back.Find(6)->rules.size(), 3u);
	EXPECT_EQ(back.Find(6)->rules[1].action, kActRecord);
	EXPECT_EQ(back.Find(6)->rules[1].filter.b, 0);
	EXPECT_FALSE(ComputeUnflattenTracers(flat.data(), flat.size(), back, false));

	// [Doruk-Aksoy] A run saved before rules: one tracer, its last hit and no kept hits.
	std::vector<int32_t> old = { 1, 6 };
	const Hit empty = ComputeEmptyHit();
	old.insert(old.end(), empty.v, empty.v + kTrPropCount);
	old.push_back(0);
	ScriptTracers fromOld;
	ASSERT_TRUE(ComputeUnflattenTracers(old.data(), old.size(), fromOld, false));
	EXPECT_TRUE(fromOld.Find(6)->rules.empty());
	EXPECT_FALSE(ComputeUnflattenTracers(old.data(), old.size(), fromOld, true));

	std::vector<int32_t> badRule = flat;
	badRule[badRule.size() - 5] = kHitWall;
	EXPECT_FALSE(ComputeUnflattenTracers(badRule.data(), badRule.size(), back, true));
	std::vector<int32_t> cut = flat;
	cut.pop_back();
	EXPECT_FALSE(ComputeUnflattenTracers(cut.data(), cut.size(), back, true));
	std::vector<int32_t> tooMany = flat;
	tooMany[tooMany.size() - 16] = int32_t(kMaxRules) + 1;
	EXPECT_FALSE(ComputeUnflattenTracers(tooMany.data(), tooMany.size(), back, true));
}
