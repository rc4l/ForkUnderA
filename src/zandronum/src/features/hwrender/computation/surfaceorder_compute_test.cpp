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
