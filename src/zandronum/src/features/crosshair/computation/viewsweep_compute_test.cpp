// [MobiusU9B] Tests for the view sweep, covering every branch of viewsweep_compute.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/crosshair/computation/viewsweep_compute.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

using zx::crosshair::ComputeNearestSweepHit;
using zx::crosshair::ComputeRayBoxEntry;
using zx::crosshair::ComputeRayBoxExit;
using zx::crosshair::ComputeSweepConsiders;
using zx::crosshair::ComputeSweepDue;
using zx::crosshair::ComputeSweepKind;
using zx::crosshair::kSweepDecoration;
using zx::crosshair::kSweepItem;
using zx::crosshair::kSweepMonster;
using zx::crosshair::kSweepPlayer;
using zx::crosshair::kSweepProjectile;
using zx::crosshair::SweepActor;
using zx::crosshair::SweepBox;
using zx::crosshair::SweepEntry;
using zx::crosshair::SweepHit;
using zx::crosshair::SweepRay;

namespace
{
	const double kNaN = std::numeric_limits<double>::quiet_NaN();
	const double kFar = 8192.0;

	// [MobiusU9B] An eye at the origin, 41 units up like a Doom player's, looking along +x.
	const SweepRay kLookEast{0.0, 0.0, 41.0, 1.0, 0.0, 0.0};

	// [MobiusU9B] A 20-radius, 56-high actor standing on the floor, its centre 100 units east.
	const SweepBox kImpAhead{100.0, 0.0, 0.0, 20.0, 56.0};

	// [MobiusU9B] A Doom player's 16-radius, 56-high body around that eye.
	const SweepBox kPlayerBody{0.0, 0.0, 0.0, 16.0, 56.0};

	// [MobiusU9B] A plain decoration in the blockmap, which the sweep looks at.
	SweepActor Decoration()
	{
		return SweepActor{false, false, true, false, false, false, false, false};
	}
}

TEST(ComputeRayBoxEntry, HitsTheNearFaceHeadOn)
{
	const SweepEntry entry = ComputeRayBoxEntry(kLookEast, kImpAhead, kFar);
	EXPECT_TRUE(entry.hit);
	EXPECT_DOUBLE_EQ(80.0, entry.distance);
}

TEST(ComputeRayBoxEntry, HitsFromTheOtherSide)
{
	// [MobiusU9B] A negative direction swaps which face is near.
	const SweepRay lookWest{200.0, 0.0, 41.0, -1.0, 0.0, 0.0};
	const SweepEntry entry = ComputeRayBoxEntry(lookWest, kImpAhead, kFar);
	EXPECT_TRUE(entry.hit);
	EXPECT_DOUBLE_EQ(80.0, entry.distance);
}

TEST(ComputeRayBoxEntry, EntersThroughTheTopWhenLookingDown)
{
	// [MobiusU9B] From 100 up at 45 degrees, the line drops to the 14-high candlestick's top 86 units out, inside its box.
	const SweepRay lookDown{0.0, 0.0, 100.0, 1.0, 0.0, -1.0};
	const SweepBox candlestick{90.0, 0.0, 0.0, 20.0, 14.0};
	const SweepEntry entry = ComputeRayBoxEntry(lookDown, candlestick, kFar);
	EXPECT_TRUE(entry.hit);
	EXPECT_DOUBLE_EQ(86.0, entry.distance);
}

TEST(ComputeRayBoxEntry, EntersThroughTheBottomWhenLookingUp)
{
	const SweepRay lookUp{0.0, 0.0, 0.0, 1.0, 0.0, 1.0};
	const SweepBox hanging{100.0, 0.0, 90.0, 20.0, 30.0};
	const SweepEntry entry = ComputeRayBoxEntry(lookUp, hanging, kFar);
	EXPECT_TRUE(entry.hit);
	EXPECT_DOUBLE_EQ(90.0, entry.distance);
}

TEST(ComputeRayBoxEntry, MeasuresInDirectionLengths)
{
	// [MobiusU9B] A direction twice as long halves the number, so callers pass a unit one to get map units.
	const SweepRay doubled{0.0, 0.0, 41.0, 2.0, 0.0, 0.0};
	EXPECT_DOUBLE_EQ(40.0, ComputeRayBoxEntry(doubled, kImpAhead, kFar).distance);
}

TEST(ComputeRayBoxEntry, MissesWhatIsBehind)
{
	const SweepBox behind{-100.0, 0.0, 0.0, 20.0, 56.0};
	EXPECT_FALSE(ComputeRayBoxEntry(kLookEast, behind, kFar).hit);
}

