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

#include <vector>

#include "features/hwrender/computation/surfaceorder_compute.h"

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

// ---------------------------------------------------------------------------------------------
// The build-time order, which decides what goes into the vertex buffer before what.
// ---------------------------------------------------------------------------------------------

namespace {

ScenePiece Piece(int blend, const void *mat, const void *base)
{
	ScenePiece p;
	p.blendMode = blend; p.material = mat; p.baseTex = base;
	return p;
}

// Stand-in handles. Their values are meaningless; only their ordering matters.
const void *const kMatA = (const void *)0x1000;
const void *const kMatB = (const void *)0x2000;
const void *const kTexA = (const void *)0x10;
const void *const kTexB = (const void *)0x20;

} // namespace

// The correctness half: a blended surface has to be drawn after everything it shows through, so it
// can never sort into the middle of the opaque run -- whatever its material says.
TEST(SurfaceOrder, BlendedNeverPrecedesOpaque)
{
	const ScenePiece opaque = Piece(0, kMatB, kTexB);   // materials chosen to sort the OTHER way
	const ScenePiece blended = Piece(1, kMatA, kTexA);
	EXPECT_TRUE(ComputePiecesBefore(opaque, blended));
	EXPECT_FALSE(ComputePiecesBefore(blended, opaque));
}

// The batching half: equal state adjacent, so a material draws once instead of once per piece.
TEST(SurfaceOrder, EqualStateSortsTogether)
{
	const ScenePiece a = Piece(0, kMatA, kTexA);
	const ScenePiece b = Piece(0, kMatA, kTexA);
	EXPECT_FALSE(ComputePiecesBefore(a, b));
	EXPECT_FALSE(ComputePiecesBefore(b, a));   // neither before the other: they may batch
}

// [rc4l] Base texture is the secondary key and it is not cosmetic. Two surfaces with different base
// textures can resolve to the SAME material at bake time, because an animation is at some frame when
// the mesh is built -- and merged into one batch they are then re-resolved every frame from whichever
// baseTex the batch recorded. That is a lava floor turning into green foliage.
TEST(SurfaceOrder, SameMaterialDifferentBaseTextureStaysSeparable)
{
	const ScenePiece lava = Piece(0, kMatA, kTexA);
	const ScenePiece vine = Piece(0, kMatA, kTexB);
	EXPECT_TRUE(ComputePiecesBefore(lava, vine));
	EXPECT_FALSE(ComputePiecesBefore(vine, lava));
}

// A comparator that says "a before b" AND "b before a" makes std::sort walk off the end of the array
// in a release build. Cheap to assert, and the failure is a crash rather than a picture.
TEST(SurfaceOrder, IsAStrictWeakOrdering)
{
	const ScenePiece all[] = {
		Piece(0, kMatA, kTexA), Piece(0, kMatA, kTexB), Piece(0, kMatB, kTexA),
		Piece(1, kMatA, kTexA), Piece(1, kMatB, kTexB), Piece(2, kMatA, kTexA),
	};
	for (const ScenePiece &x : all)
		for (const ScenePiece &y : all)
		{
			EXPECT_FALSE(ComputePiecesBefore(x, y) && ComputePiecesBefore(y, x));
			if (ComputePiecesBefore(x, y))
				for (const ScenePiece &z : all)
					if (ComputePiecesBefore(y, z)) EXPECT_TRUE(ComputePiecesBefore(x, z));
		}
}

// ---------------------------------------------------------------------------------------------
// GL's sprite rule, which is deliberately NOT the port's.
// ---------------------------------------------------------------------------------------------

TEST(SurfaceOrder, GLSpritesGoFarthestFirst)
{
	GLSpriteOrder near_, far_;
	near_.depth = 100; near_.spawnIndex = 1;
	far_.depth = 900;  far_.spawnIndex = 2;
	EXPECT_TRUE(ComputeGLSpritesBefore(far_, near_, false));
	EXPECT_FALSE(ComputeGLSpritesBefore(near_, far_, false));
}

// The tie-break flips with COMPATF_SPRITESORT, and maps depend on the older behaviour: two sprites at
// the same depth stack the other way round, which is the whole point of the flag.
TEST(SurfaceOrder, GLSpriteTieBreakFollowsTheCompatFlag)
{
	GLSpriteOrder first, second;
	first.depth = 500;  first.spawnIndex = 1;
	second.depth = 500; second.spawnIndex = 2;
	EXPECT_TRUE(ComputeGLSpritesBefore(first, second, true));    // compat: lower index first
	EXPECT_FALSE(ComputeGLSpritesBefore(first, second, false));  // default: higher index first
	EXPECT_TRUE(ComputeGLSpritesBefore(second, first, false));
}

