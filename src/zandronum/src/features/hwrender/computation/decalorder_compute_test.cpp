// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Each test below is a layering fault that shipped and was reported from a screenshot. The
// names say which one, so a failure points at the symptom rather than at an abstraction.
//
// The numbers are the real ones, read out of fua_dg_blendorder on the wall each was reported from.
// That matters: the rule these replaced required two distances to be EXACTLY equal, which looks
// perfectly reasonable until you see that the pair it was written for differs by a fifth of a
// percent. Made-up round numbers would have let it pass.

#include <gtest/gtest.h>

#include "features/hwrender/computation/decalorder_compute.h"

using namespace zx::hwrender;

namespace {

// A mark on a wall, at the point one plasma bolt landed on dbab04.
TranslucentDraw Mark(float distSq, int blend, unsigned first)
{
	TranslucentDraw d;
	d.distSq = distSq;
	d.cx = -1324.f; d.cy = 94.f; d.cz = 64.f;
	d.blend = blend;
	d.decal = true;
	d.first = first;
	return d;
}

TranslucentDraw Sprite(float distSq, int blend, unsigned first)
{
	TranslucentDraw d;
	d.distSq = distSq;
	d.cx = -1324.f; d.cy = 94.f; d.cz = 64.f;
	d.blend = blend;
	d.decal = false;
	d.first = first;
	return d;
}

}   // namespace

// ---- the scorch over its own glow ------------------------------------------
//
// Reported twice, and fixed twice without effect before the draw list was printed. A bolt leaves a
// black scorch and an additive glow at one point; the glow belongs on top.

TEST(DecalOrderCompute, GlowIsDrawnAfterTheScorchItBelongsOnTopOf)
{
	const TranslucentDraw glow   = Mark(22546.f, 2, 6);
	const TranslucentDraw scorch = Mark(22367.f, 1, 24);
	EXPECT_TRUE(ComputeDrawsBefore(scorch, glow));
	EXPECT_FALSE(ComputeDrawsBefore(glow, scorch));
}

TEST(DecalOrderCompute, TheGlowIsFartherAwayAndStillGoesLast)
{
	// The trap in one line. Farthest-first is the general rule and the glow IS the farther of the
	// two, so obeying distance here is exactly what buried it.
	const TranslucentDraw glow   = Mark(22546.f, 2, 6);
	const TranslucentDraw scorch = Mark(22367.f, 1, 24);
	EXPECT_GT(glow.distSq, scorch.distSq);
	EXPECT_TRUE(ComputeDrawsBefore(scorch, glow));
}

TEST(DecalOrderCompute, NearlyEqualIsNotEqualAndStillOrdersByBlend)
{
	// The rule this replaced asked for exact equality and so never fired once. Any pair that differs
	// at all, however little, must still be settled by blend.
	const TranslucentDraw glow   = Mark(22546.f, 2, 6);
	const TranslucentDraw scorch = Mark(22545.999f, 1, 24);
	EXPECT_NE(glow.distSq, scorch.distSq);
	EXPECT_TRUE(ComputeDrawsBefore(scorch, glow));
}

// ---- a fresh mark covers an old one ----------------------------------------

TEST(DecalOrderCompute, TwoScorchesOnOneSpotGoOldestFirst)
{
	const TranslucentDraw older = Mark(22400.f, 1, 12);
	const TranslucentDraw newer = Mark(22400.f, 1, 48);
	EXPECT_TRUE(ComputeDrawsBefore(older, newer));
	EXPECT_FALSE(ComputeDrawsBefore(newer, older));
}

// ---- marks that are not on one spot ----------------------------------------

TEST(DecalOrderCompute, MarksOnDifferentWallsGoFarthestFirst)
{
	// Beyond a decal's own width two marks cannot overlap, so the general rule applies again and the
	// far one is drawn first. Without this, blend would decide the order of every decal in the level.
	TranslucentDraw near = Mark(1000.f, 1, 4);
	TranslucentDraw far  = Mark(9000.f, 2, 8);
	far.cx += 4000.f;
	EXPECT_TRUE(ComputeDrawsBefore(far, near));
}

// ---- a mark against the thing standing in front of it ----------------------
//
// The impact flash sits at the point of the mark it just made. Drawn under the mark, the scorch
// shows through the flash -- reported on continuous fire.

TEST(DecalOrderCompute, ADecalSortsFartherThanASpriteAtTheSamePoint)
{
	const float raw = 10000.f;
	EXPECT_GT(ComputeSortDistance(raw, true), ComputeSortDistance(raw, false));
}

TEST(DecalOrderCompute, TheFlashIsDrawnOverTheMarkItJustMade)
{
	const float raw = 10000.f;
	const TranslucentDraw mark  = Mark(ComputeSortDistance(raw, true), 1, 6);
	const TranslucentDraw flash = Sprite(ComputeSortDistance(raw, false), 2, 42);
	EXPECT_TRUE(ComputeDrawsBefore(mark, flash));
	EXPECT_FALSE(ComputeDrawsBefore(flash, mark));
}

TEST(DecalOrderCompute, TheNudgeOnlyDecidesNearCoincidentPairs)
{
	// Proportional and small: it must never reorder something genuinely in front of or behind.
	EXPECT_LT(ComputeSortDistance(1000.f, true), ComputeSortDistance(2000.f, false));
	EXPECT_GT(ComputeSortDistance(2000.f, true), ComputeSortDistance(1000.f, false));
}

// ---- the comparator itself has to be legal ---------------------------------
//
// std::sort with a comparator that is not a strict weak ordering is undefined behaviour, not merely
// a wrong order, and it reads as a rare crash rather than as a sorting bug.

TEST(DecalOrderCompute, OrderingIsIrreflexive)
{
	const TranslucentDraw d = Mark(22546.f, 2, 6);
	EXPECT_FALSE(ComputeDrawsBefore(d, d));
}

TEST(DecalOrderCompute, OrderingIsAsymmetric)
{
	const TranslucentDraw items[] = {
		Mark(22546.f, 2, 6), Mark(22367.f, 1, 24), Mark(22367.f, 1, 30),
		Sprite(10179.f, 2, 42), Sprite(10179.f, 1, 48), Mark(999999.f, 1, 2),
	};
	const int n = (int)(sizeof(items) / sizeof(items[0]));
	for (int i = 0; i < n; i++)
		for (int j = 0; j < n; j++)
			if (ComputeDrawsBefore(items[i], items[j]))
				EXPECT_FALSE(ComputeDrawsBefore(items[j], items[i]))
					<< "pair " << i << "," << j << " orders both ways";
}