TEST(ComputeRayBoxEntry, MissesABoxAroundTheEye)
{
	const SweepBox around{0.0, 0.0, 0.0, 20.0, 56.0};
	EXPECT_FALSE(ComputeRayBoxEntry(kLookEast, around, kFar).hit);
}

TEST(ComputeRayBoxEntry, MissesABoxWhoseFaceIsAtTheEye)
{
	const SweepBox touching{20.0, 0.0, 0.0, 20.0, 56.0};
	EXPECT_FALSE(ComputeRayBoxEntry(kLookEast, touching, kFar).hit);
}

TEST(ComputeRayBoxEntry, MissesPastTheLimitButHitsAtIt)
{
	EXPECT_FALSE(ComputeRayBoxEntry(kLookEast, kImpAhead, 79.0).hit);
	EXPECT_TRUE(ComputeRayBoxEntry(kLookEast, kImpAhead, 80.0).hit);
}

TEST(ComputeRayBoxEntry, MissesBoxesWithoutSize)
{
	EXPECT_FALSE(ComputeRayBoxEntry(kLookEast, SweepBox{100.0, 0.0, 0.0, 0.0, 56.0}, kFar).hit);
	EXPECT_FALSE(ComputeRayBoxEntry(kLookEast, SweepBox{100.0, 0.0, 0.0, 20.0, 0.0}, kFar).hit);
	EXPECT_FALSE(ComputeRayBoxEntry(kLookEast, SweepBox{100.0, 0.0, 0.0, -20.0, 56.0}, kFar).hit);
	EXPECT_FALSE(ComputeRayBoxEntry(kLookEast, SweepBox{100.0, 0.0, 0.0, kNaN, 56.0}, kFar).hit);
	EXPECT_FALSE(ComputeRayBoxEntry(kLookEast, SweepBox{100.0, 0.0, 0.0, 20.0, kNaN}, kFar).hit);
}

TEST(ComputeRayBoxEntry, MissesBesideAboveAndBelowAParallelLine)
{
	// [MobiusU9B] Looking along x, the y and z axes are parallel, so only being between their faces counts.
	EXPECT_FALSE(ComputeRayBoxEntry(kLookEast, SweepBox{100.0, 30.0, 0.0, 20.0, 56.0}, kFar).hit);
	EXPECT_FALSE(ComputeRayBoxEntry(kLookEast, SweepBox{100.0, -30.0, 0.0, 20.0, 56.0}, kFar).hit);
	EXPECT_FALSE(ComputeRayBoxEntry(kLookEast, SweepBox{100.0, 0.0, 0.0, 20.0, 30.0}, kFar).hit);
	EXPECT_FALSE(ComputeRayBoxEntry(kLookEast, SweepBox{100.0, 0.0, 50.0, 20.0, 30.0}, kFar).hit);
}

TEST(ComputeRayBoxEntry, MissesWhenTheLinePassesACorner)
{
	// [MobiusU9B] Diagonally, the line clears the y faces before it reaches the x faces, so the clipped span is empty.
	const SweepRay diagonal{0.0, 0.0, 41.0, 1.0, 1.0, 0.0};
	EXPECT_FALSE(ComputeRayBoxEntry(diagonal, kImpAhead, kFar).hit);
}

TEST(ComputeRayBoxEntry, MissesForAZeroLengthLine)
{
	const SweepRay still{0.0, 0.0, 41.0, 0.0, 0.0, 0.0};
	EXPECT_FALSE(ComputeRayBoxEntry(still, kImpAhead, kFar).hit);
}

TEST(ComputeRayBoxExit, LeavesADoomPlayersBodyAtItsRadiusLookingStraight)
{
	EXPECT_DOUBLE_EQ(16.0, ComputeRayBoxExit(kLookEast, kPlayerBody));
}

TEST(ComputeRayBoxExit, LeavesThroughACornerFurtherOut)
{
	const double half = std::sqrt(0.5);
	const SweepRay diagonal{0.0, 0.0, 41.0, half, half, 0.0};
	EXPECT_NEAR(16.0 * std::sqrt(2.0), ComputeRayBoxExit(diagonal, kPlayerBody), 1e-9);
}

TEST(ComputeRayBoxExit, LeavesThroughTheFeetLookingSteeplyDown)
{
	const SweepRay steep{0.0, 0.0, 41.0, 1.0, 0.0, -4.0};
	EXPECT_DOUBLE_EQ(10.25, ComputeRayBoxExit(steep, kPlayerBody));
}