// And the two rules genuinely differ, which is the reason both are written down here. GL breaks a
// depth tie by spawn order; the port breaks a distance tie by what the surface IS -- a decal before
// the thing it is painted on. Asserting the difference stops someone "unifying" them by hand.
TEST(SurfaceOrder, TheTwoRulesAreNotTheSameRule)
{
	TranslucentDraw decal, wall;
	decal.distSq = 1000.f; decal.blend = 1; decal.decal = true;  decal.first = 900;
	wall.distSq  = 1000.f; wall.blend  = 1; wall.decal  = false; wall.first  = 100;
	// The port: a decal is a STAGE, drawn before the other blended things at that distance, so a
	// flash composites over the mark rather than the mark landing on top of the flash. Which way
	// round that goes is the whole content of two shipped bugs, so it is asserted explicitly.
	EXPECT_TRUE(ComputeDrawsBefore(decal, wall));
	EXPECT_FALSE(ComputeDrawsBefore(wall, decal));

	// GL, given the same pair as sprites, would answer purely on spawn index.
	GLSpriteOrder a, b;
	a.depth = 1000; a.spawnIndex = 900;
	b.depth = 1000; b.spawnIndex = 100;
	EXPECT_TRUE(ComputeGLSpritesBefore(a, b, false));   // higher index first, regardless of what it is
}

namespace {

BlendSpan Span(float lo, float hi, bool surface) { BlendSpan s; s.lo = lo; s.hi = hi; s.surface = surface; return s; }

} // namespace

// [rc4l] wdiexsec: a medkit on the pool floor sat farther than the water piece over it, so the distance sort drew it on top of the water.
TEST(SurfaceOrder, AnItemBeyondATranslucentPlaneDrawsBeforeIt)
{
	TranslucentDraw water = Sprite(16000.f, 1, 0);
	water.layer = 0; water.plane = true;
	TranslucentDraw medkit = Sprite(12000.f, 0, 10);
	medkit.layer = 1;
	EXPECT_TRUE(ComputeDrawsBefore(medkit, water));
	EXPECT_FALSE(ComputeDrawsBefore(water, medkit));
}

// [rc4l] Within one layer the plane goes first, because everything sharing its layer is on the eye's side of it.
TEST(SurfaceOrder, APlaneDrawsBeforeItemsOnTheEyesSideOfIt)
{
	TranslucentDraw water = Sprite(100.f, 1, 0);
	water.plane = true;
	const TranslucentDraw above = Sprite(9000.f, 1, 10);
	EXPECT_TRUE(ComputeDrawsBefore(water, above));
	EXPECT_FALSE(ComputeDrawsBefore(above, water));
}

TEST(SurfaceOrder, DecalsStayAStageAheadOfTheLayers)
{
	TranslucentDraw deep = Sprite(100.f, 1, 0);
	deep.layer = 3;
	const TranslucentDraw mark = Mark(100.f, 1, 10);
	EXPECT_TRUE(ComputeDrawsBefore(mark, deep));
}

TEST(SurfaceOrder, LayersCountTheLevelPlanesBetweenAnItemAndAnEyeAboveThem)
{
	const BlendSpan spans[6] = {
		Span(-80.f, -80.f, true),     // [rc4l] the water's surface
		Span(-154.f, -130.f, false),  // [rc4l] a medkit on the pool floor
		Span(-154.f, -80.f, true),    // [rc4l] a 3D floor's side under the water, its top on the surface
		Span(-100.f, -40.f, false),   // [rc4l] a sprite standing in the water
		Span(-80.f, -80.f, true),     // [rc4l] another piece of the same water
		Span(-30.f, -10.f, false),    // [rc4l] a sprite on the bank
	};
	int layers[6];
	bool planes[6];
	ComputeBlendLayers(spans, 6, 0.f, layers, planes);
	const int wantLayers[6] = { 0, 1, 1, 0, 0, 0 };
	const bool wantPlanes[6] = { true, false, false, false, true, false };
	for (int i = 0; i < 6; i++) { EXPECT_EQ(wantLayers[i], layers[i]) << i; EXPECT_EQ(wantPlanes[i], planes[i]) << i; }
}

TEST(SurfaceOrder, AnEyeUnderThePlaneCountsWhatIsAboveIt)
{
	const BlendSpan spans[4] = {
		Span(-80.f, -80.f, true),
		Span(-70.f, -40.f, false),    // [rc4l] a sprite above the water, seen from under it
		Span(-154.f, -130.f, false),  // [rc4l] the medkit, on the eye's side now
		Span(-80.f, -20.f, true),     // [rc4l] a wall rising from the surface
	};
	int layers[4];
	bool planes[4];
	ComputeBlendLayers(spans, 4, -120.f, layers, planes);
	EXPECT_EQ(1, layers[1]);
	EXPECT_EQ(0, layers[2]);
	EXPECT_EQ(1, layers[3]);
	EXPECT_EQ(0, layers[0]);
}

