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
	d.blend = blend;
	d.decal = true;
	d.first = first;
	return d;
}

TranslucentDraw Sprite(float distSq, int blend, unsigned first)
{
	TranslucentDraw d;
	d.distSq = distSq;
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

// ---- marks from DIFFERENT bolts that still overlap on screen ---------------
//
// The coincidence radius answered a scorch against its OWN glow and nothing else. Continuous fire
// puts marks all over a wall, and a scorch from one bolt overlaps the glow of another while sitting
// far enough away to be ordered by distance instead -- so it lands second and eats a hole in it.
// Reported as a dark ring inside the white flash, in Vulkan only, on a wall being hosed with plasma.

TEST(DecalOrderCompute, AScorchNeverLandsOnTopOfAGlowEvenFromADifferentBolt)
{
	const TranslucentDraw glow   = Mark(8529.f, 2, 96);    // another bolt's glow, farther off
	const TranslucentDraw scorch = Mark(8011.f, 1, 108);   // this bolt's scorch, NEARER, so ordering
	                                                       // by distance would draw it last
	EXPECT_TRUE(ComputeDrawsBefore(scorch, glow));
	EXPECT_FALSE(ComputeDrawsBefore(glow, scorch));
}

TEST(DecalOrderCompute, MarksOnOneWallOrderByAgeNotByDistance)
{
	// GL walks a sidedef's attached decals in list order and draws them all with that wall, so on one
	// surface the order is creation order. Distance between two coplanar quads is an artefact of
	// where each centre falls and says nothing about which was laid down first.
	const TranslucentDraw older = Mark(3000.f, 1, 12);
	const TranslucentDraw newer = Mark(9000.f, 1, 48);
	EXPECT_TRUE(ComputeDrawsBefore(older, newer));
}

// ---- a mark is paint on a surface, so nothing in front of it is a mark -----

TEST(DecalOrderCompute, EveryDecalIsDrawnBeforeEverySprite)
{
	// GL cannot get this wrong: decals are drawn as passengers of the wall they are glued to, in an
	// earlier pass than sprites entirely. Here it has to be stated.
	const TranslucentDraw nearMark   = Mark(1000.f, 1, 4);
	const TranslucentDraw farSprite  = Sprite(90000.f, 2, 8);
	EXPECT_TRUE(ComputeDrawsBefore(nearMark, farSprite));
	EXPECT_FALSE(ComputeDrawsBefore(farSprite, nearMark));
}

TEST(DecalOrderCompute, SpritesStillGoFarthestFirstAmongThemselves)
{
	const TranslucentDraw near = Sprite(1000.f, 2, 4);
	const TranslucentDraw far  = Sprite(9000.f, 1, 8);
	EXPECT_TRUE(ComputeDrawsBefore(far, near));
}

// ---- a mark against the thing standing in front of it ----------------------
//
// The impact flash sits at the point of the mark it just made. Drawn under the mark, the scorch
// shows through the flash -- reported on continuous fire.

TEST(DecalOrderCompute, TheFlashIsDrawnOverTheMarkItJustMade)
{
	// At the same point, which is where the flash always is: it is standing on the mark it just made.
	const TranslucentDraw mark  = Mark(10000.f, 1, 6);
	const TranslucentDraw flash = Sprite(10000.f, 2, 42);
	EXPECT_TRUE(ComputeDrawsBefore(mark, flash));
	EXPECT_FALSE(ComputeDrawsBefore(flash, mark));
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