TEST(ComputeRayBoxExit, IsZeroForABoxTheLineDoesNotStartIn)
{
	EXPECT_DOUBLE_EQ(0.0, ComputeRayBoxExit(kLookEast, kImpAhead));
	EXPECT_DOUBLE_EQ(0.0, ComputeRayBoxExit(kLookEast, SweepBox{-100.0, 0.0, 0.0, 20.0, 56.0}));
	EXPECT_DOUBLE_EQ(0.0, ComputeRayBoxExit(kLookEast, SweepBox{0.0, 0.0, 0.0, 0.0, 56.0}));
}

TEST(ComputeRayBoxExit, IsZeroForALineWithNoDirection)
{
	const SweepRay still{0.0, 0.0, 41.0, 0.0, 0.0, 0.0};
	EXPECT_DOUBLE_EQ(0.0, ComputeRayBoxExit(still, kPlayerBody));
}

TEST(ComputeSweepConsiders, LooksAtDrawnActorsInTheBlockmap)
{
	EXPECT_TRUE(ComputeSweepConsiders(Decoration()));
}

TEST(ComputeSweepConsiders, SkipsTheViewerAndInvisibleActors)
{
	SweepActor viewer = Decoration();
	viewer.isViewer = true;
	EXPECT_FALSE(ComputeSweepConsiders(viewer));

	SweepActor invisible = Decoration();
	invisible.invisible = true;
	EXPECT_FALSE(ComputeSweepConsiders(invisible));
}

TEST(ComputeSweepConsiders, SkipsEffectsOutsideTheBlockmap)
{
	// [MobiusU9B] Bullet puffs, blood and teleport fog.
	SweepActor effect = Decoration();
	effect.inBlockmap = false;
	EXPECT_FALSE(ComputeSweepConsiders(effect));
}

TEST(ComputeSweepConsiders, LooksAtOtherProjectilesButNotTheViewersOwn)
{
	SweepActor fireball = Decoration();
	fireball.inBlockmap = false;
	fireball.isMissile = true;
	EXPECT_TRUE(ComputeSweepConsiders(fireball));

	SweepActor ownRocket = fireball;
	ownRocket.firedByViewer = true;
	EXPECT_FALSE(ComputeSweepConsiders(ownRocket));
}

TEST(ComputeSweepKind, SortsActorsByWhatTheyAre)
{
	SweepActor actor = Decoration();
	EXPECT_EQ(kSweepDecoration, ComputeSweepKind(actor));
	actor.isItem = true;
	EXPECT_EQ(kSweepItem, ComputeSweepKind(actor));
	actor.isMissile = true;
	EXPECT_EQ(kSweepProjectile, ComputeSweepKind(actor));
	actor.isMonster = true;
	EXPECT_EQ(kSweepMonster, ComputeSweepKind(actor));
	actor.isPlayer = true;
	EXPECT_EQ(kSweepPlayer, ComputeSweepKind(actor));
}

TEST(ComputeNearestSweepHit, IsNoneWithoutHits)
{
	EXPECT_EQ(-1, ComputeNearestSweepHit(std::vector<SweepHit>(), false, kFar));
}

TEST(ComputeNearestSweepHit, PicksTheNearestAndTheFirstOnATie)
{
	const std::vector<SweepHit> hits = {{300.0, true}, {120.0, true}, {500.0, true}, {120.0, true}};
	EXPECT_EQ(1, ComputeNearestSweepHit(hits, false, kFar));
}

TEST(ComputeNearestSweepHit, CanLookPastWhatIsNotMeasurable)
{
	const std::vector<SweepHit> hits = {{120.0, false}, {300.0, true}};
	EXPECT_EQ(0, ComputeNearestSweepHit(hits, false, kFar));
	EXPECT_EQ(1, ComputeNearestSweepHit(hits, true, kFar));
}

TEST(ComputeNearestSweepHit, IgnoresHitsPastTheLimit)
{
	const std::vector<SweepHit> hits = {{3000.0, true}, {2048.0, true}};
	EXPECT_EQ(1, ComputeNearestSweepHit(hits, false, 2048.0));
	EXPECT_EQ(-1, ComputeNearestSweepHit(hits, false, 2000.0));
}

TEST(ComputeSweepDue, RunsOncePerTicAndViewer)
{
	EXPECT_FALSE(ComputeSweepDue(10, 10, true));
	EXPECT_TRUE(ComputeSweepDue(10, 11, true));
	EXPECT_TRUE(ComputeSweepDue(10, 10, false));
}
