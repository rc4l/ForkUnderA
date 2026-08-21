// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Three of these are faults that shipped, on the same afternoon, and every one of them was a
// change that no channel happened to carry. They are written as the player described them, because
// that is the form the next one will arrive in.

#include <gtest/gtest.h>

#include "features/surfaces/computation/surfacechange_compute.h"

using namespace zx::surfaces;

namespace {

// Two textures, two sectors' worth of shading: pointers stand in for identity, which is all the
// comparison uses.
static const char kTexA = 'A', kTexB = 'B';

SurfaceKey Wall()
{
	SurfaceKey k;
	k.material = &kTexA;
	k.baseTex = &kTexA;
	k.rangeOffset = 1024; k.rangeCount = 6;
	k.blendMode = 0; k.translation = 0; k.alpha = 1.f;
	k.colorR = k.colorG = k.colorB = 0.5f;
	k.softLight = -1.f; k.fogDensity = 0.f; k.fogColor = 0; k.fogMode = 0;
	k.normX = 1.f; k.normY = 0.f; k.normZ = 0.f;
	return k;
}

} // namespace

TEST(SurfaceChange, ASurfaceThatDidNotChangeCostsNothing)
{
	EXPECT_EQ(kSurfaceUnchanged, ComputeSurfaceChange(Wall(), Wall()));
}

// [rc4l] The switch. Pressing one swaps the sidedef's texture; the seg re-bakes, the piece takes the
// new material, and NOT ONE VERTEX MOVES. Every mechanism that watched for moving vertices saw
// nothing, and the switch stayed looking unpressed.
TEST(SurfaceChange, ASwitchChangesItsMaterialAndNothingElse)
{
	SurfaceKey now = Wall();
	now.material = &kTexB;
	now.baseTex = &kTexB;
	EXPECT_EQ(kSurfaceRebatch, ComputeSurfaceChange(Wall(), now));
}

// [rc4l] ...but the material ALONE is an animation frame, and that is a repaint.
//
// An animated texture resolves to a different FMaterial every few tics while remaining the same
// surface in the same batch -- a batch is keyed on the base texture and the backend re-resolves from
// it every frame. Calling that a rebatch made every animated wall on a level with moving sectors
// force a full scene rebuild, 5126 of them on dbab04 in under a minute.
TEST(SurfaceChange, TheMaterialAloneIsAnAnimationFrame)
{
	SurfaceKey now = Wall();
	now.material = &kTexB;
	EXPECT_EQ(kSurfaceRepaint, ComputeSurfaceChange(Wall(), now));
}

// [rc4l] A pane of glass becoming opaque, or a surface joining the sorted pass. Blend mode decides
// WHICH PASS draws it, so it cannot be patched in place any more than a material can.
TEST(SurfaceChange, ChangingPassIsARebatch)
{
	SurfaceKey now = Wall();
	now.blendMode = 1;
	EXPECT_EQ(kSurfaceRebatch, ComputeSurfaceChange(Wall(), now));
}

TEST(SurfaceChange, ATranslatedSurfaceIsADifferentImage)
{
	SurfaceKey now = Wall();
	now.translation = 7;
	EXPECT_EQ(kSurfaceRebatch, ComputeSurfaceChange(Wall(), now));
}

// A piece re-baked at a different size lives somewhere else, which takes it out of whatever
// contiguous run it belonged to -- the same problem as a material change wearing different clothes.
TEST(SurfaceChange, MovedOrResizedGeometryIsARebatch)
{
	SurfaceKey moved = Wall(); moved.rangeOffset = 2048;
	EXPECT_EQ(kSurfaceRebatch, ComputeSurfaceChange(Wall(), moved));
	SurfaceKey resized = Wall(); resized.rangeCount = 12;
	EXPECT_EQ(kSurfaceRebatch, ComputeSurfaceChange(Wall(), resized));
}