// [rc4l] Only a level piece of the level splits the order: a sloped one, or a sprite with no height, does not.
TEST(SurfaceOrder, OnlyLevelSurfacesAreSplittingPlanes)
{
	const BlendSpan spans[4] = {
		Span(-80.f, -60.f, true),
		Span(-50.f, -50.f, false),
		Span(-120.f, -100.f, false),
		Span(-200.f, -200.f, true),
	};
	int layers[4];
	bool planes[4];
	ComputeBlendLayers(spans, 4, 0.f, layers, planes);
	EXPECT_FALSE(planes[0]);
	EXPECT_FALSE(planes[1]);
	EXPECT_EQ(0, layers[2]);
	EXPECT_TRUE(planes[3]);
	EXPECT_EQ(0, layers[3]);
}

// [rc4l] Two pieces of water at one height are one plane, so an item under both is one layer down, not two.
TEST(SurfaceOrder, CoplanarPiecesCountOnce)
{
	const BlendSpan spans[3] = { Span(-80.f, -80.f, true), Span(-80.f, -80.f, true), Span(-154.f, -130.f, false) };
	int layers[3];
	bool planes[3];
	EXPECT_EQ(1, ComputeBlendLayers(spans, 3, 0.f, layers, planes));
	EXPECT_EQ(1, layers[2]);
}

namespace { struct P3 { float x, y, z, u; }; }

// [rc4l] WDI07's WFALL1 waterfall scrolls every tic: its UVs change and its corners do not, so the patch keeps the scene's order.
TEST(SurfaceOrder, AScrolledSurfaceKeepsItsPositionsAndAMovedOneDoesNot)
{
	const P3 before[2] = { { 1, 2, 3, 0.0f }, { 4, 5, 6, 0.5f } };
	const P3 scrolled[2] = { { 1, 2, 3, 0.25f }, { 4, 5, 6, 0.75f } };
	P3 moved[2] = { { 1, 2, 3, 0.0f }, { 4, 5, 6, 0.5f } };
	EXPECT_TRUE(ComputeSamePositions(before, scrolled, 2));
	for (int axis = 0; axis < 3; axis++)
	{
		moved[1] = before[1];
		(axis == 0 ? moved[1].x : axis == 1 ? moved[1].y : moved[1].z) += 1.f;
		EXPECT_FALSE(ComputeSamePositions(before, moved, 2)) << axis;
	}
	EXPECT_TRUE(ComputeSamePositions(before, moved, 0));
}

// [rc4l] The key orders every pair as the rule does, over draws mixing every field the rule reads.
TEST(SurfaceOrder, TheSortKeyIsTheRule)
{
	std::vector<TranslucentDraw> draws;
	const float dists[] = { 0.f, 1.f, 1.5f, 64.f, 4096.f, 1.e9f };
	unsigned first = 0;
	for (int decal = 0; decal < 2; decal++)
		for (int blend = 1; blend <= 2; blend++)
			for (int layer = 0; layer < 3; layer++)
				for (int plane = 0; plane < 2; plane++)
					for (float dist : dists)
						for (int age = 0; age < 2; age++)
						{
							TranslucentDraw d;
							d.distSq = dist; d.blend = blend; d.decal = decal != 0; d.layer = layer; d.plane = plane != 0;
							d.first = (first++ * 2654435761u) >> (age * 3);
							draws.push_back(d);
						}
	for (size_t i = 0; i < draws.size(); i++)
		for (size_t j = 0; j < draws.size(); j++)
			ASSERT_EQ(ComputeDrawsBefore(draws[i], draws[j]),
				ComputeTranslucentSortKey(draws[i]) < ComputeTranslucentSortKey(draws[j])) << i << " " << j;
}

TEST(SurfaceOrder, TheSortKeyClampsTheLayer)
{
	TranslucentDraw deep, beyond;
	deep.distSq = beyond.distSq = 1.f; deep.blend = beyond.blend = 1; deep.decal = beyond.decal = false; deep.first = beyond.first = 0;
	deep.layer = 0x20000;
	beyond.layer = 0xffff;
	EXPECT_FALSE(ComputeTranslucentSortKey(deep) < ComputeTranslucentSortKey(beyond));
	EXPECT_FALSE(ComputeTranslucentSortKey(beyond) < ComputeTranslucentSortKey(deep));
	deep.layer = -3;
	beyond.layer = 0;
	EXPECT_FALSE(ComputeTranslucentSortKey(deep) < ComputeTranslucentSortKey(beyond));
	EXPECT_FALSE(ComputeTranslucentSortKey(beyond) < ComputeTranslucentSortKey(deep));
}
