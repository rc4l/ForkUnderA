// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/levelmesh/computation/fogboundary_compute.h"

using namespace zx::levelmesh;

namespace {

// [rc4l] No outside fog (its alpha unset) and no level density: each side fogs by its own light, case 4.
const FogRules kByLight = { 0, 0xff000000u, 0, false };

} // namespace

TEST(FogBoundary, AFoggedDimSectorBesideABrightClearOneGetsASheet)
{
	EXPECT_TRUE(ComputeFogBoundary({ 0x808080, 128 }, { 0, 200 }, kByLight, false));
	// [rc4l] The same sector on both sides never does, nor an unfogged front, nor a front bright enough not to fog.
	EXPECT_FALSE(ComputeFogBoundary({ 0x808080, 128 }, { 0, 200 }, kByLight, true));
	EXPECT_FALSE(ComputeFogBoundary({ 0, 128 }, { 0, 200 }, kByLight, false));
	EXPECT_FALSE(ComputeFogBoundary({ 0x808080, 248 }, { 0, 200 }, kByLight, false));
	// [rc4l] A back side that fogs too has no boundary: by its own colour and dim light, or as bright fog that does not.
	EXPECT_FALSE(ComputeFogBoundary({ 0x808080, 128 }, { 0x404040, 128 }, kByLight, false));
	EXPECT_TRUE(ComputeFogBoundary({ 0x808080, 128 }, { 0x404040, 255 }, kByLight, false));
}

// [rc4l] With the level's own fog density, or light mode 4, every fade colour fogs: a front's, whatever its light, and a back's, which so has no boundary.
TEST(FogBoundary, TheLevelsDensityFogsEveryColouredSide)
{
	const FogRules dense = { 0, 0xff000000u, 64, false };
	EXPECT_TRUE(ComputeFogBoundary({ 0x808080, 255 }, { 0, 255 }, dense, false));
	EXPECT_FALSE(ComputeFogBoundary({ 0x808080, 255 }, { 0x404040, 255 }, dense, false));
	const FogRules mode4 = { 0, 0xff000000u, 0, true };
	EXPECT_TRUE(ComputeFogBoundary({ 0x808080, 255 }, { 0, 255 }, mode4, false));
}

// [rc4l] The outside fog fogs a front in its colour, and a back in its colour is fogged too.
TEST(FogBoundary, TheOutsideFogCountsOnlyInItsOwnColour)
{
	const FogRules outside = { 32, 0x00a0b0c0u, 0, false };
	EXPECT_TRUE(ComputeFogBoundary({ 0xa0b0c0, 255 }, { 0, 255 }, outside, false));
	EXPECT_FALSE(ComputeFogBoundary({ 0x808080, 128 }, { 0xa0b0c0, 255 }, outside, false));
	// [rc4l] An outside fog whose colour was never set (alpha 0xff) is not one.
	const FogRules unset = { 32, 0xffa0b0c0u, 0, false };
	EXPECT_FALSE(ComputeFogBoundary({ 0xa0b0c0, 255 }, { 0, 255 }, unset, false));
}

TEST(FogBoundary, TheSheetSpansTheOpeningBetweenTheLowerCeilingAndHigherFloor)
{
	MidOpeningIn in = { { 128, 128 }, { 0, 0 }, { 96, 96 }, { 16, 32 }, false, false, false, false };
	float top[2], bottom[2];
	ASSERT_TRUE(ComputeFogSheetOpening(in, top, bottom));
	EXPECT_EQ(96.f, top[0]); EXPECT_EQ(16.f, bottom[0]); EXPECT_EQ(32.f, bottom[1]);
	// [rc4l] With the upper and lower missing it takes the higher ceiling and the lower floor.
	in.upperMissing = in.lowerMissing = true;
	ASSERT_TRUE(ComputeFogSheetOpening(in, top, bottom));
	EXPECT_EQ(128.f, top[0]); EXPECT_EQ(0.f, bottom[1]);
	// [rc4l] Between two sky ceilings with no upper it is not clipped, which for no middle texture is 0.
	in.frontCeilingSky = in.backCeilingSky = true;
	ASSERT_FALSE(ComputeFogSheetOpening(in, top, bottom));
}

// [rc4l] A back ceiling above the front's, or a back floor below it, is used whole and the front's planes clip the sheet.
TEST(FogBoundary, IntersectingPlanesUseTheBackSectors)
{
	MidOpeningIn in = { { 128, 128 }, { 0, 0 }, { 160, 96 }, { -16, 8 }, false, false, false, false };
	float top[2], bottom[2];
	ASSERT_TRUE(ComputeFogSheetOpening(in, top, bottom));
	EXPECT_EQ(160.f, top[0]); EXPECT_EQ(96.f, top[1]);
	EXPECT_EQ(-16.f, bottom[0]); EXPECT_EQ(8.f, bottom[1]);
	// [rc4l] ...unless the front's ceiling is sky and the back's is not, when the lower one is kept.
	in.frontCeilingSky = true;
	ASSERT_TRUE(ComputeFogSheetOpening(in, top, bottom));
	EXPECT_EQ(128.f, top[0]); EXPECT_EQ(96.f, top[1]);
}

TEST(FogBoundary, AClosedLineHasNoSheet)
{
	const MidOpeningIn in = { { 64, 64 }, { 0, 0 }, { 32, 32 }, { 32, 32 }, false, false, false, false };
	float top[2], bottom[2];
	EXPECT_FALSE(ComputeFogSheetOpening(in, top, bottom));
}
