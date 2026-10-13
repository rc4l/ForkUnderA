// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/skylook_compute.h"

using namespace zx::hwrender;

namespace {

// [rc4l] A level with sky1 10 scrolled to 30 degrees and sky2 20 at 60, and a transfer line 4 whose upper is 40 and lower 41.
SkyLookIn Level()
{
	SkyLookIn in = {};
	in.sky1 = 10; in.sky2 = 20; in.sky1Pos = 30.f; in.sky2Pos = 60.f;
	in.lineTop = 40; in.lineBottom = 41;
	in.lineTopDraws = true; in.lineBottomValid = true; in.lineBottomDraws = true;
	in.lineTopX = 90.f; in.lineTopY = 8.f; in.lineBottomX = 180.f; in.lineBottomY = 16.f;
	in.lineMirrored = true;
	return in;
}

} // namespace

TEST(SkyLook, AnOrdinarySectorShowsSky1AtItsScroll)
{
	const SkyLook s = ComputeSkyLook(Level());
	EXPECT_EQ(10, s.texture); EXPECT_EQ(30.f, s.xOffset); EXPECT_EQ(0.f, s.yOffset);
	EXPECT_FALSE(s.mirrored); EXPECT_EQ(-1, s.secondTexture);
}

// [rc4l] Sky2 for a Sky2 sector, a swapped level and a double sky, whose second layer is sky1 at sky1's scroll.
TEST(SkyLook, Sky2ForASky2SectorASwapAndADoubleSky)
{
	SkyLookIn in = Level();
	in.sectorSky = kSkyFlatFlag;
	EXPECT_EQ(20, ComputeSkyLook(in).texture);
	EXPECT_EQ(60.f, ComputeSkyLook(in).xOffset);
	in = Level();
	in.swapSkies = true;
	EXPECT_EQ(20, ComputeSkyLook(in).texture);
	in = Level();
	in.doubleSky = true;
	const SkyLook d = ComputeSkyLook(in);
	EXPECT_EQ(20, d.texture); EXPECT_EQ(10, d.secondTexture); EXPECT_EQ(30.f, d.secondXOffset);
}

// [rc4l] With sky2 the same as sky1 every one of those shows sky1 at sky1's scroll, and a double sky has no second layer of it.
TEST(SkyLook, TheSameTwoSkiesAreOneAtSky1sScroll)
{
	SkyLookIn in = Level();
	in.sky2 = 10;
	in.doubleSky = true;
	const SkyLook s = ComputeSkyLook(in);
	EXPECT_EQ(10, s.texture); EXPECT_EQ(30.f, s.xOffset); EXPECT_EQ(-1, s.secondTexture);
}

// [rc4l] MBF's transfer: the line's upper, its offsets and its mirroring, or under lightning its lower.
TEST(SkyLook, ATransferLineGivesItsOwnSky)
{
	SkyLookIn in = Level();
	in.sectorSky = kSkyFlatFlag | 5;
	SkyLook s = ComputeSkyLook(in);
	EXPECT_EQ(40, s.texture); EXPECT_EQ(90.f, s.xOffset); EXPECT_EQ(8.f, s.yOffset); EXPECT_TRUE(s.mirrored);
	in.swapSkies = true;
	s = ComputeSkyLook(in);
	EXPECT_EQ(41, s.texture); EXPECT_EQ(180.f, s.xOffset); EXPECT_EQ(16.f, s.yOffset);
	// [rc4l] Lightning keeps the upper when the line has no lower at all.
	in.lineBottomValid = false;
	EXPECT_EQ(40, ComputeSkyLook(in).texture);
}

// [rc4l] A line whose texture does not draw falls back to the level's sky, as GL's goto normalsky does.
TEST(SkyLook, ALineWithANullTextureFallsBackToTheLevelsSky)
{
	SkyLookIn in = Level();
	in.sectorSky = kSkyFlatFlag | 5;
	in.lineTopDraws = false;
	EXPECT_EQ(10, ComputeSkyLook(in).texture);
	in.swapSkies = true;
	in.lineBottomDraws = false;
	EXPECT_EQ(20, ComputeSkyLook(in).texture);
}

// [rc4l] The key round trip: the level's sky, sky2 and line 0 and line 7's skies, and a sector value with no flag is the level's.
TEST(SkyLook, ASectorsSkyRoundTripsThroughItsQuadKey)
{
	EXPECT_EQ(0, ComputeSkyQuadKey(0));
	EXPECT_EQ(0, ComputeSkyQuadKey(5));
	EXPECT_EQ(1, ComputeSkyQuadKey(kSkyFlatFlag));
	EXPECT_EQ(2, ComputeSkyQuadKey(kSkyFlatFlag | 1));
	EXPECT_EQ(8, ComputeSkyQuadKey(kSkyFlatFlag | 7));
	for (int sky : { 0, kSkyFlatFlag, kSkyFlatFlag | 1, kSkyFlatFlag | 7 })
		EXPECT_EQ(sky, ComputeSkyFromQuadKey(ComputeSkyQuadKey(sky)));
	EXPECT_EQ(0, ComputeSkyFromQuadKey(-3));
}

// [rc4l] RenderDome's -180 degree turn plus the scroll, the mirror's negated u, the y offset over the texture height at the dome's v scale, and the layer.
TEST(SkyLook, TheDomeDrawsAtItsScrollMirrorAndRaise)
{
	float c[4];
	ComputeSkyDrawConstants(90.f, true, 32.f, 2.f, 128, true, c);
	EXPECT_FLOAT_EQ(c[0], -3.14159265f * 0.5f);
	EXPECT_EQ(-1.f, c[1]);
	EXPECT_FLOAT_EQ(c[2], 0.5f);
	EXPECT_EQ(1.f, c[3]);
	ComputeSkyDrawConstants(180.f, false, 32.f, 1.f, 0, false, c);
	EXPECT_FLOAT_EQ(c[0], 0.f);
	EXPECT_EQ(1.f, c[1]);
	EXPECT_EQ(0.f, c[2]);
	EXPECT_EQ(0.f, c[3]);
}

TEST(SkyLook, TwoSkiesAreTheSameOnlyWhenEverythingDrawnMatches)
{
	const SkyLook a = { 10, 30.f, 0.f, false, -1, 0.f };
	SkyLook b = a;
	EXPECT_TRUE(ComputeSameSkyLook(a, b));
	b.secondXOffset = 5.f;   // [rc4l] no second layer, so its offset does not count
	EXPECT_TRUE(ComputeSameSkyLook(a, b));
	b = a; b.texture = 11; EXPECT_FALSE(ComputeSameSkyLook(a, b));
	b = a; b.xOffset = 31.f; EXPECT_FALSE(ComputeSameSkyLook(a, b));
	b = a; b.yOffset = 1.f; EXPECT_FALSE(ComputeSameSkyLook(a, b));
	b = a; b.mirrored = true; EXPECT_FALSE(ComputeSameSkyLook(a, b));
	b = a; b.secondTexture = 3; EXPECT_FALSE(ComputeSameSkyLook(a, b));
	SkyLook c = a; c.secondTexture = 3; c.secondXOffset = 1.f;
	SkyLook e = c; e.secondXOffset = 2.f;
	EXPECT_FALSE(ComputeSameSkyLook(c, e));
	e.secondXOffset = 1.f;
	EXPECT_TRUE(ComputeSameSkyLook(c, e));
}