// [rc4l] A door moving changes the light level of the surfaces it passes, and a lift changes the
// colour of its own sides. The vertices are in the right place and in the right batch; what is
// baked INTO them is stale.
TEST(SurfaceChange, ShadingAloneIsRepaintedInPlace)
{
	SurfaceKey now = Wall();
	now.colorR = 0.25f;
	EXPECT_EQ(kSurfaceRepaint, ComputeSurfaceChange(Wall(), now));
	SurfaceKey lit = Wall(); lit.softLight = 0.75f;
	EXPECT_EQ(kSurfaceRepaint, ComputeSurfaceChange(Wall(), lit));
	SurfaceKey fogged = Wall(); fogged.fogDensity = 0.002f;
	EXPECT_EQ(kSurfaceRepaint, ComputeSurfaceChange(Wall(), fogged));
	SurfaceKey faded = Wall(); faded.fogColor = 0x204060;
	EXPECT_EQ(kSurfaceRepaint, ComputeSurfaceChange(Wall(), faded));
}

// [rc4l] The normal decides which dynamic lights reach a surface at all -- see MeshPiece::normX. A
// slope that moves changes it without moving anything else, and a surface lit from the wrong side is
// the fault that reads as "the torch lights the back of the wall".
TEST(SurfaceChange, ANormalThatTurnedIsRepainted)
{
	SurfaceKey now = Wall();
	now.normZ = 1.f; now.normX = 0.f;
	EXPECT_EQ(kSurfaceRepaint, ComputeSurfaceChange(Wall(), now));
}

// [rc4l] Rebatching outranks repainting, because it costs more and does the other's job on the way.
// A caller comparing against kSurfaceRepaint must not be told "repaint" about a surface that has
// also changed material -- which is why the enum escalates and the material questions come first.
TEST(SurfaceChange, TheMoreExpensiveAnswerWins)
{
	// A surface that has both moved batch and changed colour must be told to rebatch, not repaint --
	// the cheaper answer would leave it in a batch it no longer belongs to.
	SurfaceKey now = Wall();
	now.baseTex = &kTexB;
	now.colorR = 0.25f;
	EXPECT_EQ(kSurfaceRebatch, ComputeSurfaceChange(Wall(), now));
	EXPECT_GT(kSurfaceRebatch, kSurfaceRepaint);
	EXPECT_GT(kSurfaceRepaint, kSurfaceUnchanged);
}

// A surface with no geometry is still a surface, and comparing two of them must not decide they
// differ -- an empty piece is registered on every frame a door is shut.
TEST(SurfaceChange, TwoEmptySurfacesAreTheSame)
{
	SurfaceKey a; SurfaceKey b;
	EXPECT_EQ(kSurfaceUnchanged, ComputeSurfaceChange(a, b));
}

// [rc4l] A surface that was squashed and has come back into the same range costs nothing.
//
// Squashing zeroes a surface's vertices and KEEPS its range so the geometry can return to it -- how
// a door's upper is held while the door is open. The stored count goes to zero to say so, and
// reading that as a RESIZE made every door and lift rebatch on the way back: 320,544 of them on
// dbab04 in under a minute, each one a full scene rebuild.
//
// Unchanged is the right answer and not a cop-out. The batch never stopped covering those vertices,
// nothing the backend reads per piece is different, and the vertices themselves travel back by the
// dirty range that MeshStore already raised. There is nothing left to tell anyone.
TEST(SurfaceChange, ComingBackFromASquashCostsNothing)
{
	SurfaceKey was = Wall(); was.rangeCount = 0;   // squashed
	SurfaceKey now = Wall();                       // back, same offset, same size as before
	EXPECT_EQ(kSurfaceUnchanged, ComputeSurfaceChange(was, now));
}

// ...but a surface that came back somewhere ELSE really has moved, and that is a rebatch.
TEST(SurfaceChange, ComingBackAtADifferentOffsetIsARebatch)
{
	SurfaceKey was = Wall(); was.rangeCount = 0;
	SurfaceKey now = Wall(); now.rangeOffset = 4096;
	EXPECT_EQ(kSurfaceRebatch, ComputeSurfaceChange(was, now));
}

// A genuine resize -- a wall whose split topology changed -- is still a rebatch.
TEST(SurfaceChange, AGenuineResizeIsStillARebatch)
{
	SurfaceKey was = Wall(); was.rangeCount = 6;
	SurfaceKey now = Wall(); now.rangeCount = 12;
	EXPECT_EQ(kSurfaceRebatch, ComputeSurfaceChange(was, now));
}
